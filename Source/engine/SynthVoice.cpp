#include "engine/SynthVoice.h"

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
    frequencyHz_ = midiNoteToHz (midiNote, tuningSemitones_);
    oscillator_.setFrequencyHz (frequencyHz_);
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

    // envVel a 0 % = inviluppo sempre a piena ampiezza; a 100 % = proporzionale alla velocity.
    const auto peak = 1.0f - velocityAmount_ * (1.0f - velocity);
    envelope_.noteOn (peak);
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
    oscOn_ = p.oscOn;
    filterOn_ = p.filterOn;
    driveGain_ = p.driveGain;
    tuningSemitones_ = (float) (12 * p.octave + p.semitones) + p.fineCents * 0.01f;

    smoothedFramePosition_.setTargetValue (p.framePosition);
    smoothedLevel_.setTargetValue (p.level);
    smoothedPan_.setTargetValue (p.pan);

    envelope_.setAttackSeconds (p.attackSeconds);
    envelope_.setDecaySeconds (p.decaySeconds);
    envelope_.setSustainLevel (p.sustain);
    envelope_.setReleaseSeconds (p.releaseSeconds);
    velocityAmount_ = p.velocityAmount;

    filter_.setType (p.filterType);
    filter_.setNumStages (p.filterStages);
    filter_.setResonance (p.resonanceQ);

    // Key tracking: il cutoff segue la nota. Calcolato qui, non per campione.
    baseCutoffHz_ = p.cutoffHz;
    keyTrack_ = p.keyTrack;
    updateCutoff (false);
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
