#include "bridge/MidiChannel.h"

#include "plugin/PluginProcessor.h"

namespace bridge
{
namespace
{
/** Il canale su cui la UI suona. Uno solo: la striscia non ha un selettore di canale. */
constexpr int kChannel = 1;

int noteArg (const juce::Array<juce::var>& args, int index)
{
    return index < args.size() ? juce::jlimit (0, 127, (int) args[index]) : -1;
}
} // namespace

MidiChannel::MidiChannel (juce::MidiKeyboardState& keyboardState,
                          SerumStyleSynthAudioProcessor& processor)
    : keyboardState_ (keyboardState), processor_ (processor)
{
}

juce::WebBrowserComponent::Options MidiChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("noteOn",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 const auto note = noteArg (args, 0);
                                 const auto velocity = args.size() > 1 ? (float) args[1] : 0.8f;

                                 if (note >= 0)
                                     keyboardState_.noteOn (kChannel, note, juce::jlimit (0.0f, 1.0f, velocity));

                                 done (juce::var());
                             })
        .withNativeFunction ("noteOff",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (const auto note = noteArg (args, 0); note >= 0)
                                     keyboardState_.noteOff (kChannel, note, 0.0f);

                                 done (juce::var());
                             })
        .withNativeFunction ("allNotesOff",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 // Non e' cosmetico: se la WebView perde il puntatore a meta'
                                 // click, la nota resta appesa e suona per sempre.
                                 keyboardState_.allNotesOff (kChannel);
                                 done (juce::var());
                             })
        .withNativeFunction ("setWheel",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.size() >= 2)
                                 {
                                     const auto kind = args[0].toString();
                                     const auto value = juce::jlimit (0.0f, 1.0f, (float) args[1]);

                                     if (kind == "pitch")
                                         processor_.setUiPitchBend (value);
                                     else if (kind == "mod")
                                         processor_.setUiModWheel (value);
                                 }

                                 done (juce::var());
                             });
}
} // namespace bridge
