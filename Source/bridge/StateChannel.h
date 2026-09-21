#pragma once

// audio_devices e audio_utils servono solo per resetToDefaults(): juce_StandaloneFilterWindow.h,
// incluso nel .cpp sotto #if JucePlugin_Build_Standalone, dà per scontato che chi lo include li
// abbia già aperti (di solito lo fa il .cpp dello Standalone, non lui). Stesso motivo di
// MidiDeviceChannel.h/AudioSettingsChannel.h.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>


namespace bridge
{
/** Stato non parametrico (mod matrix, arp steps) fra ValueTree APVTS e WebView.
    Native functions: getState(), setMods(json, origin), setArpSteps(json, origin),
    resetToDefaults(). Evento verso la UI: "stateChanged" con lo stesso payload di getState. */
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
    /** Voce "Reset to default state" del vecchio menu Options di JUCE. Non ricrea il plugin come
        juce_StandaloneFilterWindow.h:766-780 (clearContentComponent()+deletePlugin()): quella
        chiamata distruggerebbe la WebView da dentro la native function che la WebView stessa sta
        eseguendo. Riporta invece i valori ai default: stessa cosa vista da fuori, senza smontare
        niente. Parametri e stato non parametrico valgono anche in AU/VST3; lo stato salvato
        (filterState) e' solo Standalone, vedi il .cpp. */
    void resetToDefaults();
    /** Mod matrix e passi dell'arp ai default, per la stessa via di setMods/setArpSteps: cosi'
        la UI riceve lo stesso "stateChanged" che gia' sa gestire, non una seconda via di scrittura. */
    void resetNonParametricState();
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
