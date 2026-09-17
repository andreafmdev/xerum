#pragma once

#include "engine/SynthEngine.h"
#include "parameters/ParameterLayout.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

class SerumStyleSynthAudioProcessor final : public juce::AudioProcessor
{
public:
    SerumStyleSynthAudioProcessor();
    ~SerumStyleSynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }

private:
    juce::AudioProcessorValueTreeState apvts_;
    std::unique_ptr<engine::SynthEngine> engine_;

    std::atomic<float>* masterGainParam_ { nullptr };
    std::atomic<float>* osc1LevelParam_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessor)
};
