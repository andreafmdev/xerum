#pragma once

namespace dsp
{
/** Stub ADSR envelope — gates only in phase 1. */
class ADSREnvelope
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setAttackSeconds (float seconds) noexcept;
    void setDecaySeconds (float seconds) noexcept;
    void setSustainLevel (float level) noexcept;
    void setReleaseSeconds (float seconds) noexcept;

    void noteOn() noexcept;
    void noteOff() noexcept;

    bool isActive() const noexcept;
    float getNextSample() noexcept;

private:
    double sampleRate_ { 44100.0 };
    bool gate_ { false };
    float level_ { 0.0f };

    float attack_ { 0.01f };
    float decay_ { 0.1f };
    float sustain_ { 0.8f };
    float release_ { 0.2f };
};
} // namespace dsp
