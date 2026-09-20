#include "state/StateToEngine.h"
#include "state/StateTree.h"

#include <algorithm>

namespace state
{
namespace
{
/** `src` testuale -> sorgente. Ritorna false se la UI ne manda una che questo motore non conosce. */
bool sourceFromString (const juce::String& id, engine::ModSource& out) noexcept
{
    if (id == "lfo") { out = engine::ModSource::lfo; return true; }
    if (id == "env") { out = engine::ModSource::env; return true; }
    // Confronto esatto, non un prefisso: "env" e "env2" sono due sorgenti diverse.
    if (id == "env2") { out = engine::ModSource::env2; return true; }
    if (id == "vel") { out = engine::ModSource::vel; return true; }
    if (id == "mw")  { out = engine::ModSource::mw;  return true; }
    return false;
}

/** `target` testuale -> indice in kModTargets, -1 se non modulabile o sconosciuto. */
int targetIndexFromString (const juce::String& id) noexcept
{
    for (int i = 0; i < engine::kNumModTargets; ++i)
        if (id == engine::kModTargetIds[i])
            return i;

    return -1;
}
} // namespace

void buildModSnapshot (const juce::ValueTree& modsNode, engine::ModSnapshot& out)
{
    // Riscrittura totale, non un diff: `out` e' uno slot riusato dell'anello, e quello che
    // resta dello snapshot precedente oltre `count` non deve poter riaffiorare.
    out.count = 0;

    for (const auto& m : modsNode)
    {
        if (out.count >= engine::ModSnapshot::kMaxRoutes)
            break;

        engine::ModSource src {};

        if (! sourceFromString (m[ids::src].toString(), src))
            continue;

        const auto targetIndex = targetIndexFromString (m[ids::target].toString());

        if (targetIndex < 0)
            continue;

        engine::ModRoute route;
        route.src = src;
        route.targetIndex = targetIndex;

        // Il depth arriva dalla UI e finisce nello stato persistito: un preset scritto a mano o
        // una versione futura della UI potrebbero mandarne uno fuori scala. Limitarlo qui evita
        // che il thread audio debba fidarsi di un dato che non controlla.
        route.depth = juce::jlimit (-1.0f, 1.0f, (float) (double) m[ids::depth]);

        out.routes[out.count++] = route;
    }
}

void buildArpSnapshot (const juce::ValueTree& arpNode, engine::ArpSnapshot& out)
{
    // Riscrittura totale, non un diff: `out` e' uno slot riusato dell'anello, e quello che resta
    // della sequenza precedente non deve poter riaffiorare da sotto una sequenza piu' corta.
    for (auto& s : out.steps)
        s = 0.0f;

    const auto tokens = juce::StringArray::fromTokens (arpNode[ids::steps].toString(), ",", "");

    for (int i = 0; i < std::min (engine::kArpSteps, tokens.size()); ++i)
    {
        // Il livello arriva dalla UI e finisce nello stato persistito: un preset scritto a mano o
        // una versione futura della UI potrebbero mandarne uno fuori scala. Limitarlo qui evita
        // che il thread audio debba fidarsi di un dato che non controlla — stesso criterio del
        // depth in buildModSnapshot.
        out.steps[i] = juce::jlimit (0.0f, 1.0f, (float) tokens[i].getDoubleValue());
    }
}
} // namespace state
