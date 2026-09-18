#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "parameters/PresetTable.h"

namespace bridge
{
/** Stato non parametrico (mod matrix, arp steps) fra ValueTree APVTS e WebView.
    Native functions: getState(), setMods(json, origin), setArpSteps(json, origin).
    Evento verso la UI: "stateChanged" con lo stesso payload di getState. */
class StateChannel final : private juce::ValueTree::Listener,
                           private juce::ChangeListener,
                           private juce::AsyncUpdater
{
public:
    StateChannel (juce::AudioProcessorValueTreeState& apvts, juce::ChangeBroadcaster& stateReplaced);
    ~StateChannel() override;

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

    /** La WebView a cui mandare l'evento "stateChanged"; nullptr la disattiva. */
    void setWebView (juce::WebBrowserComponent* view) noexcept { webView_ = view; }

private:
    void listenTo (juce::ValueTree root);
    void emitState (const juce::String& origin);
    /** Scrive i valori del preset indicato via beginChangeGesture/setValueNotifyingHost/
        endChangeGesture: cosi' l'host registra il cambio e l'undo funziona. Sul message thread. */
    void applyPreset (int index);
    bool isOurs (const juce::ValueTree& tree) const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState& apvts_;
    juce::ChangeBroadcaster& stateReplaced_;
    juce::ValueTree listened_;
    juce::WebBrowserComponent* webView_ { nullptr };
    juce::String lastOrigin_;   // origin dell'ultima scrittura arrivata dalla UI, rimbalzato nell'evento

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateChannel)
};
} // namespace bridge
