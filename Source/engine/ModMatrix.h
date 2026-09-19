#pragma once

#include "parameters/ParamSlot.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstddef>
#include <iterator>

namespace engine
{
/** Le quattro sorgenti del mod matrix. Stessi nomi di ModSource in WebUI/src/juce/backend.ts. */
enum class ModSource { lfo, env, vel, mw, count };

/**
 * I sette parametri che il motore sa modulare, in ordine stabile: l'indice dentro questo array
 * e' la valuta con cui il thread audio somma le modulazioni, e indicizza EngineParams::modBase.
 *
 * La UI lascia trascinare una sorgente su qualunque knob; le assegnazioni verso un target fuori
 * da questa lista vengono scartate quando si costruisce lo snapshot, cioe' sul message thread.
 * Il thread audio non vede mai una route che non sa applicare.
 */
inline constexpr params::ParamSlot kModTargets[] = {
    params::ParamSlot::cutoff,
    params::ParamSlot::res,
    params::ParamSlot::wtpos,
    params::ParamSlot::level,
    params::ParamSlot::pan,
    params::ParamSlot::fine,
    params::ParamSlot::drive,
};

/**
 * Gli id testuali degli stessi target, nello stesso ordine: sono quelli che la UI manda nel
 * nodo MODS.
 *
 * Esiste una tabella a parte e non si ricava l'id da params::kTable indicizzata con lo slot,
 * perche' ParamSlot ha un ordine proprio, deciso qui, mentre kTable segue parameters.json:
 * i due ordini non coincidono e usare l'uno per indicizzare l'altra risolverebbe `cutoff` in
 * tutt'altro parametro. Lo static_assert sotto e' cio' che tiene le due liste allineate quando
 * qualcuno ne allunga una sola.
 */
inline constexpr const char* kModTargetIds[] = { "cutoff", "res", "wtpos", "level", "pan", "fine", "drive" };

static_assert (std::size (kModTargetIds) == std::size (kModTargets),
               "le due tabelle devono restare allineate");

inline constexpr int kNumModTargets = (int) std::size (kModTargets);

/** Posizione dentro kModTargets, -1 se quel parametro non e' modulabile. */
inline constexpr int modTargetIndexFor (params::ParamSlot slot) noexcept
{
    for (int i = 0; i < kNumModTargets; ++i)
        if (kModTargets[i] == slot)
            return i;

    return -1;
}

/**
 * Una assegnazione, con il target gia' risolto a indice: sul thread audio non resta niente da
 * cercare, si somma dentro un array di kNumModTargets elementi.
 */
struct ModRoute
{
    ModSource src { ModSource::lfo };
    int targetIndex { 0 };
    float depth { 0.0f }; // -1..1
};

/**
 * La lista completa delle assegnazioni, in un blocco di memoria a dimensione fissa: il thread
 * audio non alloca e non segue puntatori. La UI non impone un tetto al numero di assegnazioni,
 * quindi lo impone il motore.
 */
struct ModSnapshot
{
    static constexpr int kMaxRoutes = 32;

    ModRoute routes[kMaxRoutes] {};
    int count { 0 };
};

/**
 * Traduce il nodo MODS del ValueTree in uno snapshot. Message thread: qui si confrontano
 * stringhe, si scartano target e sorgenti sconosciute e si limita il depth. `out` viene
 * riscritto per intero, anche quando il nodo e' vuoto.
 */
void buildModSnapshot (const juce::ValueTree& modsNode, ModSnapshot& out);
} // namespace engine
