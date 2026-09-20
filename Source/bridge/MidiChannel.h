#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class SerumStyleSynthAudioProcessor;

namespace bridge
{
/**
 * Le note e le rotelle suonate dentro la WebView.
 *
 * Native function: noteOn(note, velocity), noteOff(note), allNotesOff(), setWheel(kind, value).
 *
 * Le note finiscono nel MidiKeyboardState del processor, che processBlock() fonde gia' nel flusso
 * dell'host: una nota della UI e' quindi indistinguibile da una del DAW e non esiste codice nuovo
 * a valle. Le rotelle non possono passare di li' — MidiKeyboardState trasporta solo note — e
 * vanno invece in due atomici che il processor traduce in messaggi MIDI veri.
 *
 * Nessun evento verso la UI: quali note suonano lo dice il mask dentro il frame `meters`.
 */
class MidiChannel final
{
public:
    MidiChannel (juce::MidiKeyboardState& keyboardState, SerumStyleSynthAudioProcessor& processor);

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

private:
    juce::MidiKeyboardState& keyboardState_;
    SerumStyleSynthAudioProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiChannel)
};
} // namespace bridge
