#include "dsp/ADSREnvelope.h"

namespace dsp
{
void ADSREnvelope::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void ADSREnvelope::reset() noexcept
{
    gate_ = false;
    level_ = 0.0f;
}

void ADSREnvelope::setAttackSeconds (float seconds) noexcept
{
    attack_ = seconds;
}

void ADSREnvelope::setDecaySeconds (float seconds) noexcept
{
    decay_ = seconds;
}

void ADSREnvelope::setSustainLevel (float level) noexcept
{
    sustain_ = level;
}

void ADSREnvelope::setReleaseSeconds (float seconds) noexcept
{
    release_ = seconds;
}

void ADSREnvelope::noteOn() noexcept
{
    gate_ = true;
    level_ = 1.0f;
}

void ADSREnvelope::noteOff() noexcept
{
    gate_ = false;
    level_ = 0.0f;
}

bool ADSREnvelope::isActive() const noexcept
{
    return gate_ || level_ > 0.0f;
}

float ADSREnvelope::getNextSample() noexcept
{
    // Phase 1: hard gate (no slopes).
    (void) sampleRate_;
    (void) attack_;
    (void) decay_;
    (void) sustain_;
    (void) release_;
    return gate_ ? 1.0f : 0.0f;
}
} // namespace dsp
