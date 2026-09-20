#pragma once

#include "bridge/MeterChannel.h"
#include "bridge/MidiChannel.h"
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
    SerumStyleSynthAudioProcessor& processorRef_;

    /** Relay dei parametri verso la WebView: dichiarati prima di webView_ perché le
        Options della WebView vengono costruite a partire da loro. */
    bridge::WebRelays relays_;

    /** Canale per lo stato non parametrico (mod matrix, arp): anche lui prima di webView_. */
    bridge::StateChannel stateChannel_;

    /** Note e rotelle suonate dentro la WebView: anche lui prima di webView_. */
    bridge::MidiChannel midiChannel_;

    juce::WebBrowserComponent webView_;

    /** Timer a 30 Hz che manda i picchi dei meter alla WebView: tiene un riferimento a
        webView_, quindi va dichiarato dopo di lei per essere distrutto prima. */
    bridge::MeterChannel meters_;

    /** Lega l'altezza della finestra alla larghezza: senza, la WebView resta più
        alta dello chassis scalato e fra pannello e tastiera si apre una banda vuota. */
    std::unique_ptr<juce::ComponentBoundsConstrainer> constrainer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessorEditor)
};
