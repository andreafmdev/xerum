#include "bridge/MidiDeviceChannel.h"

#if JucePlugin_Build_Standalone
 // L'header dell'holder da' per scontati juce_audio_utils e juce_audio_devices gia' inclusi
 // (li include il file .cpp dello Standalone, non lui): qui ci pensa MidiDeviceChannel.h.
 // L'holder dello Standalone: `currentInstance` e' `inline static` nell'header, quindi includerlo
 // nel codice condiviso e' innocuo per gli altri formati, dove getInstance() risponde nullptr.
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace bridge
{
MidiDeviceChannel::MidiDeviceChannel (juce::AudioProcessor& processor)
    : processor_ (processor)
{
    if (auto* dm = deviceManager())
        dm->addChangeListener (this);
}

MidiDeviceChannel::~MidiDeviceChannel()
{
    if (auto* dm = deviceManager())
        dm->removeChangeListener (this);
}

juce::AudioDeviceManager* MidiDeviceChannel::deviceManager() const noexcept
{
   #if JucePlugin_Build_Standalone
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return &holder->deviceManager;
   #endif

    return nullptr;
}

juce::var MidiDeviceChannel::snapshot() const
{
    auto* dm = deviceManager();

    if (dm == nullptr)
        return midiInputsToVar (true, {});

    std::vector<MidiInputDevice> devices;

    for (const auto& info : juce::MidiInput::getAvailableDevices())
        devices.push_back ({ info.identifier, info.name, dm->isMidiInputDeviceEnabled (info.identifier) });

    return midiInputsToVar (false, devices);
}

void MidiDeviceChannel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (webView_ != nullptr)
        webView_->emitEventIfBrowserIsVisible ("midiInputsChanged", snapshot());
}

juce::WebBrowserComponent::Options MidiDeviceChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("getMidiInputs",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 done (snapshot());
                             })
        .withNativeFunction ("setMidiInputEnabled",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (auto* dm = deviceManager(); dm != nullptr && args.size() >= 2)
                                     dm->setMidiInputDeviceEnabled (args[0].toString(), (bool) args[1]);

                                 done (juce::var());
                             });
}
} // namespace bridge
