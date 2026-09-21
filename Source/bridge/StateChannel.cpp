#include "bridge/StateChannel.h"

#include "parameters/ParameterTable.h"
#include "parameters/PresetValue.h"
#include "state/StateTree.h"

#if JucePlugin_Build_Standalone
 // Stesso motivo di AudioSettingsChannel.cpp/MidiDeviceChannel.cpp: `currentInstance` e' inline
 // static nell'header, quindi includerlo nel codice condiviso e' innocuo per gli altri formati,
 // dove getInstance() risponde nullptr.
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace bridge
{
StateChannel::StateChannel (juce::AudioProcessorValueTreeState& apvts, juce::ChangeBroadcaster& stateReplaced)
    : apvts_ (apvts), stateReplaced_ (stateReplaced)
{
    state::ensureChildren (apvts_.state);
    listenTo (apvts_.state);
    stateReplaced_.addChangeListener (this);
}

StateChannel::~StateChannel()
{
    cancelPendingUpdate();
    stateReplaced_.removeChangeListener (this);
    listened_.removeListener (this);
}

void StateChannel::listenTo (juce::ValueTree root)
{
    listened_.removeListener (this);
    listened_ = root;
    listened_.addListener (this);
}

juce::WebBrowserComponent::Options StateChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("getState", [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            done (state::toVar (apvts_.state, {}));
        })
        .withNativeFunction ("setMods", [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            lastOrigin_ = args.size() > 1 ? args[1].toString() : juce::String();
            state::setMods (apvts_.state, juce::JSON::parse (args[0].toString()), apvts_.undoManager);
            done ({});
        })
        .withNativeFunction ("setArpSteps", [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            lastOrigin_ = args.size() > 1 ? args[1].toString() : juce::String();
            state::setArpSteps (apvts_.state, juce::JSON::parse (args[0].toString()), apvts_.undoManager);
            done ({});
        })
        .withNativeFunction ("loadPreset", [this] (const juce::Array<juce::var>& args,
                                                    juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // Siamo sul message thread: scrivere i parametri e notificare è sicuro.
            if (args.size() >= 1)
                applyPreset ((int) args[0]);

            done (juce::var {});
        })
        .withNativeFunction ("resetToDefaults",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 resetToDefaults();
                                 done (juce::var());
                             });
}

// La regola "quale valore normalizzato riceve questo parametro da questo preset" vive in
// params::presetValue (Source/parameters/PresetValue.h), testata in XerumTests: qui e' solo
// una chiamata. E' rispecchiata a mano in WebUI/src/juce/fake-backend.ts::loadPreset (stesso
// fallback al default di spec per un parametro non menzionato) — due file, due linguaggi,
// nessun modo di condividere il codice, quindi se cambi questa logica cambia anche l'altra.
void StateChannel::applyPreset (int index)
{
    if (index < 0 || index >= params::kNumPresets)
        return;

    const auto& preset = params::kPresetTable[index];

    for (const auto& spec : params::kTable)
    {
        auto* parameter = apvts_.getParameter (spec.id);

        if (parameter == nullptr)
            continue;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (params::presetValue (preset, spec));
        parameter->endChangeGesture();
    }

    emitState ("preset");
}

void StateChannel::resetToDefaults()
{
    // Non si ricrea il plugin come fa JUCE (juce_StandaloneFilterWindow.h:766-780): quel codice
    // chiama clearContentComponent(), che distruggerebbe la WebView mentre sta eseguendo la
    // native function da cui siamo arrivati qui. Si riportano i valori ai default, che per
    // questo strumento e' la stessa cosa vista da fuori.
    for (auto* p : apvts_.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
            ranged->endChangeGesture();
        }

    resetNonParametricState();

    // Solo Standalone: e' la ragione per cui questo comando esiste. Senza, savePluginState()
    // (chiamata solo alla chiusura della finestra, juce_StandaloneFilterWindow.h:784) riscrive
    // comunque lo stato di fabbrica in "filterState" al prossimo close pulito — ma se la app
    // finisse prima (crash, kill, force quit) il vecchio "filterState" resterebbe sul disco e la
    // prossima apertura ricaricherebbe la patch che l'utente ha appena buttato via.
   #if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* props = holder->settings.get())
            props->removeValue ("filterState");
   #endif
}

void StateChannel::resetNonParametricState()
{
    // Stessa via di setMods/setArpSteps (vedi applyTo sopra): il diff sul ValueTree passa da qui,
    // con l'undo, e l'emit di "stateChanged" arriva da solo tramite valueTreeChildRemoved/
    // valueTreePropertyChanged -> triggerAsyncUpdate, non da una seconda strada scritta apposta.
    state::setMods (apvts_.state, juce::var (juce::Array<juce::var>()), apvts_.undoManager);

    // Stessi 16 valori di default di state::ensureChildren e del FakeBackend lato WebUI
    // (WebUI/src/juce/fake-backend.ts): tre posti, nessun modo di condividerli, quindi se cambia
    // uno cambiano tutti e tre.
    juce::Array<juce::var> defaultSteps;
    for (double v : { 0.8, 0.0, 0.6, 0.9, 0.0, 0.7, 0.0, 0.5, 0.8, 0.0, 0.6, 0.0, 0.9, 0.4, 0.0, 0.7 })
        defaultSteps.add (v);
    state::setArpSteps (apvts_.state, juce::var (defaultSteps), apvts_.undoManager);
}

bool StateChannel::isOurs (const juce::ValueTree& t) const
{
    return t.hasType (state::ids::MODS) || t.hasType (state::ids::MOD) || t.hasType (state::ids::ARP);
}

void StateChannel::emitState (const juce::String& origin)
{
    if (webView_ != nullptr)
        webView_->emitEventIfBrowserIsVisible ("stateChanged", state::toVar (apvts_.state, origin));
}

// Ogni cambio (nostro o esterno) marca soltanto: setMods produce removeAllChildren + N append, cioè
// 2N callback, e un emitState sincrono per ciascuna significherebbe 2N toVar + evaluateJavascript
// durante un drag di depth. L'AsyncUpdater li coalizza in un solo evento per giro di message loop.
void StateChannel::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) { if (isOurs (t)) triggerAsyncUpdate(); }
void StateChannel::valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&)             { if (isOurs (p)) triggerAsyncUpdate(); }
void StateChannel::valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int)      { if (isOurs (p)) triggerAsyncUpdate(); }

// L'origin dell'ultima scrittura dalla UI deve sopravvivere fino a qui: si azzera solo dopo l'emit,
// così il prossimo cambio senza origin (host, undo, automazione) parte davvero da stringa vuota.
void StateChannel::handleAsyncUpdate()
{
    emitState (lastOrigin_);
    lastOrigin_.clear();
}

void StateChannel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // replaceState() ha sostituito l'albero radice: riaggancia il listener e notifica (origin vuoto = esterno).
    // L'emit pendente si riferirebbe all'albero vecchio: lo annulliamo e mandiamo questo, sincrono.
    cancelPendingUpdate();
    lastOrigin_.clear();
    listenTo (apvts_.state);
    emitState ({});
}
} // namespace bridge
