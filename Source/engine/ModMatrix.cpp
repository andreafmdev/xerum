#include "engine/ModMatrix.h"
#include "parameters/StateTree.h"

namespace engine
{
namespace
{
/** `src` testuale -> sorgente. Ritorna false se la UI ne manda una che questo motore non conosce. */
bool sourceFromString (const juce::String& id, ModSource& out) noexcept
{
    if (id == "lfo") { out = ModSource::lfo; return true; }
    if (id == "env") { out = ModSource::env; return true; }
    if (id == "vel") { out = ModSource::vel; return true; }
    if (id == "mw")  { out = ModSource::mw;  return true; }
    return false;
}

/** `target` testuale -> indice in kModTargets, -1 se non modulabile o sconosciuto. */
int targetIndexFromString (const juce::String& id) noexcept
{
    for (int i = 0; i < kNumModTargets; ++i)
        if (id == kModTargetIds[i])
            return i;

    return -1;
}
} // namespace

void buildModSnapshot (const juce::ValueTree& modsNode, ModSnapshot& out)
{
    // Riscrittura totale, non un diff: `out` e' uno slot riusato dell'anello, e quello che
    // resta dello snapshot precedente oltre `count` non deve poter riaffiorare.
    out.count = 0;

    for (const auto& m : modsNode)
    {
        if (out.count >= ModSnapshot::kMaxRoutes)
            break;

        ModSource src {};

        if (! sourceFromString (m[state::ids::src].toString(), src))
            continue;

        const auto targetIndex = targetIndexFromString (m[state::ids::target].toString());

        if (targetIndex < 0)
            continue;

        ModRoute route;
        route.src = src;
        route.targetIndex = targetIndex;

        // Il depth arriva dalla UI e finisce nello stato persistito: un preset scritto a mano o
        // una versione futura della UI potrebbero mandarne uno fuori scala. Limitarlo qui evita
        // che il thread audio debba fidarsi di un dato che non controlla.
        route.depth = juce::jlimit (-1.0f, 1.0f, (float) (double) m[state::ids::depth]);

        out.routes[out.count++] = route;
    }
}
} // namespace engine
