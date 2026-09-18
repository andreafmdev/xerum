#include "dsp/ADSREnvelope.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
/** Soglia di spegnimento: -80 dB. Sotto, la coda è inudibile e la voce va liberata. */
constexpr float kSilence = 1.0e-4f;

/** Quante costanti di tempo servono per considerare finito ogni stadio. */
constexpr float kAttackTau = 4.605170f;   // 1 - e^-4.605170 ≈ 0.99
constexpr float kDecayTau = 4.605170f;    // 99 % della distanza
constexpr float kReleaseTau = 9.210340f; // e^-9.210340 ≈ 1e-4, cioè -80 dB

float coefficientFor (float seconds, float constants, double sampleRate) noexcept
{
    const auto samples = std::max (1.0, (double) seconds * sampleRate);
    return 1.0f - (float) std::exp (-(double) constants / samples);
}
} // namespace

void ADSREnvelope::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients();
    reset();
}

void ADSREnvelope::reset() noexcept
{
    stage_ = Stage::idle;
    level_ = 0.0f;
    decayDistance_ = 0.0f;
}

void ADSREnvelope::setAttackSeconds (float seconds) noexcept
{
    attackSeconds_ = std::max (0.0f, seconds);
    updateCoefficients();
}

void ADSREnvelope::setDecaySeconds (float seconds) noexcept
{
    decaySeconds_ = std::max (0.0f, seconds);
    updateCoefficients();
}

void ADSREnvelope::setSustainLevel (float level) noexcept
{
    sustain_ = std::clamp (level, 0.0f, 1.0f);
}

void ADSREnvelope::setReleaseSeconds (float seconds) noexcept
{
    releaseSeconds_ = std::max (0.0f, seconds);
    updateCoefficients();
}

void ADSREnvelope::updateCoefficients() noexcept
{
    attackCoeff_ = coefficientFor (attackSeconds_, kAttackTau, sampleRate_);
    decayCoeff_ = coefficientFor (decaySeconds_, kDecayTau, sampleRate_);
    releaseCoeff_ = coefficientFor (releaseSeconds_, kReleaseTau, sampleRate_);
}

void ADSREnvelope::noteOn (float peak) noexcept
{
    peak_ = std::clamp (peak, 0.0f, 1.0f);
    stage_ = Stage::attack;
}

void ADSREnvelope::noteOff() noexcept
{
    if (stage_ != Stage::idle)
        stage_ = Stage::release;
}

float ADSREnvelope::getNextSample() noexcept
{
    switch (stage_)
    {
        case Stage::idle:
            return 0.0f;

        case Stage::attack:
            level_ += attackCoeff_ * (peak_ - level_);
            if (level_ >= peak_ * 0.99f)
            {
                level_ = peak_;
                const auto target = peak_ * sustain_;
                decayDistance_ = std::abs (peak_ - target);
                if (decayDistance_ <= 0.0f)
                {
                    level_ = target;
                    stage_ = Stage::sustain;
                }
                else
                {
                    stage_ = Stage::decay;
                }
            }
            break;

        case Stage::decay:
        {
            const auto target = peak_ * sustain_;
            level_ += decayCoeff_ * (target - level_);
            if (std::abs (level_ - target) <= 0.01f * decayDistance_)
            {
                level_ = target;
                stage_ = Stage::sustain;
            }
            break;
        }

        case Stage::sustain:
            level_ = peak_ * sustain_;
            break;

        case Stage::release:
            level_ -= releaseCoeff_ * level_;
            if (level_ < kSilence)
            {
                level_ = 0.0f;
                stage_ = Stage::idle;
            }
            break;
    }

    return level_;
}
} // namespace dsp
