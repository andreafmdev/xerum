#include "engine/SynthVoice.h"

#include "parameters/ParamCollect.h"

#include <cmath>

namespace engine
{
namespace
{
constexpr double kSmoothingSeconds = 0.02;

// Headroom fisso per voce. E' una costante, non un divisore sul numero di voci attive: un
// divisore farebbe "respirare" il volume ogni volta che una nota parte o finisce, un difetto
// peggiore del clipping che risolve.
//
// -4 dB. Era -3, ed e' **sceso**: la catena e' diventata piu' calda di quanto fosse quando quel
// numero fu scelto, e i tre bersagli di sicurezza non ci stavano piu' dentro. Misurato prima di
// questa ritaratura, tutto con il metodo del gain ridotto (vedi il commento di softClip in
// SynthEngine.cpp):
//
//   - l'accordo di quattro note ai default di fabbrica presentava 0.933 al clipper contro una
//     soglia di 0.95, cioe' **0.16 dB** di margine: non un margine, un arrotondamento;
//   - il caso peggiore (matrix pieno, `res` modulato a fondo, unison 8) presentava 2.832,
//     cioe' **+9.04 dBFS**, oltre il tetto di +8 che ci si e' dati (la nuova mappa di `drive`
//     ne ha tolto mezzo da sola, portandolo a 2.655, e non bastava);
//   - "Acid Line" **accendeva il clipper** su un accordo di quattro note (1.035 secco).
//
// A 0.63 i tre tornano dentro: l'accordo scende a 0.828 (1.20 dB di margine), il caso peggiore
// a 2.394 (+7.58 dBFS) e nessun preset arriva al clipper — il piu' caldo e' "Init", cioe' i
// default stessi, con 1.20 dB di margine. Il conto e' 1:1 e non c'e' modo di
// aggirarlo — tutto cio' che sta a monte del clipper e' lineare in questa costante, quindi ogni
// decibel di margine sull'accordo e' un decibel di livello dello strumento.
//
// **Cio' che si e' pagato.** Una nota sola a level 1.0 e volume 1.0 (tavola "Analog Saws",
// filtro a Q Butterworth) esce a -16.4 dBFS RMS invece di -15.4. Il riferimento che si cita per
// questa classe di strumenti — circa -14 dBFS RMS, dove stanno Serum e Vital — resta
// irraggiungibile, e la ragione e' aritmetica e non prudenza: il fattore di cresta fra l'RMS di
// una nota e il picco di un accordo di quattro e' fisso a 16.4 dB, quindi una nota a -14 dBFS
// RMS mette l'accordo a +0.5 dBFS anche contando il volume di default. Nessuna soglia e nessuna
// forma di soft clipper con un tetto a fondo scala puo' farci passare un accordo intatto: i due
// bersagli non stanno insieme, e qui vince l'accordo pulito. La tabella completa del compromesso
// sta in docs/architecture.md, sezione "Voice engine".
//
// Il valore precedente (0.4, -8 dB) lasciava quella stessa nota a -20.4 dBFS RMS: uno strumento
// che bisognava alzare di sei decibel nel mixer prima di poterlo giudicare. E prima ancora era
// -20 dB, scelto quando il filtro poteva da solo aggiungere +30 dB di picco e la saturazione ne
// aggiungeva altri 3.5 anche a drive zero. Il verso di quella storia non si e' invertito: da
// -20 a -4 dB lo strumento e' salito di sedici decibel, e questo ultimo decibel all'indietro e'
// il prezzo del margine, non un ritorno indietro.
//
// Misure in EngineTests, "gain staging: una nota e un accordo normale stanno sotto il soft
// clipper" e "il caso peggiore resta sotto il tetto di +8 dBFS".
constexpr float kVoiceHeadroomGain = 0.63f; // 10^(-4/20)

/**
 * Il numero di nota e' un **float** e non un intero, e non e' un allargamento di comodo: e' il
 * punto in cui il glide entra nella catena.
 *
 * Con `note` uguale a `(float) midiNote_` — cioe' fuori da un glide, e sempre quando `glide` sta
 * a zero — l'espressione e' esattamente quella di prima, stesse operazioni nello stesso ordine
 * sugli stessi valori: il risultato ha gli stessi bit. La nota frazionaria e' quindi una strada
 * in piu' che si apre, non una strada diversa per chi passava di qui gia' prima.
 */
float midiNoteToHz (float note, float offsetSemitones) noexcept
{
    // std::pow gira una volta per sotto-fetta di controllo, mai per campione: niente libm nel
    // loop audio.
    return 440.0f * std::pow (2.0f, (note + offsetSemitones - 69.0f) / 12.0f);
}
} // namespace

void SynthVoice::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Tutte e otto, non solo quelle che girano adesso: l'unison puo' salire a nota gia' viva e
    // una copia preparata a meta' produrrebbe un incremento di fase calcolato sul sample rate
    // sbagliato.
    for (auto& oscillator : oscillators_)
        oscillator.prepare (sampleRate_);

    filter_.prepare (sampleRate_);
    filterRight_.prepare (sampleRate_);
    envelope_.prepare (sampleRate_);
    envelope2_.prepare (sampleRate_);
    lfo_.prepare (sampleRate_);

    smoothedCutoff_.reset (sampleRate_, kSmoothingSeconds);
    smoothedFramePosition_.reset (sampleRate_, kSmoothingSeconds);
    smoothedLevel_.reset (sampleRate_, kSmoothingSeconds);
    smoothedPan_.reset (sampleRate_, kSmoothingSeconds);

