#pragma once

#include "parameters/ParameterTable.h"

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
 * Resta una tabella scritta a mano perche' e' leggibile accanto a kModTargets, non perche' sia
 * l'unica fonte: da quando ParamSlot e kSlotTableIndex sono generati da parameters.json,
 * params::specForSlot() sa risalire allo spec di uno slot (l'ordine di ParamSlot e quello di
 * kTable non coincidono, e' kSlotTableIndex a fare il ponte). Gli static_assert qui sotto
 * verificano entrambe le cose che contano: che le due liste siano lunghe uguali, e che riga per
 * riga parlino dello stesso parametro.
 */
inline constexpr const char* kModTargetIds[] = { "cutoff", "res", "wtpos", "level", "pan", "fine", "drive" };

namespace detail
{
constexpr bool sameId (const char* a, const char* b) noexcept
{
    while (*a != 0 && *a == *b) { ++a; ++b; }
    return *a == 0 && *b == 0;
}

/** Il primo indice in cui kModTargetIds[i] non e' l'id del parametro di kModTargets[i], -1 se nessuno. */
constexpr int firstMisalignedTarget() noexcept
{
    for (int i = 0; i < (int) std::size (kModTargets); ++i)
        if (! sameId (params::specForSlot (kModTargets[i]).id, kModTargetIds[i]))
            return i;

    return -1;
}

/** Il primo target che non e' Kind::Float, -1 se nessuno. */
constexpr int firstNonFloatTarget() noexcept
{
    for (int i = 0; i < (int) std::size (kModTargets); ++i)
        if (params::specForSlot (kModTargets[i]).kind != params::Kind::Float)
            return i;

    return -1;
}
} // namespace detail

static_assert (std::size (kModTargetIds) == std::size (kModTargets),
               "le due tabelle devono restare allineate");

// Non basta che siano lunghe uguali: allungarle entrambe di una riga sbagliata le lascerebbe
// allineate per conteggio e disallineate per contenuto, e il sintomo sarebbe una modulazione che
// arriva sul parametro sbagliato. Qui si confronta riga per riga, a tempo di compilazione.
static_assert (detail::firstMisalignedTarget() < 0,
               "kModTargetIds[i] non e' l'id del parametro in kModTargets[i]: le due tabelle sono disallineate");

/**
 * La guardia che manca al bug delle quattro ottave per potersi ripetere.
 *
 * SynthVoice::modulated() somma depth x livello sorgente al valore che trova in
 * EngineParams::modBase e clampa il risultato a 0..1, poi lo denormalizza: tutta quella
 * aritmetica assume che modBase sia *normalizzato*. E' vero solo per i Kind::Float, che
 * ParameterMapping.h crea con NormalisableRange {0, 1}. Aggiungere qui ParamSlot::oct — un
 * Kind::Int, che arriva in unita' naturali -3..3 — compilerebbe senza una parola e
 * riprodurrebbe il difetto identico: clamp a 0..1 di un valore naturale, cioe' il knob bloccato
 * sui due estremi.
 */
static_assert (detail::firstNonFloatTarget() < 0,
               "un target di kModTargets non e' Kind::Float: modBase e modulated() assumono un valore normalizzato 0..1");

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
