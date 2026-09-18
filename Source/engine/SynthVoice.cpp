#include "engine/SynthVoice.h"

#include <algorithm>
#include <cmath>

namespace engine
{
namespace
{
constexpr double kSmoothingSeconds = 0.02;

// Headroom fisso per voce: senza di questo, una singola nota a level=1.0 con il volume di
// default satura gia' da sola, e un accordo satura pesantemente (vedi task-7-report.md,
// Finding 4). E' una costante fissa, non un divisore sul numero di voci attive: un divisore
// farebbe "respirare" il volume ogni volta che una nota parte o finisce, un difetto peggiore
// del clipping che risolve. -20 dB, misurato con HeadroomHarness (vedi report): una nota
// singola a level=1.0/volume di default arriva a -19.04 dBFS, un accordo di 16 voci simultanee
// (caso pessimistico: nessuna cancellazione di fase) arriva a -4.19 dBFS, quindi mai in clip.
constexpr float kVoiceHeadroomGain = 0.1f; // 10^(-20/20)

float midiNoteToHz (int note, float offsetSemitones) noexcept
{
    // std::pow gira solo a note-on, mai per campione: niente libm nel loop audio.
    return 440.0f * std::pow (2.0f, ((float) note + offsetSemitones - 69.0f) / 12.0f);
}

/** Saturazione polinomiale: unitaria a fondo scala, liscia, senza tanh() per campione. */
float saturate (float x) noexcept
{
    const auto clamped = std::clamp (x, -1.0f, 1.0f);
    return 1.5f * (clamped - (clamped * clamped * clamped) / 3.0f);
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
