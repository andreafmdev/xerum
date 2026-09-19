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
// -3 dB, e non e' un numero scelto a tavolino: e' il piu' alto che tiene un accordo *ordinario*
// fuori dal soft clipper. Con il pan centrale (0.707) e il volume di default (0.8), quattro note
// a level 1.0 arrivano al clipper a 0.898 contro una soglia di 0.95 (kSoftClipThreshold in
// SynthEngine.cpp): mezzo decibel di margine, e mezzo decibel piu' su la rete di sicurezza
// diventerebbe uno stadio sempre acceso. In cambio una nota singola a level 1.0 e volume 1.0
// esce a -15.4 dBFS RMS e -8.7 dBFS di picco, cinque decibel piu' forte di prima e nella
// finestra in cui stanno Serum e Vital.
//
// Il valore precedente (0.4, -8 dB) lasciava quella stessa nota a -20.4 dBFS RMS: uno strumento
// che bisognava alzare di sei decibel nel mixer prima di poterlo giudicare. E prima ancora era
// -20 dB, scelto quando il filtro poteva da solo aggiungere +30 dB di picco e la saturazione ne
// aggiungeva altri 3.5 anche a drive zero.
//
// Piu' in alto non si va, e la ragione non e' prudenza ma aritmetica: il fattore di cresta e'
// fisso (6.7 dB fra RMS e picco su una nota, altri 9.7 dB di picco sommando quattro note), quindi
// un accordo di quattro note sta sempre 16.4 dB sopra l'RMS di una nota sola. Portare la nota
// singola a -14 dBFS RMS metterebbe l'accordo a +2.4 dBFS, cioe' dentro il clipper: i due
// bersagli non stanno insieme, e qui vince l'accordo pulito. Misure in EngineTests,
// "gain staging: una nota e un accordo normale stanno sotto il soft clipper".
constexpr float kVoiceHeadroomGain = 0.71f; // 10^(-3/20)

float midiNoteToHz (int note, float offsetSemitones) noexcept
{
    // std::pow gira solo a note-on, mai per campione: niente libm nel loop audio.
    return 440.0f * std::pow (2.0f, ((float) note + offsetSemitones - 69.0f) / 12.0f);
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
    lfo_.prepare (sampleRate_);

    smoothedCutoff_.reset (sampleRate_, kSmoothingSeconds);
    smoothedFramePosition_.reset (sampleRate_, kSmoothingSeconds);
    smoothedLevel_.reset (sampleRate_, kSmoothingSeconds);
    smoothedPan_.reset (sampleRate_, kSmoothingSeconds);

    reset();
}

void SynthVoice::reset() noexcept
{
    active_ = false;
    midiNote_ = -1;
    velocity_ = 0.0f;

    for (auto& oscillator : oscillators_)
        oscillator.reset();

    filter_.reset();
    filterRight_.reset();
    envelope_.reset();
}

void SynthVoice::start (int midiNote, float velocity) noexcept
{
    midiNote_ = midiNote;
    velocity_ = velocity;

    // envVel a 0 % = inviluppo sempre a piena ampiezza; a 100 % = proporzionale alla velocity.
    // L'inviluppo parte prima della modulazione perche' `env` e' una delle quattro sorgenti:
    // applyModulation() qui sotto deve leggerne il livello della nota nuova, non quello della
    // nota che occupava questo slot del pool. noteOn() non consuma campioni, quindi anticiparlo
    // non cambia di un campione il segnale renderizzato.
    const auto peak = 1.0f - velocityAmount_ * (1.0f - velocity);
    envelope_.noteOn (peak);

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
    active_ = true;
}