    // Da 1 a kStealFadeFloor in kStealFadeSeconds: e' la stessa forma (e la stessa soglia di
    // silenzio a -80 dB) del release di dsp::ADSREnvelope. L'exp() sta qui e non in
    // beginStealFade() perche' quella viene chiamata dal thread audio, a note-on.
    stealFadeCoeff_ = (float) std::exp (std::log ((double) kStealFadeFloor)
                                        / ((double) kStealFadeSeconds * sampleRate_));

    reset();
}

void SynthVoice::reset() noexcept
{
    active_ = false;
    midiNote_ = -1;
    velocity_ = 0.0f;
    released_ = false;

    // Posizione ferma all'arrivo e nota interpolata a zero: una voce spenta non sta gliddando,
    // e il primo start() riscrivera' comunque tutti e quattro i numeri prima che qualcuno li
    // legga. L'uno e' l'identita' del glide come 1.0f lo e' della dissolvenza del furto.
    glideSourceNote_ = 0.0f;
    glideTargetNote_ = 0.0f;
    glideNote_ = 0.0f;
    glidePosition_ = 1.0f;

    // L'identita' moltiplicativa, non uno zero: e' questo che rende `x * fadeGain_` trasparente
    // sul percorso normale. startOrder_ invece non si tocca — vedi la sua dichiarazione.
    fading_ = false;
    fadeGain_ = 1.0f;
    fadeCoeff_ = 1.0f;

    for (auto& oscillator : oscillators_)
        oscillator.reset();

    filter_.reset();
    filterRight_.reset();
    envelope_.reset();
    envelope2_.reset();
}

void SynthVoice::beginStealFade() noexcept
{
    if (fading_)
        return; // gia' in dissolvenza: riarmarla la farebbe ripartire dal livello corrente

    fading_ = true;
    fadeCoeff_ = stealFadeCoeff_;
}

void SynthVoice::start (int midiNote, float velocity, unsigned long long startOrder) noexcept
{
    midiNote_ = midiNote;
    velocity_ = velocity;
    startOrder_ = startOrder;
    released_ = false;

    // Una nota nuova parte alla propria altezza: il glide di partenza, quando c'e', lo arma
    // VoiceManager subito dopo con glideFrom(), perche' e' li' che si sa quale fosse l'ultima
    // nota suonata — un dato fra voci, non di questa voce. Qui si azzera la coda di un glide
    // lasciato a meta' dalla nota che occupava questo slot del pool.
    glideSourceNote_ = (float) midiNote;
    glideTargetNote_ = (float) midiNote;
    glideNote_ = (float) midiNote;
    glidePosition_ = 1.0f;

    // Uno slot appena liberato da una dissolvenza e' gia' a posto (reset() l'ha rimessa a uno),
    // ma uno slot la cui *dissolvenza non era ancora finita quando l'inviluppo si e' spento*
    // no: li' fadeGain_ e' fermo a meta' strada. Senza queste due righe la nota nuova partirebbe
    // attenuata, e continuerebbe a scendere.
    fading_ = false;
    fadeGain_ = 1.0f;
    fadeCoeff_ = 1.0f;

    // envVel a 0 % = inviluppo sempre a piena ampiezza; a 100 % = proporzionale alla velocity.
    // L'inviluppo parte prima della modulazione perche' `env` e' una delle quattro sorgenti:
    // applyModulation() qui sotto deve leggerne il livello della nota nuova, non quello della
    // nota che occupava questo slot del pool. noteOn() non consuma campioni, quindi anticiparlo
    // non cambia di un campione il segnale renderizzato.
    const auto peak = 1.0f - velocityAmount_ * (1.0f - velocity);
    envelope_.noteOn (peak);

    // Il secondo inviluppo parte insieme, e sempre a picco pieno: e' una sorgente unipolare
    // 0..1, e la velocity e' gia' disponibile come sorgente a se'. Moltiplicarle qui vorrebbe
    // dire non poterle piu' separare in una route.
    envelope2_.noteOn (1.0f);

    // Con lfoRetrig la fase riparte dall'offset scelto: e' cio' che rende ripetibile un vibrato
    // che deve cominciare sempre allo stesso punto. Senza, la voce eredita la fase libera del
    // motore e due note identiche suonano diverse.
    if (lfoRetrig_)
        lfo_.retrigger (lfoPhaseOffset01_);

    // Prima di snappare gli smoothed value e di calcolare la frequenza: `vel` e `env` sono
    // sorgenti per voce, quindi una nota nuova deve partire gia' con i valori modulati che le
    // competono, non con quelli della nota precedente. Qui dentro finiscono anche
    // tuningSemitones_ e, con esso, frequencyHz_ e la frequenza dell'oscillatore.
    applyModulation();
    updateCutoff (true);

    // Le copie partono distribuite su i/N del ciclo. Senza, al note-on sono il medesimo
    // segnale sommato N volte: N volte piu' forte e senza un solo battimento finche' il detune
    // non le separa, cioe' proprio il contrario di cio' che serve.
    //
    // Con unison 1 la fase non si tocca, ed e' deliberato: oggi start() non resetta
    // l'oscillatore (lo fa solo kill()), quindi azzerarla qui cambierebbe il segnale di una
    // voce che riprende uno slot del pool. La non-regressione e' strutturale, non numerica.
    if (unisonVoices_ > 1)
        for (int i = 0; i < unisonVoices_; ++i)
            oscillators_[(size_t) i].resetToPhase ((float) i / (float) unisonVoices_);

    // Lo slot del pool puo' arrivare dalla nota precedente con i due integratori dell'SVF
    // ancora carichi: alla prima nota nuova quello stato viene reiniettato nel segnale, udibile
    // come un clic (piu' forte quanto piu' e' alta la risonanza). reset() non e' ridondante con
    // SynthVoice::reset(): quello gira solo su kill(), non a ogni note-on.
    filter_.reset();
    filterRight_.reset();

    // Una nota nuova parte subito ai valori correnti: niente rampa "ereditata" dalla
    // voce precedentemente occupata da questo slot del pool.
    smoothedFramePosition_.setCurrentAndTargetValue (smoothedFramePosition_.getTargetValue());
    smoothedLevel_.setCurrentAndTargetValue (smoothedLevel_.getTargetValue());
    smoothedPan_.setCurrentAndTargetValue (smoothedPan_.getTargetValue());

    active_ = true;
}

