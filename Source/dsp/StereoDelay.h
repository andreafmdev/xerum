#pragma once

#include "dsp/Constants.h"

#include <juce_dsp/juce_dsp.h>

namespace dsp
{
/**
 * Delay stereo: due linee Lagrange-3 da kMaxDelaySeconds, feedback con passa-basso a un polo,
 * ping-pong opzionale. Uscita: il **solo bagnato** a guadagno 1 (il mix lo fa il chiamante, con
 * la stessa regola sin3dB degli altri effetti). Il tempo insegue il bersaglio con un polo di
 * emivita kTimeSmoothingHalfLifeSeconds: cambiarlo fa scivolare l'intonazione degli echi, che e'
 * il suono di un delay analogico, e non produce clic. Nessuna allocazione dopo prepare().
 */
class StereoDelay
{
public:
    static constexpr float kMaxDelaySeconds = 2.0f;
    /** Sopra, la coda smette di finire: 90 % e' il tetto del knob. */
    static constexpr float kMaxFeedback = 0.9f;
    static constexpr float kDampMaxHz = 20000.0f;
    static constexpr float kDampMinHz = 500.0f;
    static constexpr float kTimeSmoothingHalfLifeSeconds = 0.050f;
    /** La coda dichiarata all'host non supera questo, qualunque siano tempo e feedback. */
    static constexpr float kMaxTailSeconds = 60.0f;

    void prepare (double sampleRate, int maximumBlockSize);
    void reset() noexcept;
    void setParameters (float timeSeconds, float feedback01, float damp01, bool pingPong) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    /** Quanto dura la coda sotto -80 dB: `time · ceil(ln 1e-4 / ln feedback)`, `time` con feedback 0. */
    static float tailSeconds (float timeSeconds, float feedback01) noexcept;

private:
    void processSlice (float* left, float* right, int numSamples) noexcept;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line_;
    double sampleRate_ { 48000.0 };
    float maxDelaySamples_ { 0.0f };
    float delayTarget_ { 0.0f };
    float delay_ { -1.0f };          // -1: nessun valore ancora, il primo bersaglio si prende di scatto
    float smoothingCoeff_ { 1.0f };
    float feedback_ { 0.0f };
    float dampCoeff_ { 1.0f };
    bool pingPong_ { false };
    float lpLeft_ { 0.0f };
    float lpRight_ { 0.0f };
};
} // namespace dsp
