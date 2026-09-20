#pragma once

#include "bridge/MidiDevices.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace bridge
{
/**
 * Gli ingressi MIDI del sistema, per il selettore nella barra della UI.
 *
 * Native function: getMidiInputs() -> { host, devices }, setMidiInputEnabled(id, enabled).
 * Evento verso la UI: "midiInputsChanged", stesso payload, quando l'AudioDeviceManager cambia.
 *
 * Ha senso solo nello Standalone, dove il MIDI lo apre l'app (StandalonePluginHolder e il suo
 * AudioDeviceManager, che persiste gia' la scelta nelle impostazioni). In VST3/AU il MIDI arriva
 * dall'host e il canale risponde `host: true` con la lista vuota: la UI mostra la scritta, non
 * un selettore.
 */
class MidiDeviceChannel final : private juce::ChangeListener
{
public:
    explicit MidiDeviceChannel (juce::AudioProcessor& processor);
    ~MidiDeviceChannel() override;

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

    /** La WebView a cui mandare l'evento; nullptr la disattiva. */
    void setWebView (juce::WebBrowserComponent* view) noexcept { webView_ = view; }

private:
    juce::AudioDeviceManager* deviceManager() const noexcept;
    juce::var snapshot() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioProcessor& processor_;
    juce::WebBrowserComponent* webView_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDeviceChannel)
};
} // namespace bridge