void SynthVoice::retrigger (float velocity) noexcept
{
    velocity_ = velocity;

    // Niente reset: ne' la fase dell'oscillatore ne' i due integratori dell'SVF vengono
    // azzerati, e l'inviluppo riparte dal livello a cui e' arrivato (ADSREnvelope::noteOn
    // non tocca level_). E' esattamente cio' che distingue una ribattuta da una nota nuova:
    // azzerare qualcosa qui porterebbe l'ampiezza a zero fra due campioni adiacenti, un
    // gradino di 0.24 a fondo scala misurato prima di questa funzione ("ribattere una nota
    // che suona gia' non produce un gradino" in EngineTests).
    //
    // Nessuno degli smoothed value va risincronizzato: la voce sta gia' girando, quindi i loro
    // valori correnti sono quelli giusti — al contrario di start(), che prende in carico uno
    // slot del pool arrivato da un'altra nota.
    //
    // Per la stessa ragione l'LFO non viene toccato: ne' la fase ne' la dissolvenza. Farla
    // ripartire qui produrrebbe un salto di profondita' della modulazione su una nota che sta
    // gia' suonando — lo stesso difetto di categoria del clic che retrigger() esiste per
    // evitare. La nuova velocity entra invece nella modulazione al prossimo render(), rampata
    // dagli smoothed value come qualunque altro cambio di parametro.
    const auto peak = 1.0f - velocityAmount_ * (1.0f - velocity);
    envelope_.noteOn (peak);
    envelope2_.noteOn (1.0f);
    released_ = false;
    active_ = true;
}

void SynthVoice::beginGlide (float fromNote) noexcept
{
    glideTargetNote_ = (float) midiNote_;

    // La distanza decide sia se il glide esista sia quanto duri: e' tutto il constant rate.
    const auto distance = std::abs (glideTargetNote_ - fromNote);

    // Due bypass, e sono lo stesso ramo per due ragioni diverse. `glide` a zero (il default)
    // spegne la funzione; una distanza nulla vuol dire che non c'e' niente da percorrere — e
    // senza questa meta' l'incremento sarebbe una divisione per zero travestita da infinito.
    if (glideSeconds_ <= kGlideMinSeconds || distance <= 0.0f)
    {
        glideSourceNote_ = glideTargetNote_;
        glideNote_ = glideTargetNote_;
        glidePosition_ = 1.0f;
        return;
    }

    glideSourceNote_ = fromNote;
    glideNote_ = fromNote;
    glidePosition_ = 0.0f;
}

void SynthVoice::advanceGlide (int numSamples) noexcept
{
    // Il caso normale, e quello che deve costare zero: nessun glide in corso. Un confronto fra
    // float e la funzione e' finita, e nessuna riga sotto ha toccato la frequenza.
    if (glidePosition_ >= 1.0f)
        return;

    if (glideSeconds_ <= kGlideMinSeconds)
    {
        // Il knob e' stato portato a zero a glide in corso: si arriva subito invece di
        // proseguire con il tempo di prima. Lo scatto d'intonazione e' cio' che l'utente ha
        // appena chiesto.
        glidePosition_ = 1.0f;
        glideNote_ = glideTargetNote_;
        return;
    }

    const auto distance = std::abs (glideTargetNote_ - glideSourceNote_);

    // La posizione avanza di numSamples / (T' * fs) con T' = glideSeconds_ * distanza / 12,
    // cioe' di numSamples * 12 / (glideSeconds_ * distanza * fs). E' la stessa ricorrenza di
    // Vital (portamento_slope.cpp), con in piu' il fattore di scala del constant rate.
    const auto step = (float) ((double) numSamples * 12.0
                               / ((double) glideSeconds_ * (double) distance * sampleRate_));

    glidePosition_ = juce::jmin (1.0f, glidePosition_ + step);

    // All'arrivo si prende il bersaglio esatto invece del risultato dell'interpolazione: a
    // posizione 1 `source + 1 * (target - source)` e' giusto in aritmetica reale ma puo'
    // arrivare a un ulp di distanza in float, e quell'ulp resterebbe li' per tutta la nota.
    glideNote_ = glidePosition_ >= 1.0f
                     ? glideTargetNote_
                     : glideSourceNote_ + glidePosition_ * (glideTargetNote_ - glideSourceNote_);
}

void SynthVoice::glideToNote (int midiNote, bool glide) noexcept
{
    // La sorgente e' dove la voce sta **adesso**, glide interrotto compreso: vedi il commento
    // della dichiarazione. Va letta prima di riscrivere midiNote_, perche' beginGlide() prende
    // il bersaglio da li'.
    //
    // Il salto si ottiene passando a beginGlide() la nota d'arrivo come partenza: distanza zero,
    // e il bypass che c'e' gia' fa il resto. Nessun secondo percorso da tenere allineato.
    const auto from = glide ? glideNote_ : (float) midiNote;

    midiNote_ = midiNote;
    beginGlide (from);
    updatePitch();

    // Il cutoff **non** si snappa: la voce sta suonando, e il keytracking deve seguire la stessa
    // rampa del glide invece di saltare alla nota d'arrivo. updateCutoff(false) gira comunque
    // alla prossima sotto-fetta, con la nota interpolata.
}

