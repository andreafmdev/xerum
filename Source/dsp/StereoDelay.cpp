#include "dsp/StereoDelay.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
void StereoDelay::prepare (double sampleRate, int maximumBlockSize)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxDelaySamples_ = (float) (kMaxDelaySeconds * sampleRate_);

    // +4: Lagrange3rd legge tre campioni oltre l'indice intero (stesso margine del chorus).
    line_.setMaximumDelayInSamples ((int) std::ceil (maxDelaySamples_) + 4);

    juce::dsp::ProcessSpec spec {};
    spec.sampleRate = sampleRate_;
    spec.maximumBlockSize = (juce::uint32) std::max (1, maximumBlockSize);
    spec.numChannels = 2;
    line_.prepare (spec);

    smoothingCoeff_ = halfLifeCoefficient (kTimeSmoothingHalfLifeSeconds, sampleRate_);
    reset();
}

void StereoDelay::reset() noexcept
{
    line_.reset();
    lpLeft_ = lpRight_ = 0.0f;
    delay_ = -1.0f;
}

void StereoDelay::setParameters (float timeSeconds, float feedback01, float damp01, bool pingPong) noexcept
{
    delayTarget_ = juce::jlimit (1.0f, maxDelaySamples_, timeSeconds * (float) sampleRate_);
    feedback_ = juce::jlimit (0.0f, 1.0f, feedback01) * kMaxFeedback;
    pingPong_ = pingPong;

    // Damping: 20 kHz (0) -> 500 Hz (1), logaritmico come il riverbero. Un pow e un exp per
    // chiamata, cioe' una volta per blocco: mai per campione.
    const auto hz = kDampMaxHz * std::pow (kDampMinHz / kDampMaxHz, juce::jlimit (0.0f, 1.0f, damp01));
    dampCoeff_ = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sampleRate_);
}

float StereoDelay::tailSeconds (float timeSeconds, float feedback01) noexcept
{
    const auto fb = juce::jlimit (0.0f, 1.0f, feedback01) * kMaxFeedback;
    const auto repeats = fb <= 0.0f ? 1.0f : std::ceil (std::log (1.0e-4f) / std::log (fb));
    return juce::jmin (kMaxTailSeconds, timeSeconds * repeats);
}

void StereoDelay::process (float* left, float* right, int numSamples) noexcept
{
    forEachSlice (numSamples, kControlRateSamples, [&] (int offset, int slice)
    {
        processSlice (left + offset, right != nullptr ? right + offset : nullptr, slice);
    });
}

void StereoDelay::processSlice (float* left, float* right, int numSamples) noexcept
{
    // Il primo bersaglio si prende di scatto (la linea e' vuota: niente da far scivolare); i
    // successivi si inseguono con il polo, una volta per fetta.
    delay_ = delay_ < 0.0f ? delayTarget_ : delay_ + smoothingCoeff_ * (delayTarget_ - delay_);
    line_.setDelay (delay_);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = right != nullptr ? right[i] : 0.0f;

        const auto outL = line_.popSample (0);
        const auto outR = line_.popSample (1);

        // Il passa-basso sta nel solo anello di feedback: l'eco diretto resta pieno, quelli
        // successivi si scuriscono a ogni giro.
        lpLeft_ += dampCoeff_ * (outL - lpLeft_);
        lpRight_ += dampCoeff_ * (outR - lpRight_);

        if (pingPong_)
        {
            // L'ingresso (sommato a mono) entra solo a sinistra; ogni linea alimenta l'altra.
            line_.pushSample (0, 0.5f * (inL + inR) + feedback_ * lpRight_);
            line_.pushSample (1, feedback_ * lpLeft_);
        }
        else
        {
            line_.pushSample (0, inL + feedback_ * lpLeft_);
            line_.pushSample (1, inR + feedback_ * lpRight_);
        }

        left[i] = outL;

        if (right != nullptr)
            right[i] = outR;
    }
}
} // namespace dsp