void SynthVoice::stop() noexcept
{
    envelope_.noteOff();
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
        oscillator.setFramePosition (smoothedFramePosition_.getCurrentValue());
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

    filter_.setType (p.filterType);
    filter_.setNumStages (p.filterStages);
    filterRight_.setType (p.filterType);
    filterRight_.setNumStages (p.filterStages);

    updateUnison (p.unisonVoices, p.detuneCents);

    // Key tracking: il cutoff segue la nota. Calcolato una volta per blocco, non per campione.
    keyTrack_ = p.keyTrack;

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

void SynthVoice::applyModulation() noexcept
{
    // I livelli delle quattro sorgenti si calcolano una volta sola, prima di applicarli: env e
    // vel sono per voce (due note tenute stanno a punti diversi del loro inviluppo), mw e'
    // globale, lfo dipende da lfoRetrig.
    sourceLevels_[(size_t) ModSource::lfo] = lfoRetrig_ ? lfo_.level() : globalLfoLevel_;
    sourceLevels_[(size_t) ModSource::env] = envelope_.getLevel();
    sourceLevels_[(size_t) ModSource::vel] = velocity_;
    sourceLevels_[(size_t) ModSource::mw] = params_.modWheel;

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

    baseCutoffHz_ = value (params::ParamSlot::cutoff, params_.cutoffHz, &params::cutoffHzFromRaw);
    driveGain_ = value (params::ParamSlot::drive, params_.driveGain, &params::driveGainFromRaw);
    const auto resonance = value (params::ParamSlot::res, params_.resonanceQ, &params::resonanceQFromRaw);
    filter_.setResonance (resonance);
    filterRight_.setResonance (resonance);

    smoothedFramePosition_.setTargetValue (
        value (params::ParamSlot::wtpos, params_.framePosition, &params::framePositionFromRaw));
    smoothedLevel_.setTargetValue (value (params::ParamSlot::level, params_.level, &params::levelGainFromRaw));
    smoothedPan_.setTargetValue (value (params::ParamSlot::pan, params_.pan, &params::panFromRaw));

    const auto fineCents = value (params::ParamSlot::fine, params_.fineCents, &params::fineCentsFromRaw);
    tuningSemitones_ = (float) (12 * params_.octave + params_.semitones) + fineCents * 0.01f;

    // Il vibrato ha senso solo se l'intonazione segue la modulazione anche a nota gia' partita:
    // senza questo, una route su `fine` cambierebbe solo l'accordatura delle note successive.
    if (midiNote_ >= 0)
    {
        frequencyHz_ = midiNoteToHz (midiNote_, tuningSemitones_);

        // Il rapporto e' gia' pronto: qui c'e' una moltiplicazione per copia, non un exp2.
        // Con unison 1 il fattore vale esattamente 1.0f, quindi la frequenza e' bit per bit
        // quella di prima.
        for (int i = 0; i < unisonVoices_; ++i)
            oscillators_[(size_t) i].setFrequencyHz (frequencyHz_ * detuneRatio_[(size_t) i]);
    }
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
    const auto offsetSemitones = keyTrack_ * (float) (midiNote_ - 60);
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

    // La modulazione si valuta qui e non in setParams() per una ragione sola: `env` e `vel`
    // sono sorgenti per voce e cambiano *dentro* il blocco (un note-on arriva a meta' buffer),
    // quindi vanno lette quando la voce sta per suonare, non quando il processore deposita i
    // parametri. Resta un sistema a tasso di controllo, una valutazione per blocco (o per fetta
    // fra due eventi MIDI): 375 Hz a 48 kHz con blocchi da 128, abbondante per un LFO che
    // arriva a 20 Hz, inadatto a FM e AM — che non sono un obiettivo.
    applyModulation();
    updateCutoff (false);

    // Cutoff e posizione si aggiornano una volta per blocco: ricalcolano tan() e gli
    // indici di frame, troppo costosi per girare per campione.
    const auto cutoffNow = smoothedCutoff_.skip (numSamples);
    filter_.setCutoffHz (cutoffNow);
    filterRight_.setCutoffHz (cutoffNow);

    const auto positionNow = smoothedFramePosition_.skip (numSamples);
    for (int i = 0; i < unisonVoices_; ++i)
        oscillators_[(size_t) i].setFramePosition (positionNow);

    // Pan a potenza costante: se seguisse la rampa campione per campione userebbe
    // cos()/sin() per campione, vietato. Il guadagno si ricalcola quindi una sola
    // volta per blocco dal valore rampato; solo Level resta rampato per campione,
    // perché getNextValue() è una semplice interpolazione lineare, senza libm.
    //
    // Con l'unison diventano N coppie invece di una — le copie si distribuiscono attorno al pan
    // della voce su kUnisonSpreadWidth — ma restano N calcoli *per blocco*, non per campione.
    const auto panNow = smoothedPan_.skip (numSamples);

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

            if (driveGain_ > 1.0f)
                sample = saturate (sample * driveGain_);

            if (filterOn_)
                sample = filter_.processSample (sample);

            sample *= envelope_.getNextSample();
            sample *= smoothedLevel_.getNextValue();

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

                if (driveGain_ > 1.0f)
                    sample = saturate (sample * driveGain_);

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
            const auto amplitude = envelope_.getNextSample() * smoothedLevel_.getNextValue();

            outL[i] += left * amplitude;
            outR[i] += right * amplitude;
        }
    }

    if (! envelope_.isActive())
        active_ = false;
}
} // namespace engine
