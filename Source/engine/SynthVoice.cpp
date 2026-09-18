#include "engine/SynthVoice.h"

#include <cmath>

namespace engine
{
namespace
{
float midiNoteToHz (int note) noexcept
{
    // std::pow gira solo a note-on (da SynthVoice::start), mai per campione: niente libm nel loop audio.
    return 440.0f * std::pow (2.0f, (static_cast<float> (note) - 69.0f) / 12.0f);
}
} // namespace

void SynthVoice::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    oscillator_.prepare (sampleRate_);
    filter_.prepare (sampleRate_);
    envelope_.prepare (sampleRate_);
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
    frequencyHz_ = midiNoteToHz (midiNote);
    oscillator_.setFrequencyHz (frequencyHz_);
    envelope_.noteOn (velocity);
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
    oscillator_.setTable (table);
}

void SynthVoice::setFramePosition (float normalised) noexcept
{
    oscillator_.setFramePosition (normalised);
}

void SynthVoice::render (float* outL, float* outR, int numSamples) noexcept
{
    if (! isActive() || outL == nullptr || outR == nullptr || numSamples <= 0)
        return;

    const float amp = 0.2f;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = oscillator_.getSample() * amp * envelope_.getNextSample();
        sample = filter_.processSample (sample);

        outL[i] += sample;
        outR[i] += sample;
    }

    if (! envelope_.isActive())
        active_ = false;
}
} // namespace engine