void SynthVoice::glideFrom (float sourceNote) noexcept
{
    beginGlide (sourceNote);
    updatePitch();

    // Qui invece si snappa, ed e' l'opposto del caso sopra per la stessa ragione: start() ha
    // appena portato lo smoother del cutoff sulla nota d'arrivo, e senza questa riga il
    // keytracking partirebbe da li' per tornare indietro alla nota di partenza del glide.
    updateCutoff (true);
}

void SynthVoice::updatePitch() noexcept
{
    if (midiNote_ < 0)
        return;

    frequencyHz_ = midiNoteToHz (glideNote_, tuningSemitones_);

    // Il rapporto e' gia' pronto: qui c'e' una moltiplicazione per copia, non un exp2.
    // Con unison 1 il fattore vale esattamente 1.0f, quindi la frequenza e' bit per bit
    // quella di prima.
    for (int i = 0; i < unisonVoices_; ++i)
        oscillators_[(size_t) i].setFrequencyHz (frequencyHz_ * detuneRatio_[(size_t) i]);
}

void SynthVoice::stop() noexcept
{
    envelope_.noteOff();
    envelope2_.noteOff();
    released_ = true;

    // `envelope_` e non `envelope2_`, e nemmeno i due insieme: la voce vive finche' vive
    // l'inviluppo d'**ampiezza**. Il secondo e' un modulatore, e un modulatore non decide
    // quando la nota finisce — con un release corto qui dentro taglierebbe la coda.
    active_ = envelope_.isActive();
}

void SynthVoice::kill() noexcept
{
    reset();
}

bool SynthVoice::isActive() const noexcept
{
    return active_ && envelope_.isActive();
}

int SynthVoice::getMidiNote() const noexcept
{
    return midiNote_;
}

void SynthVoice::setWavetable (const dsp::MipTable* table) noexcept
{
    // Tutte e otto anche qui: una copia che entra in servizio dopo un cambio di tavola
    // suonerebbe altrimenti con un puntatore nullo (silenzio) o con la tavola precedente.
    for (auto& oscillator : oscillators_)
    {
        oscillator.setTable (table); // resetta la posizione di frame a 0 internamente
        // Rimette subito la posizione corrente: senza questo, il cambio tavola resterebbe
        // silenziosamente sul frame 0 fino alla prossima render(), udibile come un salto.
        oscillator.setFramePosition (juce::jlimit (0.0f, 1.0f,
                                                  smoothedFramePosition_.getCurrentValue() + framePositionMod_));
    }
}

void SynthVoice::setParams (const EngineParams& p) noexcept
{
    // Copia per valore di una struct POD: nessuna allocazione, nessun puntatore seguito. Serve
    // perche' i sette target modulabili non si applicano piu' qui ma in applyModulation(), che
    // gira quando la voce sta per suonare.
    params_ = p;

    oscOn_ = p.oscOn;
    filterOn_ = p.filterOn;

    envelope_.setAttackSeconds (p.attackSeconds);
    envelope_.setDecaySeconds (p.decaySeconds);
    envelope_.setSustainLevel (p.sustain);
    envelope_.setReleaseSeconds (p.releaseSeconds);
    velocityAmount_ = p.velocityAmount;

    envelope2_.setAttackSeconds (p.attack2Seconds);
    envelope2_.setDecaySeconds (p.decay2Seconds);
    envelope2_.setSustainLevel (p.sustain2);
    envelope2_.setReleaseSeconds (p.release2Seconds);

    filter_.setType (p.filterType);
    filter_.setNumStages (p.filterStages);
    filterRight_.setType (p.filterType);
    filterRight_.setNumStages (p.filterStages);

    updateUnison (p.unisonVoices, p.detuneCents);

    // Key tracking: il cutoff segue la nota. Calcolato una volta per blocco, non per campione.
    keyTrack_ = p.keyTrack;

    // Il tempo del glide si copia e basta: non arma niente e non cambia un glide gia' in corso
    // se non attraverso advanceGlide(), che ricalcola l'incremento da qui a ogni sotto-fetta.
    // Il modo di voce invece non arriva alla voce: e' VoiceManager a deciderlo, perche' riguarda
    // *quale* voce prende una nota, non come una voce suona.
    glideSeconds_ = p.glideSeconds;

    // La maschera dei target che hanno almeno una route: si costruisce qui, una volta per
    // blocco, cosi' applyModulation() non deve riscorrere la lista per ognuno dei sette.
    modMask_ = 0;

    if (p.mods != nullptr)
        for (int i = 0; i < p.mods->count; ++i)
        {
            const auto target = p.mods->routes[i].targetIndex;

            if (target >= 0 && target < kNumModTargets)
                modMask_ |= 1u << (unsigned) target;
        }

    // La base denormalizzata del cutoff, una volta per blocco: vedi il commento del campo.
    // Fuori dal ramo modulato non serve a nessuno, e calcolarla costerebbe uno std::pow per
    // blocco e per voce a chi non ha nessuna route su cutoff.
    {
        const auto cutoffTarget = modTargetIndexFor (params::ParamSlot::cutoff);
        modBaseCutoffHz_ = isModulated (cutoffTarget)
                               ? params::cutoffHzFromRaw (p.modBase[(size_t) cutoffTarget])
                               : 0.0f;
    }

    globalLfoLevel_ = p.globalLfoLevel;
    lfoRetrig_ = p.lfoRetrig;
    lfoPhaseOffset01_ = p.lfoPhaseOffset01;

    constexpr auto* specLrate = params::find ("lrate");
    static_assert (specLrate != nullptr, "lrate non e' in ParameterTable.h");

    lfo_.setShape ((dsp::Lfo::Shape) juce::jlimit (0, 4, p.lfoShapeIndex));
    lfo_.setFadeSeconds (p.lfoFadeSeconds);
    lfo_.setFrequencyHz (p.lfoSync ? dsp::syncedRateHz (p.lfoRateRaw, (double) p.bpm)
                                   : params::denormalise (*specLrate, p.lfoRateRaw));
}

