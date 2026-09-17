#include "dsp/StateVariableFilter.h"

namespace dsp
{
void StateVariableFilter::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void StateVariableFilter::reset() noexcept
{
}

void StateVariableFilter::setType (Type type) noexcept
{
    type_ = type;
}

void StateVariableFilter::setCutoffHz (float hz) noexcept
{
    cutoffHz_ = hz;
}

void StateVariableFilter::setResonance (float q) noexcept
{
    resonance_ = q;
}

float StateVariableFilter::processSample (float input) noexcept
{
    // Phase 1: bypass.
    (void) type_;
    (void) cutoffHz_;
    (void) resonance_;
    (void) sampleRate_;
    return input;
}
} // namespace dsp
