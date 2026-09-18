#pragma once

#include "bridge/MeterChannel.h"
#include "bridge/StateChannel.h"
#include "bridge/WebRelays.h"
#include "plugin/PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class SerumStyleSynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SerumStyleSynthAudioProcessorEditor (SerumStyleSynthAudioProcessor&);
    ~SerumStyleSynthAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void configureKeyboard();

    SerumStyleSynthAudioProcessor& processorRef_;

    /** Relay dei parametri verso la WebView: dichiarati prima di webView_ perché le
        Options della WebView vengono costruite a partire da loro. */
    bridge::WebRelays relays_;

    /** Canale per lo stato non parametrico (mod matrix, arp): anche lui prima di webView_. */
    bridge::StateChannel stateChannel_;

    juce::WebBrowserComponent webView_;

    /** Timer a 30 Hz che manda i picchi dei meter alla WebView: tiene un riferimento a
        webView_, quindi va dichiarato dopo di lei per essere distrutto prima. */
    bridge::MeterChannel meters_;

    /** Native on-screen keyboard strip under the WebView (Serum/Vital style).
        Feeds the processor's MidiKeyboardState, which is merged into the host
        MIDI stream in processBlock(). */
    juce::MidiKeyboardComponent keyboard_;

    static constexpr int kDefaultWidth = 900;
    static constexpr int kDefaultHeight = 672; // 600 (chassis WebUI) + tastiera
    static constexpr int kKeyboardHeight = 72;
    static constexpr int kLowestNote = 36;   // C2
    static constexpr int kHighestNote = 96;  // C7
    static constexpr int kWhiteKeysVisible = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessorEditor)
};