float SynthVoice::modulated (int targetIndex) const noexcept
{
    auto value = params_.modBase[(size_t) targetIndex];

    if (params_.mods != nullptr)
        for (int i = 0; i < params_.mods->count; ++i)
        {
            const auto& route = params_.mods->routes[i];

            if (route.targetIndex == targetIndex)
                value += route.depth * sourceLevels_[(size_t) route.src];
        }

    // Il clamp e' parte del contratto, non una precauzione: liveValue() in mod.ts clampa allo
    // stesso punto, e l'anello del knob nella UI mostra quel valore. Senza, suono e schermo
    // racconterebbero due storie diverse agli estremi della corsa.
    return juce::jlimit (0.0f, 1.0f, value);
}

float SynthVoice::getSourceLevel (ModSource src) const noexcept
{
    switch (src)
    {
        case ModSource::lfo:   return lfoRetrig_ ? lfo_.level() : globalLfoLevel_;
        case ModSource::env:   return envelope_.getLevel();
        case ModSource::env2:  return envelope2_.getLevel();
        case ModSource::vel:   return velocity_;
        case ModSource::mw:    return params_.modWheel;
        case ModSource::count:
        default:               return 0.0f;
    }
}

void SynthVoice::applyModulation() noexcept
{
    // I livelli delle cinque sorgenti si calcolano una volta sola, prima di applicarli: env,
    // env2 e vel sono per voce (due note tenute stanno a punti diversi del loro inviluppo), mw
    // e' globale, lfo dipende da lfoRetrig. L'argomento e' costante in ognuna di queste righe,
    // quindi getSourceLevel si riduce alla sola espressione che c'era prima: la chiamata e'
    // li' per avere una definizione sola, condivisa con il meter, non per fare un giro in piu'.
    sourceLevels_[(size_t) ModSource::lfo] = getSourceLevel (ModSource::lfo);
    sourceLevels_[(size_t) ModSource::env] = getSourceLevel (ModSource::env);
    sourceLevels_[(size_t) ModSource::env2] = getSourceLevel (ModSource::env2);
    sourceLevels_[(size_t) ModSource::vel] = getSourceLevel (ModSource::vel);
    sourceLevels_[(size_t) ModSource::mw] = getSourceLevel (ModSource::mw);

    // `convert` e' la stessa identica funzione che collectEngineParams usa per quel target:
    // e' il requisito che impedisce a un cutoff mosso da un LFO e a uno mosso a mano di
    // finire in due posti diversi. Senza route su quel target si usa direttamente il valore
    // gia' denormalizzato dal processore, che e' per costruzione lo stesso numero.
    const auto value = [this] (params::ParamSlot slot, float unmodulated,
                               float (*convert) (float) noexcept) noexcept
    {
        const auto target = modTargetIndexFor (slot);
        return isModulated (target) ? convert (modulated (target)) : unmodulated;
    };

    driveGain_ = value (params::ParamSlot::drive, params_.driveGain, &params::driveGainFromRaw);

    // La dissolvenza del primo tratto di `drive`: vedi kDriveFadeGain e driveMix_. Si calcola
    // qui, una volta per sotto-fetta di controllo, non per campione — e si calcola sul
    // **guadagno** invece che sui decibel apposta, per non chiamare un logaritmo: su due soli
    // decibel di corsa la differenza fra una rampa lineare in gain e una lineare in dB vale
    // meno di 0.03 dB sul risultato, cioe' niente, e il ramo resta senza libm come il resto.
    driveMix_ = driveGain_ > 1.0f
                   ? juce::jmin (1.0f, (driveGain_ - 1.0f) / (kDriveFadeGain - 1.0f))
                   : 0.0f;

    const auto resonance = value (params::ParamSlot::res, params_.resonanceQ, &params::resonanceQFromRaw);
    filter_.setResonance (resonance);
    filterRight_.setResonance (resonance);

    // I quattro bersagli rampati si dividono in due: sullo smoother finisce la sola **base**, e
    // la modulazione torna di qui come scostamento, da sommare a valle in render(). Vedi il
    // commento degli smoother in SynthVoice.h: la rampa di 20 ms e' un polo a 8 Hz, e applicarla
    // al totale attenuava di 8.6 dB una route a 20 Hz.
    //
    // Nota quale base finisce nello smoother quando il bersaglio e' modulato: `convert(modBase)`
    // e non il campo denormalizzato di EngineParams. In produzione sono lo stesso numero
    // (collectEngineParams li riempie dallo stesso grezzo), ma il percorso modulato ha sempre
    // letto modBase, e continuare a leggerlo e' cio' che tiene lo scostamento e la base nella
    // stessa scala anche quando un test costruisce a mano una EngineParams in cui i due campi
    // non si corrispondono.
    const auto rampedBase = [this] (params::ParamSlot slot, float unmodulated,
                                    float (*convert) (float) noexcept,
                                    juce::SmoothedValue<float>& smoother) noexcept
    {
        const auto target = modTargetIndexFor (slot);

        if (! isModulated (target))
        {
            smoother.setTargetValue (unmodulated);
            return 0.0f;
        }

        const auto base = convert (params_.modBase[(size_t) target]);
        smoother.setTargetValue (base);
        return convert (modulated (target)) - base;
    };

    framePositionMod_ = rampedBase (params::ParamSlot::wtpos, params_.framePosition,
                                    &params::framePositionFromRaw, smoothedFramePosition_);
    levelMod_ = rampedBase (params::ParamSlot::level, params_.level,
                            &params::levelGainFromRaw, smoothedLevel_);
    panMod_ = rampedBase (params::ParamSlot::pan, params_.pan, &params::panFromRaw, smoothedPan_);

    // Cutoff a parte, e moltiplicativo: la mappa e' logaritmica, quindi la modulazione e' un
    // rapporto di frequenza — la stessa profondita' vale 632 Hz a meta' corsa e 12 kHz vicino al
    // fondo scala, e uno scostamento additivo salterebbe a ogni movimento della base. Il
    // rapporto no: e' 1000^(scostamento normalizzato), indipendente da dove sta la base, quindi
    // la rampa moltiplicata per una costante resta una rampa.
    const auto cutoffTarget = modTargetIndexFor (params::ParamSlot::cutoff);

    if (isModulated (cutoffTarget))
    {
        baseCutoffHz_ = modBaseCutoffHz_;
        const auto modulatedHz = params::cutoffHzFromRaw (modulated (cutoffTarget));

        // Il minimo della mappa e' 20 Hz, quindi il denominatore non e' mai zero; la guardia
        // c'e' lo stesso perche' il costo e' un confronto e il prezzo di sbagliarsi un NaN che
        // si propaga fino all'uscita.
        cutoffModRatio_ = baseCutoffHz_ > 0.0f ? modulatedHz / baseCutoffHz_ : 1.0f;
    }
    else
    {
        baseCutoffHz_ = params_.cutoffHz;
        cutoffModRatio_ = 1.0f;
    }

    const auto fineCents = value (params::ParamSlot::fine, params_.fineCents, &params::fineCentsFromRaw);

    // Il bend e' un addendo dell'offset in semitoni, come oct, semi e fine: entra dalla stessa
    // porta, si compone con il glide per costruzione (il glide muove il numero di nota, questo
    // muove l'offset, updatePitch() li somma) e con `pitchBend` a zero somma 0.0f, quindi chi
    // non tocca la rotella ottiene gli stessi bit di prima.
    const auto bendSemitones = params_.pitchBend * (float) params_.pitchBendRangeSemitones;
    tuningSemitones_ = (float) (12 * params_.octave + params_.semitones) + fineCents * 0.01f + bendSemitones;

    // Il vibrato ha senso solo se l'intonazione segue la modulazione anche a nota gia' partita:
    // senza questo, una route su `fine` cambierebbe solo l'accordatura delle note successive.
    //
    // Glide e modulazione di `fine` si **compongono** invece di escludersi, ed e' una proprieta'
    // di come sono scritti: il glide muove il numero di nota, `fine` muove tuningSemitones_, e
    // updatePitch() li somma. Un vibrato su una nota che sta gliddando oscilla attorno al punto
    // in cui il glide e' arrivato, che e' cio' che fa un dito su una tastata.
    updatePitch();
}

