#pragma once

#include <cstddef>
#include <cstdint>

namespace dsp
{
/** Stub wavetable oscillator — phase 1 holds the API surface only. */
class WavetableOscillator
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Future: point at a read-only wavetable owned by WavetableStore. */
    void setWavetable (const float* table, int tableSize) noexcept;

    void setFrequencyHz (float hz) noexcept;
    float getSample() noexcept;

private:
    double sampleRate_ { 44100.0 };
    float phase_ { 0.0f };
    float phaseIncrement_ { 0.0f };
    const float* table_ { nullptr };
    int tableSize_ { 0 };
};
} // namespace dsp
