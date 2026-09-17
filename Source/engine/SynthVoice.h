#pragma once

#include "dsp/ADSREnvelope.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/WavetableOscillator.h"

namespace engine
{
/** Single synth voice — phase 1 renders silence (or optional test tone). */
class SynthVoice
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void start (int midiNote, float velocity) noexcept;
    void stop() noexcept;
    void kill() noexcept;

    bool isActive() const noexcept;
    int getMidiNote() const noexcept;

    /** Mix into stereo buffers (additive). Real-time safe. */
    void render (float* outL, float* outR, int numSamples) noexcept;

private:
    double sampleRate_ { 44100.0 };
    bool active_ { false };
    int midiNote_ { -1 };
    float velocity_ { 0.0f };
    float frequencyHz_ { 440.0f };
    double phase_ { 0.0 }; // used only when util::kEnableTestTone

    dsp::WavetableOscillator oscillator_;
    dsp::StateVariableFilter filter_;
    dsp::ADSREnvelope envelope_;
};
} // namespace engine
