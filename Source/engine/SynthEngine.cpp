#include "engine/SynthEngine.h"

namespace engine
{
void SynthEngine::prepare (const EngineSpec& spec) noexcept
{
    spec_ = spec;
    voices_.prepare (spec_.sampleRate);
}

void SynthEngine::reset() noexcept
{
    voices_.reset();
}

void SynthEngine::setMasterGainLinear (float gain) noexcept
{
    masterGain_ = gain;
}

void SynthEngine::handleMidiEvent (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        voices_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        voices_.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff())
    {
        voices_.allNotesOff();
    }
    else if (message.isAllSoundOff())
    {
        voices_.allSoundOff();
    }
}

void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : left;

    int samplePos = 0;

    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);
        const int slice = eventPos - samplePos;

        if (slice > 0)
        {
            voices_.render (left + samplePos, right + samplePos, slice);
            samplePos = eventPos;
        }

        handleMidiEvent (metadata.getMessage());
    }

    if (samplePos < numSamples)
        voices_.render (left + samplePos, right + samplePos, numSamples - samplePos);

    if (masterGain_ != 1.0f)
        buffer.applyGain (masterGain_);

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
}
} // namespace engine
