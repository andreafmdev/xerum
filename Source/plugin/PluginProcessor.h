#pragma once

#include "engine/MeterFrame.h"
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

    /** Shared with the editor's on-screen keyboard; merged into the MIDI stream in processBlock(). */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState_; }

    /** Picchi per blocco pubblicati dal thread audio; letti dal timer dell'editor (task successivo). */
    engine::MeterFrame& getMeters() noexcept { return meters_; }

private:
    juce::AudioProcessorValueTreeState apvts_;
    juce::MidiKeyboardState keyboardState_;
    std::unique_ptr<engine::SynthEngine> engine_;
    engine::MeterFrame meters_;

    // Puntatori grezzi ai valori normalizzati 0..1 dell'APVTS: letti solo con load() sul thread audio.
    std::atomic<float>* volumeParam_ { nullptr };
    std::atomic<float>* levelParam_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessor)
};
