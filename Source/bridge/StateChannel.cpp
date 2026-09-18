#include "bridge/StateChannel.h"

#include "parameters/ParameterTable.h"
#include "parameters/StateTree.h"

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
        });
}

// Rispecchiata in WebUI/src/juce/fake-backend.ts::loadPreset (stesso fallback al default
// di spec per un parametro non menzionato). Le due implementazioni possono divergere senza
// che nessun test se ne accorga (XerumTests non compila questo file): se cambi questa
// logica, cambia anche l'altra.
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

        // Un parametro non elencato nel preset torna al suo default di spec:
        // altrimenti i preset ereditano pezzi del suono precedente.
        float value = spec.def;

        for (int i = 0; i < preset.numValues; ++i)
            if (juce::String (preset.values[i].id) == spec.id)
                value = preset.values[i].value;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (value);
        parameter->endChangeGesture();
    }

    emitState ("preset");
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
