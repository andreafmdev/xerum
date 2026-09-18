#include "bridge/StateChannel.h"

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
            lastOrigin_.clear();   // la scrittura è sincrona: i callback hanno già usato l'origin
            done ({});
        })
        .withNativeFunction ("setArpSteps", [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            lastOrigin_ = args.size() > 1 ? args[1].toString() : juce::String();
            state::setArpSteps (apvts_.state, juce::JSON::parse (args[0].toString()), apvts_.undoManager);
            lastOrigin_.clear();   // la scrittura è sincrona: i callback hanno già usato l'origin
            done ({});
        });
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

// Ogni cambio (nostro o esterno) emette: la UI filtra il proprio origin. Coalescenza: un evento per
// cambio; setMods produce più callback (remove + N add) → la UI riceve N+1 eventi identici, tutti
// scartati perché origin coincide. Accettabile: gli array sono piccoli.
void StateChannel::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) { if (isOurs (t)) emitState (lastOrigin_); }
void StateChannel::valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&)             { if (isOurs (p)) emitState (lastOrigin_); }
void StateChannel::valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int)      { if (isOurs (p)) emitState (lastOrigin_); }

void StateChannel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // replaceState() ha sostituito l'albero radice: riaggancia il listener e notifica (origin vuoto = esterno).
    lastOrigin_.clear();
    listenTo (apvts_.state);
    emitState ({});
}
} // namespace bridge
