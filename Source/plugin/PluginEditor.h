#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class SerumStyleSynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SerumStyleSynthAudioProcessorEditor (SerumStyleSynthAudioProcessor&);
    ~SerumStyleSynthAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void configureKeyboard();

    SerumStyleSynthAudioProcessor& processorRef_;
    juce::WebBrowserComponent webView_;

    /** Native on-screen keyboard strip under the WebView (Serum/Vital style).
        Feeds the processor's MidiKeyboardState, which is merged into the host
        MIDI stream in processBlock(). */
    juce::MidiKeyboardComponent keyboard_;

    static constexpr int kDefaultWidth = 900;
    static constexpr int kDefaultHeight = 632;
    static constexpr int kKeyboardHeight = 72;
    static constexpr int kLowestNote = 36;   // C2
    static constexpr int kHighestNote = 96;  // C7
    static constexpr int kWhiteKeysVisible = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessorEditor)
};
