#include "dsp/WavetableOscillator.h"

namespace dsp
{
void WavetableOscillator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void WavetableOscillator::reset() noexcept
{
    phase_ = 0.0f;
}

void WavetableOscillator::setWavetable (const float* table, int tableSize) noexcept
{
    table_ = table;
    tableSize_ = tableSize > 0 ? tableSize : 0;
}

void WavetableOscillator::setFrequencyHz (float hz) noexcept
{
    if (sampleRate_ <= 0.0)
        return;

    phaseIncrement_ = static_cast<float> (hz / sampleRate_);
}

float WavetableOscillator::getSample() noexcept
{
    // Phase 1: no wavetable — return silence.
    // Phase 3: interpolate table_[phase * tableSize] and advance phase.
    phase_ += phaseIncrement_;
    if (phase_ >= 1.0f)
        phase_ -= 1.0f;

    (void) table_;
    (void) tableSize_;
    return 0.0f;
}
} // namespace dsp
