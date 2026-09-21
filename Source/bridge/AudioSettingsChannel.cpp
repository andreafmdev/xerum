#include "bridge/AudioSettingsChannel.h"

#if JucePlugin_Build_Standalone
 // Stesso motivo di MidiDeviceChannel.cpp: `currentInstance` e' inline static nell'header, quindi
 // includerlo nel codice condiviso e' innocuo per gli altri formati.
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace bridge
{
AudioSettingsChannel::AudioSettingsChannel (juce::AudioProcessor& processor)
    : processor_ (processor)
{
    if (auto* dm = deviceManager())
        dm->addChangeListener (this);
}

AudioSettingsChannel::~AudioSettingsChannel()
{
    if (auto* dm = deviceManager())
        dm->removeChangeListener (this);
}

juce::AudioDeviceManager* AudioSettingsChannel::deviceManager() const noexcept
{
   #if JucePlugin_Build_Standalone
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return &holder->deviceManager;
   #endif

    return nullptr;
}

juce::var AudioSettingsChannel::snapshot() const
{
    auto* dm = deviceManager();

    if (dm == nullptr)
        return audioSettingsToVar ({});

    AudioSettings s;
    s.standalone = true;

    if (auto* type = dm->getCurrentDeviceTypeObject())
    {
        type->scanForDevices();

        for (const auto& name : type->getDeviceNames (false))
            s.outputs.push_back ({ name, name });
    }

    const auto setup = dm->getAudioDeviceSetup();
    s.currentOutput = setup.outputDeviceName;

    // Le liste dipendono dal device aperto: senza device restano vuote, e la UI mostra solo il
    // selettore dei device. Non si inventano valori di ripiego.
    if (auto* device = dm->getCurrentAudioDevice())
    {
        for (const auto r : device->getAvailableSampleRates()) s.sampleRates.push_back (r);
        for (const auto b : device->getAvailableBufferSizes()) s.bufferSizes.push_back (b);
        s.currentSampleRate = device->getCurrentSampleRate();
        s.currentBufferSize = device->getCurrentBufferSizeSamples();
    }

    return audioSettingsToVar (s);
}

juce::String AudioSettingsChannel::applySetup (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change)
{
    auto* dm = deviceManager();

    if (dm == nullptr)
        return "Le impostazioni audio le gestisce l'host.";

    auto setup = dm->getAudioDeviceSetup();
    change (setup);

    // `true` = trattalo come scelta dell'utente: e' cio' che lo fa finire nelle impostazioni
    // salvate invece di restare una preferenza volatile.
    const auto error = dm->setAudioDeviceSetup (setup, true);

   #if JucePlugin_Build_Standalone
    if (error.isEmpty())
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->saveAudioDeviceState();
   #endif

    return error;
}

void AudioSettingsChannel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (webView_ != nullptr)
        webView_->emitEventIfBrowserIsVisible ("audioSettingsChanged", snapshot());
}

juce::WebBrowserComponent::Options AudioSettingsChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("getAudioSettings",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 done (snapshot());
                             })
        .withNativeFunction ("setAudioOutput",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.isEmpty())
                                     return done (juce::var (juce::String ("Nessun device indicato.")));

                                 const auto name = args[0].toString();
                                 done (juce::var (applySetup ([&name] (auto& setup) { setup.outputDeviceName = name; })));
                             })
        .withNativeFunction ("setSampleRate",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.isEmpty())
                                     return done (juce::var (juce::String ("Nessun sample rate indicato.")));

                                 const auto rate = (double) args[0];
                                 done (juce::var (applySetup ([rate] (auto& setup) { setup.sampleRate = rate; })));
                             })
        .withNativeFunction ("setBufferSize",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.isEmpty())
                                     return done (juce::var (juce::String ("Nessun buffer size indicato.")));

                                 const auto size = (int) args[0];
                                 done (juce::var (applySetup ([size] (auto& setup) { setup.bufferSize = size; })));
                             });
}
} // namespace bridge
