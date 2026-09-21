#pragma once

#include "bridge/AudioSettings.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace bridge
{
/**
 * Il device audio del sistema, per il pannello impostazioni della UI.
 *
 * Native function: getAudioSettings() -> vedi bridge/AudioSettings.h; setAudioOutput(id),
 * setSampleRate(hz), setBufferSize(n), che tornano "" se e' andata e il messaggio d'errore di
 * AudioDeviceManager::setAudioDeviceSetup altrimenti.
 * Evento verso la UI: "audioSettingsChanged", stesso payload di getAudioSettings.
 *
 * Ha senso solo nello Standalone, dove l'audio lo apre l'app (StandalonePluginHolder e il suo
 * AudioDeviceManager, che persiste gia' la scelta). In VST3/AU risponde `standalone: false` con
 * le liste vuote: la UI mostra la scritta, non i controlli. Gemello di MidiDeviceChannel.
 */
class AudioSettingsChannel final : private juce::ChangeListener
{
public:
    explicit AudioSettingsChannel (juce::AudioProcessor& processor);
    ~AudioSettingsChannel() override;

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

    /** La WebView a cui mandare l'evento; nullptr la disattiva. */
    void setWebView (juce::WebBrowserComponent* view) noexcept { webView_ = view; }

private:
    juce::AudioDeviceManager* deviceManager() const noexcept;
    juce::var snapshot() const;
    /** Applica una modifica al setup corrente. Torna "" se e' andata, l'errore altrimenti. */
    juce::String applySetup (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioProcessor& processor_;
    juce::WebBrowserComponent* webView_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsChannel)
};
} // namespace bridge
