#include "engine/SynthVoice.h"

#include "parameters/ParamCollect.h"

#include <algorithm>
#include <cmath>

namespace engine
{
namespace
{
constexpr double kSmoothingSeconds = 0.02;

// Headroom fisso per voce. E' una costante, non un divisore sul numero di voci attive: un
// divisore farebbe "respirare" il volume ogni volta che una nota parte o finisce, un difetto
// peggiore del clipping che risolve. -8 dB: con il pan centrale (0.707), il volume di default
// (0.8) e la compensazione di risonanza in StateVariableFilter, una nota singola a level 1.0
// esce a -13.8 dBFS e un accordo di quattro note a -6.0 dBFS (misurati da EngineTests,
// "gain staging: una nota e un accordo normale stanno sotto il soft clipper"). Il valore precedente
// (-20 dB) era stato scelto quando il filtro poteva da solo aggiungere +30 dB di picco e la
// saturazione ne aggiungeva altri 3.5 anche a drive zero: tolte quelle due sorgenti di
// guadagno incontrollato, -20 dB lasciava lo strumento inutilizzabilmente piano (-26 dBFS
// su una nota singola). Cio' che resta oltre il fondo scala lo prende il soft clipper di
// SynthEngine::process, quindi salire qui non puo' produrre clipping digitale netto.
constexpr float kVoiceHeadroomGain = 0.4f; // 10^(-8/20)

float midiNoteToHz (int note, float offsetSemitones) noexcept
{
    // std::pow gira solo a note-on, mai per campione: niente libm nel loop audio.
    return 440.0f * std::pow (2.0f, ((float) note + offsetSemitones - 69.0f) / 12.0f);
}

/**
 * Soft clipper polinomiale: guadagno unitario sul piccolo segnale, satura dolcemente e si
 * ferma a 1.0 quando l'ingresso raggiunge 1.5. Niente tanh() per campione.
 *
 * La versione precedente, `1.5 * (x - x^3/3)`, era unitaria a fondo scala ma aveva pendenza
 * **1.5 nell'origine**: applicava +3.5 dB a tutto il segnale anche a drive minimo. Insieme al
 * default di `drive` (che era 0.15, cioe' 3.6 dB) significava +7 dB non richiesti sulla catena.
 */
float saturate (float x) noexcept
{
    // y = c - c^3/6.75 -> y'(0) = 1, y(1.5) = 1, y'(1.5) = 0.
    const auto c = std::clamp (x, -1.5f, 1.5f);
    return c - (c * c * c) / 6.75f;
}
} // namespace

void SynthVoice::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    oscillator_.prepare (sampleRate_);
    filter_.prepare (sampleRate_);
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
    oscillator_.reset();
    filter_.reset();
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

    // Lo slot del pool puo' arrivare dalla nota precedente con i due integratori dell'SVF
    // ancora carichi: alla prima nota nuova quello stato viene reiniettato nel segnale, udibile
    // come un clic (piu' forte quanto piu' e' alta la risonanza). reset() non e' ridondante con
    // SynthVoice::reset(): quello gira solo su kill(), non a ogni note-on.
    filter_.reset();

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
    oscillator_.setTable (table); // resetta la posizione di frame a 0 internamente
    // Rimette subito la posizione corrente: senza questo, il cambio tavola resterebbe
    // silenziosamente sul frame 0 fino alla prossima render(), udibile come un salto.
    oscillator_.setFramePosition (smoothedFramePosition_.getCurrentValue());
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
    filter_.setResonance (value (params::ParamSlot::res, params_.resonanceQ, &params::resonanceQFromRaw));

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
        oscillator_.setFrequencyHz (frequencyHz_);
    }
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
    filter_.setCutoffHz (smoothedCutoff_.skip (numSamples));
    oscillator_.setFramePosition (smoothedFramePosition_.skip (numSamples));

    // Pan a potenza costante: se seguisse la rampa campione per campione userebbe
    // cos()/sin() per campione, vietato. Il guadagno si ricalcola quindi una sola
    // volta per blocco dal valore rampato; solo Level resta rampato per campione,
    // perché getNextValue() è una semplice interpolazione lineare, senza libm.
    const auto panNow = smoothedPan_.skip (numSamples);
    const auto angle = (panNow * 0.5f + 0.5f) * 1.5707963f;
    const auto gainL = std::cos (angle) * kVoiceHeadroomGain;
    const auto gainR = std::sin (angle) * kVoiceHeadroomGain;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = oscOn_ ? oscillator_.getSample() : 0.0f;

        if (driveGain_ > 1.0f)
            sample = saturate (sample * driveGain_);

        if (filterOn_)
            sample = filter_.processSample (sample);

        sample *= envelope_.getNextSample();
        sample *= smoothedLevel_.getNextValue();

        outL[i] += sample * gainL;
        outR[i] += sample * gainR;
    }

    if (! envelope_.isActive())
        active_ = false;
}
} // namespace engine