void SynthVoice::updateUnison (int voices, float detuneCents) noexcept
{
    voices = juce::jlimit (1, kMaxUnison, voices);

    // exp2() e sqrt() girano solo quando uno dei due knob si muove davvero: a parametri fermi
    // questa funzione e' due confronti per blocco e per voce.
    // exactlyEqual e non un confronto con tolleranza: qui non si cerca "quasi lo stesso
    // valore", si cerca "il knob non si e' mosso", e la risposta giusta a una differenza di un
    // ulp e' ricalcolare i rapporti, non ignorarla.
    if (voices == unisonVoices_ && juce::exactlyEqual (detuneCents, detuneCents_))
        return;

    const auto previous = unisonVoices_;
    unisonVoices_ = voices;
    detuneCents_ = detuneCents;

    // sqrt(1) = 1 esatto: la compensazione non tocca il percorso a copia singola.
    unisonGain_ = 1.0f / std::sqrt ((float) voices);

    for (int i = 0; i < voices; ++i)
        detuneRatio_[(size_t) i] = std::exp2 (detuneCents * unisonSpread (i, voices) / 1200.0f);

    // Le copie che prima non giravano entrano in servizio adesso e la loro fase e' ferma dove
    // l'aveva lasciata un'altra nota: si distribuiscono. Quelle gia' attive non si toccano —
    // riazzerarne la fase sarebbe un gradino su un segnale che sta suonando.
    for (int i = previous; i < voices; ++i)
        oscillators_[(size_t) i].resetToPhase ((float) i / (float) voices);
}

void SynthVoice::updateCutoff (bool snap) noexcept
{
    // A keyTrack 1 il cutoff raddoppia per ottava sopra il DO centrale.
    //
    // La nota e' quella **suonata**, glide compreso: durante un glide il filtro sale insieme
    // all'intonazione invece di essere gia' arrivato. Fuori da un glide glideNote_ e'
    // esattamente (float) midiNote_ e la sottrazione da' lo stesso identico float di prima —
    // i piccoli interi sono esatti in binario — quindi il percorso senza glide non cambia.
    const auto offsetSemitones = keyTrack_ * (glideNote_ - 60.0f);
    const auto target = baseCutoffHz_ * std::exp2 (offsetSemitones / 12.0f);

    if (snap)
        smoothedCutoff_.setCurrentAndTargetValue (target);
    else
        smoothedCutoff_.setTargetValue (target);
}

