#pragma once

#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace engine
{
struct EngineSpec
{
    double sampleRate { 44100.0 };
    int maximumBlockSize { 512 };
    int numChannels { 2 };
};

/** Real-time synth core. Owned by PluginProcessor; no UI / allocations here. */
class SynthEngine
{
public:
    void prepare (const EngineSpec& spec) noexcept;
    void reset() noexcept;

    /** Apply MIDI for this block then render. Buffers must be cleared by caller. */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    void setMasterGainLinear (float gain) noexcept;

private:
    void handleMidiEvent (const juce::MidiMessage& message) noexcept;

    VoiceManager voices_;
    EngineSpec spec_ {};
    float masterGain_ { 1.0f };
};
} // namespace engine
