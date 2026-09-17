#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

class SerumStyleSynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SerumStyleSynthAudioProcessorEditor (SerumStyleSynthAudioProcessor&);
    ~SerumStyleSynthAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SerumStyleSynthAudioProcessor& processorRef_;
    juce::WebBrowserComponent webView_;

    static constexpr int kDefaultWidth = 900;
    static constexpr int kDefaultHeight = 560;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessorEditor)
};
