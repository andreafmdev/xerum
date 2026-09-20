#pragma once

#include "parameters/ParameterTable.h"

#include <cstddef>
#include <iterator>

namespace engine
{
/**
 * Le cinque sorgenti del mod matrix. Stessi nomi di ModSource in WebUI/src/juce/backend.ts.
 *
 * `env` e' l'inviluppo d'**ampiezza** riusato come modulatore, `env2` un secondo inviluppo che
 * non gata niente: esiste solo per essere modulato via. E' la differenza che rende esprimibile
 * il filtro che si apre e si richiude mentre la nota tiene — con il solo `env` la forma del
 * cutoff era costretta a seguire quella del volume.
 *
 * I valori numerici non finiscono da nessuna parte fuori dal processo: state::setMods salva
 * `src` come stringa, quindi l'ordine qui dentro e' libero e non e' un formato.
 */
enum class ModSource { lfo, env, env2, vel, mw, count };

/**
 * I target del mod matrix e i loro id testuali, generati da parameters.json (`"modTarget": true`,
 * vedi params::kModTargets in ParameterTable.h). Qui si importano e si verificano: che le due
 * liste siano lunghe uguali, che riga per riga parlino dello stesso parametro, e che siano tutti
 * Kind::Float — l'unico kind con un valore normalizzato 0..1 su cui modulated() puo' sommare.
 *
 * L'indice dentro kModTargets e' la valuta con cui il thread audio somma le modulazioni, e
 * indicizza EngineParams::modBase. La UI accetta il drop di una sorgente solo su questi knob
 * (MOD_TARGETS in params.generated.ts, stessa fonte); un'assegnazione salvata verso un target
 * fuori lista viene scartata da state::buildModSnapshot sul message thread e mostrata come
 * inerte nel tab Mod.
 */
using params::kModTargets;
using params::kModTargetIds;

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

// Chi riempie uno snapshot a partire dal ValueTree e' state::buildModSnapshot
// (Source/state/StateToEngine.h): il motore non conosce il formato dello stato salvato.
} // namespace engine