void SynthVoice::render (float* outL, float* outR, int numSamples) noexcept
{
    if (! isActive() || outL == nullptr || outR == nullptr || numSamples <= 0)
        return;

    if (lfoRetrig_)
        lfo_.advance (numSamples);

    // Il glide avanza **prima** di applyModulation(), non dopo: quella legge glideNote_ per
    // calcolare la frequenza, e leggerlo dopo vorrebbe dire suonare ogni sotto-fetta con la
    // nota della sotto-fetta precedente. Come per l'LFO, il passo e' in campioni: il tasso e'
    // quello delle sotto-fette di controllo (32 campioni, 1500 Hz a 48 kHz), non quello del
    // buffer che passa l'host, quindi lo stesso glide dura lo stesso a qualunque dimensione di
    // blocco. Senza glide in corso e' un solo confronto.
    advanceGlide (numSamples);

    // La modulazione si valuta qui e non in setParams() per una ragione sola: `env` e `vel`
    // sono sorgenti per voce e cambiano *dentro* il blocco (un note-on arriva a meta' buffer),
    // quindi vanno lette quando la voce sta per suonare, non quando il processore deposita i
    // parametri. Resta un sistema a tasso di controllo, una valutazione per
    // sotto-fetta: SynthEngine::process spezza il render in fette di al piu'
    // kControlBlockSamples (32) campioni, quindi 1500 Hz a 48 kHz **garantiti**, non
    // piu' dipendenti dal buffer che passa l'host, abbondante per un LFO che
    // arriva a 20 Hz, inadatto a FM e AM — che non sono un obiettivo.
    applyModulation();
    updateCutoff (false);

    // Cutoff e posizione si aggiornano una volta per blocco: ricalcolano tan() e gli
    // indici di frame, troppo costosi per girare per campione.
    //
    // Rampa per la base, prodotto per la modulazione: `* 1.0f` e `+ 0.0f` sui bersagli senza
    // route sono l'identita' esatta, quindi il percorso non modulato resta bit per bit quello
    // di prima. Il cutoff che ne esce puo' uscire dal range della mappa mentre la base rampa;
    // ci pensa StateVariableFilter, che limita comunque a [10 Hz, 0.49 x sr] prima del prewarp.
    const auto cutoffNow = smoothedCutoff_.skip (numSamples) * cutoffModRatio_;
    filter_.setCutoffHz (cutoffNow);
    filterRight_.setCutoffHz (cutoffNow);

    const auto positionNow = juce::jlimit (0.0f, 1.0f,
                                           smoothedFramePosition_.skip (numSamples) + framePositionMod_);
    for (int i = 0; i < unisonVoices_; ++i)
        oscillators_[(size_t) i].setFramePosition (positionNow);

    // Pan a potenza costante: se seguisse la rampa campione per campione userebbe
    // cos()/sin() per campione, vietato. Il guadagno si ricalcola quindi una sola
    // volta per blocco dal valore rampato; solo Level resta rampato per campione,
    // perché getNextValue() è una semplice interpolazione lineare, senza libm.
    //
    // Con l'unison diventano N coppie invece di una — le copie si distribuiscono attorno al pan
    // della voce su kUnisonSpreadWidth — ma restano N calcoli *per blocco*, non per campione.
    const auto panNow = smoothedPan_.skip (numSamples) + panMod_;

    for (int i = 0; i < unisonVoices_; ++i)
    {
        const auto position = juce::jlimit (-1.0f, 1.0f,
                                            panNow + kUnisonSpreadWidth * unisonSpread (i, unisonVoices_));
        const auto angle = (position * 0.5f + 0.5f) * 1.5707963f;

        // Con unison 1 lo spread vale 0, jlimit non morde (il pan e' gia' in -1..1),
        // unisonGain_ vale 1.0f: le due righe danno gli stessi bit di prima.
        unisonGainL_[(size_t) i] = std::cos (angle) * kVoiceHeadroomGain * unisonGain_;
        unisonGainR_[(size_t) i] = std::sin (angle) * kVoiceHeadroomGain * unisonGain_;
    }

    // Level e' l'unico bersaglio rampato per campione, quindi la somma con la modulazione sta
    // dentro il ciclo: una addizione, e nient'altro. Il limite a 0..1 si applica **allo
    // scostamento**, qui, una volta per sotto-fetta invece che a ogni campione: dentro la fetta
    // la rampa sta tutta fra il valore corrente e il bersaglio, quindi tenere lo scostamento
    // dentro quel margine garantisce 0..1 su ogni campione.
    //
    // Il limite serve davvero, e non e' una precauzione di troppo: lo scostamento si ricalcola
    // dalla base *nuova* mentre la rampa e' ancora sulla vecchia, e con un depth negativo
    // profondo quella sfasatura porterebbe il prodotto sotto zero — cioe' invertirebbe la
    // polarita' della voce invece di abbassarla. Sta qui e non in applyModulation() perche'
    // start() risincronizza lo smoother *dopo* di quella: leggerlo prima darebbe un margine
    // calcolato sul livello lasciato dalla nota precedente.
    //
    // Senza route lo scostamento e' zero, i due estremi stanno gia' in 0..1 (levelGainFromRaw
    // clampa), quindi -lo <= 0 <= 1-hi: il limite lo lascia a zero e `+ 0.0f` e' l'identita'
    // esatta.
    const auto levelLow = juce::jmin (smoothedLevel_.getCurrentValue(), smoothedLevel_.getTargetValue());
    const auto levelHigh = juce::jmax (smoothedLevel_.getCurrentValue(), smoothedLevel_.getTargetValue());
    const auto levelMod = juce::jlimit (-levelLow, 1.0f - levelHigh, levelMod_);
    const auto levelAt = [levelMod] (float base) noexcept { return base + levelMod; };

    if (unisonVoices_ == 1)
    {
        // Il percorso di prima dell'unison, riga per riga: mono fino al pan finale, un filtro
        // solo. Non e' una duplicazione da unificare — e' cio' che rende la non-regressione una
        // proprieta' strutturale invece di una coincidenza fra due formule che devono
        // combaciare, lo stesso argomento di modMask_ per la modulazione.
        const auto gainL = unisonGainL_[0];
        const auto gainR = unisonGainR_[0];

        for (int i = 0; i < numSamples; ++i)
        {
            float sample = oscOn_ ? oscillators_[0].getSample() : 0.0f;

            // Tre rami, tutti invarianti dentro la fetta: a drive spento non si tocca niente
            // (l'identita' bit per bit di prima), oltre kDriveFadeGain si satura e basta, e in
            // mezzo si miscela. Il ramo centrale esiste solo su due decibel di corsa.
            if (driveMix_ >= 1.0f)
                sample = saturate (sample * driveGain_);
            else if (driveMix_ > 0.0f)
                sample += driveMix_ * (saturate (sample * driveGain_) - sample);

            if (filterOn_)
                sample = filter_.processSample (sample);

            // La dissolvenza del furto moltiplica l'inviluppo. A riposo fadeGain_ vale
            // esattamente 1.0f e fadeCoeff_ pure, quindi le due righe sono l'identita' bit per
            // bit e questo ciclo resta quello di prima — lo stesso argomento di modMask_.
            sample *= envelope_.getNextSample() * fadeGain_;
            fadeGain_ *= fadeCoeff_;
            sample *= levelAt (smoothedLevel_.getNextValue());

            outL[i] += sample * gainL;
            outR[i] += sample * gainR;
        }
    }
    else
    {
        // La saturazione si applica a ogni copia *prima* del pan, non alla somma: e' l'unico
        // stadio non lineare della catena, e metterlo dopo la miscela stereo farebbe distorcere
        // una copia in modo diverso a seconda di dove sta panpottata. Cosi' invece ogni copia
        // suona come suonerebbe da sola, e poi si colloca nel campo.
        for (int i = 0; i < numSamples; ++i)
        {
            float left = 0.0f;
            float right = 0.0f;

            for (int u = 0; u < unisonVoices_; ++u)
            {
                float sample = oscOn_ ? oscillators_[(size_t) u].getSample() : 0.0f;

                // Gli stessi tre rami del percorso a unison 1, con la stessa miscela: una copia
                // dell'unison deve saturare come saturerebbe la voce da sola.
                if (driveMix_ >= 1.0f)
                    sample = saturate (sample * driveGain_);
                else if (driveMix_ > 0.0f)
                    sample += driveMix_ * (saturate (sample * driveGain_) - sample);

                left += sample * unisonGainL_[(size_t) u];
                right += sample * unisonGainR_[(size_t) u];
            }

            if (filterOn_)
            {
                left = filter_.processSample (left);
                right = filterRight_.processSample (right);
            }

            // Inviluppo e livello sono per voce, non per copia: si leggono una volta sola e si
            // applicano ai due canali. getNextSample()/getNextValue() avanzano uno stato, non
            // sono funzioni pure — chiamarle due volte farebbe correre l'inviluppo al doppio.
            const auto amplitude = envelope_.getNextSample() * fadeGain_
                                 * levelAt (smoothedLevel_.getNextValue());
            fadeGain_ *= fadeCoeff_;

            outL[i] += left * amplitude;
            outR[i] += right * amplitude;
        }
    }

    // Il secondo inviluppo avanza dello stesso numero di campioni, ma fuori dai due cicli di
    // rendering: la sua uscita non moltiplica niente, quindi non ha niente da fare li' dentro, e
    // tenerla fuori lascia quei cicli identici riga per riga a prima (lo stesso argomento di
    // modMask_). Il livello che le route leggono e' quello di *inizio* fetta, letto da
    // applyModulation() poco sopra — esattamente come per l'inviluppo d'ampiezza, che
    // getLevel() campiona prima di avanzare.
    //
    // Gira sempre, anche senza nessuna route su env2: altrimenti collegarne una a nota gia'
    // partita leggerebbe un inviluppo fermo dove l'aveva lasciato l'ultima route.
    for (int i = 0; i < numSamples; ++i)
        envelope2_.getNextSample();

    // La dissolvenza del furto e' arrivata a -80 dB: qui azzerare non e' un gradino ma un
    // arrotondamento, e lo slot torna disponibile. Il controllo sta a fine fetta e non per
    // campione perche' le fette valgono al piu' kControlBlockSamples (32): si sfora di meno di
    // un millisecondo, su un segnale che a quel punto sta gia' sotto il pavimento a 16 bit.
    if (fading_ && fadeGain_ <= kStealFadeFloor)
    {
        reset();
        return;
    }

    // L'inviluppo puo' spegnersi *prima* che la dissolvenza sia finita — una voce rubata mentre
    // era gia' in release corto. La voce e' comunque silenziosa: si libera lo slot, e `fading_`
    // torna falso perche' non c'e' piu' niente da dissolvere.
    if (! envelope_.isActive())
    {
        active_ = false;
        fading_ = false;
    }
}
} // namespace engine
