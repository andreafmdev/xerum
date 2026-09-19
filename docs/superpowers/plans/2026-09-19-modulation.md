# Mod Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cablare nel motore audio il mod matrix che esiste già nello stato, nel bridge e nella UI, così che le assegnazioni `sorgente → parametro` fatte trascinando un chip nella WebUI producano davvero modulazione.

**Architecture:** Il message thread traduce il nodo `MODS` del ValueTree in uno `ModSnapshot` con i target già risolti a indici interi, e lo pubblica al thread audio con un anello di 4 buffer preallocati più un puntatore atomico. Ogni `SynthVoice` calcola una volta per blocco i livelli delle quattro sorgenti (`lfo`, `env`, `vel`, `mw`), somma `depth × livello` sul valore **normalizzato** di ciascun target, clampa a 0..1 e poi denormalizza con la stessa funzione che usa il percorso non modulato.

**Tech Stack:** C++17, JUCE 8 (juce_dsp, juce_audio_basics, juce_audio_processors), CMake, `juce::UnitTest`. Lato UI: TypeScript, Vitest.

**Spec:** `docs/superpowers/specs/2026-09-19-modulation-design.md`

## Global Constraints

- **Nessuna modifica** a `Source/parameters/parameters.json`, `Source/parameters/presets.json`, `Source/parameters/StateTree.h`, `Source/bridge/StateChannel.{h,cpp}`, `WebUI/src/juce/backend.ts`. Il contratto di stato esiste già ed è versionato (`state::kVersion = 1`).
- **Thread audio**: nessuna allocazione, nessun lock, nessuna I/O, nessun confronto di stringhe, nessuna chiamata a libm per campione. `std::pow`, `std::tan`, `std::sqrt` sono ammessi una volta per blocco.
- **Commenti in italiano**, come tutto il resto del codebase. Spiegano il *perché*, non il *cosa*.
- La formula di modulazione deve coincidere con `liveValue` di `WebUI/src/synth/mod.ts`: somma sul normalizzato, `clamp01`, poi denormalizzazione. `lfo` è bipolare −1..1; `env`, `vel`, `mw` sono unipolari 0..1.
- Le divisioni di sync sono quelle di `WebUI/src/synth/ui/Tabs.tsx:82`: `["1/16", "1/8", "1/4", "1/2", "1", "2"]`, indice `min(5, floor(raw * 6))`.
- Fallback tempo quando l'host non espone un BPM: **120**.
- Build e test: `cmake -S . -B build/macos-debug -DCMAKE_BUILD_TYPE=Debug` poi `cmake --build build/macos-debug --target XerumTests -j8`; il binario esce in `build/macos-debug/XerumTests_artefacts/XerumTests` (o sottocartella `Debug/`) e stampa `ALL TESTS PASSED`.
- Test WebUI: `cd WebUI && pnpm test`.

---

### Task 1: `ParamSlot` in un header proprio, e le sette conversioni estratte

Oggi `ParamSlot` vive dentro `ParamCollect.h` insieme al template `collectEngineParams`, e le conversioni da valore normalizzato a valore reale sono scritte in linea dentro quel template. La modulazione deve applicare **le stesse** conversioni: se restano in linea, i due percorsi divergeranno alla prima modifica.

**Files:**
- Create: `Source/parameters/ParamSlot.h`
- Modify: `Source/parameters/ParamCollect.h`
- Test: `Tests/PresetValueTests.cpp`

**Interfaces:**
- Consumes: niente.
- Produces: `params::ParamSlot` (enum invariato, solo spostato); `params::cutoffHzFromRaw`, `resonanceQFromRaw`, `framePositionFromRaw`, `levelGainFromRaw`, `panFromRaw`, `fineCentsFromRaw`, `driveGainFromRaw`, tutte `inline float f (float raw) noexcept`.

- [ ] **Step 1: Scrivere il test che fallisce**

In `Tests/PresetValueTests.cpp`, dentro il `runTest()` esistente:

```cpp
beginTest ("le conversioni estratte coincidono con quelle di collectEngineParams");
{
    // Griglia fitta: se una formula estratta diverge anche solo agli estremi, qui si vede.
    for (int i = 0; i <= 20; ++i)
    {
        const auto raw = (float) i / 20.0f;

        const auto p = params::collectEngineParams (
            [raw] (params::ParamSlot) noexcept { return raw; });

        expectWithinAbsoluteError (params::cutoffHzFromRaw (raw), p.cutoffHz, 1.0e-3f);
        expectWithinAbsoluteError (params::resonanceQFromRaw (raw), p.resonanceQ, 1.0e-4f);
        expectWithinAbsoluteError (params::framePositionFromRaw (raw), p.framePosition, 1.0e-6f);
        expectWithinAbsoluteError (params::levelGainFromRaw (raw), p.level, 1.0e-6f);
        expectWithinAbsoluteError (params::panFromRaw (raw), p.pan, 1.0e-6f);
        expectWithinAbsoluteError (params::fineCentsFromRaw (raw), p.fineCents, 1.0e-3f);
        expectWithinAbsoluteError (params::driveGainFromRaw (raw), p.driveGain, 1.0e-4f);
    }
}
```

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Run: `cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL in compilazione, `no member named 'cutoffHzFromRaw' in namespace 'params'`.

- [ ] **Step 3: Creare `Source/parameters/ParamSlot.h`**

```cpp
#pragma once

namespace params
{
/**
 * Identifica un parametro grezzo senza passare per il suo nome: chi implementa l'accessore
 * risolve `id -> puntatore` una volta sola alla costruzione (vedi PluginProcessor::paramSlots_),
 * e qui dentro e' solo un indice di array. Ordine arbitrario ma stabile: e' un dettaglio interno
 * fra questo header e chi scrive l'accessore, non un ABI pubblico.
 *
 * Vive in un header proprio, separato da ParamCollect.h, perche' engine/ModMatrix.h ha bisogno
 * dell'enum e non del template collectEngineParams con tutte le sue dipendenze.
 */
enum class ParamSlot : int
{
    oscOn, wtpos, oct, semi, fine, level, filtOn, ftype, slope, cutoff,
    res, drive, keytrk, att, dec, sus, rel, envVel, pan, bypass,
    lshape, lrate, lsync, lphase, lfade, lretrig,
    count
};
} // namespace params
```

I sei slot dell'LFO sono nuovi: i parametri esistono già in `parameters.json`, mancava solo chi li leggesse.

- [ ] **Step 4: Estrarre le conversioni in `ParamCollect.h`**

Togliere l'`enum class ParamSlot` dal file, aggiungere `#include "parameters/ParamSlot.h"`, e sopra `collectEngineParams` inserire:

```cpp
/**
 * Da valore normalizzato 0..1 a valore reale, un target per funzione.
 *
 * Esistono come funzioni e non in linea dentro collectEngineParams perche' il percorso
 * modulato (SynthVoice) deve applicare esattamente la stessa aritmetica: due copie della
 * stessa formula divergono alla prima modifica, e il sintomo sarebbe un cutoff che finisce
 * altrove a seconda che lo muova un LFO o la mano dell'utente.
 */
inline float cutoffHzFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("cutoff");
    static_assert (spec != nullptr, "cutoff non e' in ParameterTable.h");
    return params::denormalise (*spec, raw);
}

inline float resonanceQFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("res");
    static_assert (spec != nullptr, "res non e' in ParameterTable.h");

    // res 0..100 % -> Q, esponenziale da Butterworth (0.707) a 12: Q e' percepito in rapporti,
    // con una mappa lineare la meta' bassa della corsa era gia' tutta risonante.
    return dsp::StateVariableFilter::kButterworthQ
               * std::pow (12.0f / dsp::StateVariableFilter::kButterworthQ,
                           params::denormalise (*spec, raw) * 0.01f);
}

/** wtpos e' gia' 0..1 sul set di frame: nessuna denormalizzazione. */
inline float framePositionFromRaw (float raw) noexcept { return params::clamp01 (raw); }

/** `level` ha mappa Db ma il valore grezzo e' gia' il guadagno lineare (vedi
    WebUI/src/synth/mapping.ts): denormalise() qui darebbe un dB, sbagliato. */
inline float levelGainFromRaw (float raw) noexcept { return params::clamp01 (raw); }

inline float panFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("pan");
    static_assert (spec != nullptr, "pan non e' in ParameterTable.h");
    return params::denormalise (*spec, raw) * 0.02f; // -50..50 -> -1..1
}

inline float fineCentsFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("fine");
    static_assert (spec != nullptr, "fine non e' in ParameterTable.h");
    return params::denormalise (*spec, raw);
}

inline float driveGainFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("drive");
    static_assert (spec != nullptr, "drive non e' in ParameterTable.h");
    return juce::Decibels::decibelsToGain (params::denormalise (*spec, raw));
}
```

Poi sostituire dentro `collectEngineParams` le righe corrispondenti con le chiamate a queste funzioni: `p.cutoffHz = cutoffHzFromRaw (rawFor (ParamSlot::cutoff));` e così per le altre sei. Le `constexpr auto* specCutoff` ecc. ormai inutilizzate vanno rimosse insieme al loro `static_assert`; quelle ancora usate (`specOct`, `specSemi`, `specAtt`, `specDec`, `specSus`, `specRel`, `specEnvVel`, `specKeytrk`) restano.

- [ ] **Step 5: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`. Il refactoring è a comportamento invariato, quindi **tutti** i test preesistenti devono passare senza essere toccati.

- [ ] **Step 6: Commit**

```bash
git add Source/parameters/ParamSlot.h Source/parameters/ParamCollect.h Tests/PresetValueTests.cpp
git commit -m "Extract ParamSlot and the per-target raw conversions"
```

---

### Task 2: `ModMatrix.h` e la costruzione dello snapshot

**Files:**
- Create: `Source/engine/ModMatrix.h`, `Source/engine/ModMatrix.cpp`
- Modify: `CMakeLists.txt` (aggiungere `Source/engine/ModMatrix.cpp` a entrambi i target: `SerumStyleSynth` e `XerumTests`)
- Test: `Tests/ModMatrixTests.cpp` (nuovo, da aggiungere ai sorgenti di `XerumTests`)

**Interfaces:**
- Consumes: `params::ParamSlot` dal Task 1.
- Produces: `engine::ModSource`, `engine::ModRoute { ModSource src; int targetIndex; float depth; }`, `engine::ModSnapshot` con `kMaxRoutes = 32`, `engine::kModTargets[]`, `engine::kNumModTargets = 7`, `engine::modTargetIndexFor (params::ParamSlot)`, `engine::buildModSnapshot (const juce::ValueTree&, ModSnapshot&)`.

Nota sul progetto rispetto alla spec: `ModRoute` porta un **`int targetIndex`** (posizione dentro `kModTargets`), non uno `ParamSlot`. La risoluzione avviene una volta sola alla costruzione dello snapshot, sul message thread, così il thread audio somma in un array senza nemmeno cercare quale target sia.

- [ ] **Step 1: Scrivere il test che fallisce**

Creare `Tests/ModMatrixTests.cpp`:

```cpp
#include "engine/ModMatrix.h"
#include "parameters/StateTree.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace
{
/** Costruisce un nodo MODS con le assegnazioni date, come farebbe state::setMods. */
juce::ValueTree makeMods (const std::vector<std::tuple<juce::String, juce::String, double>>& routes)
{
    juce::ValueTree mods { state::ids::MODS };

    for (const auto& [src, target, depth] : routes)
    {
        juce::ValueTree node { state::ids::MOD };
        node.setProperty (state::ids::src, src, nullptr);
        node.setProperty (state::ids::target, target, nullptr);
        node.setProperty (state::ids::depth, depth, nullptr);
        mods.appendChild (node, nullptr);
    }

    return mods;
}
} // namespace

struct ModMatrixTests final : juce::UnitTest
{
    ModMatrixTests() : juce::UnitTest ("ModMatrix", "engine") {}

    void runTest() override
    {
        beginTest ("una route valida finisce nello snapshot con il target risolto");
        {
            engine::ModSnapshot snap;
            engine::buildModSnapshot (makeMods ({ { "env", "cutoff", 0.75 } }), snap);

            expectEquals (snap.count, 1);
            expect (snap.routes[0].src == engine::ModSource::env);
            expectEquals (snap.routes[0].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::cutoff));
            expectWithinAbsoluteError (snap.routes[0].depth, 0.75f, 1.0e-6f);
        }

        beginTest ("un target che il motore non sa modulare viene scartato");
        {
            engine::ModSnapshot snap;
            // `att` e' un parametro vero ma non e' fra i sette target modulabili.
            engine::buildModSnapshot (makeMods ({ { "lfo", "att", 1.0 },
                                                  { "lfo", "wtpos", 0.5 } }), snap);

            expectEquals (snap.count, 1);
            expectEquals (snap.routes[0].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::wtpos));
        }

        beginTest ("una sorgente sconosciuta viene scartata");
        {
            engine::ModSnapshot snap;
            engine::buildModSnapshot (makeMods ({ { "aftertouch", "cutoff", 1.0 } }), snap);
            expectEquals (snap.count, 0);
        }

        beginTest ("oltre kMaxRoutes le assegnazioni in eccesso si scartano senza crescere");
        {
            std::vector<std::tuple<juce::String, juce::String, double>> many;
            for (int i = 0; i < engine::ModSnapshot::kMaxRoutes + 5; ++i)
                many.emplace_back ("lfo", "cutoff", 0.1);

            engine::ModSnapshot snap;
            engine::buildModSnapshot (makeMods (many), snap);
            expectEquals (snap.count, engine::ModSnapshot::kMaxRoutes);
        }

        beginTest ("depth fuori scala viene limitato a -1..1");
        {
            engine::ModSnapshot snap;
            engine::buildModSnapshot (makeMods ({ { "vel", "level", 4.2 },
                                                  { "vel", "pan", -9.0 } }), snap);

            expectEquals (snap.count, 2);
            expectWithinAbsoluteError (snap.routes[0].depth, 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (snap.routes[1].depth, -1.0f, 1.0e-6f);
        }

        beginTest ("un nodo MODS vuoto produce uno snapshot vuoto");
        {
            engine::ModSnapshot snap;
            snap.count = 7; // sporco di proposito: buildModSnapshot deve azzerarlo
            engine::buildModSnapshot (makeMods ({}), snap);
            expectEquals (snap.count, 0);
        }
    }
};

static ModMatrixTests modMatrixTests;
```

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Aggiungere prima `Tests/ModMatrixTests.cpp` e `Source/engine/ModMatrix.cpp` a `target_sources(XerumTests ...)` in `CMakeLists.txt`, e `Source/engine/ModMatrix.cpp` anche a `target_sources(SerumStyleSynth ...)`. `XerumTests` linka già `juce::juce_audio_basics`; serve in più `juce::juce_data_structures` per `juce::ValueTree` — aggiungerlo a `target_link_libraries(XerumTests PRIVATE ...)`.

Run: `cmake -S . -B build/macos-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL, `'engine/ModMatrix.h' file not found`.

- [ ] **Step 3: Scrivere `Source/engine/ModMatrix.h`**

```cpp
#pragma once

#include "parameters/ParamSlot.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstddef>

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
```

- [ ] **Step 4: Scrivere `Source/engine/ModMatrix.cpp`**

```cpp
#include "engine/ModMatrix.h"
#include "parameters/ParameterTable.h"
#include "parameters/StateTree.h"

#include <algorithm>
#include <cstring>

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
    {
        // kTable e kModTargets sono indicizzati diversamente: si confronta l'id del parametro
        // che sta nello slot, prendendolo dalla tabella generata.
        const auto slot = (int) kModTargets[i];

        if (slot < params::kNumParams && id == params::kTable[slot].id)
            return i;
    }

    return -1;
}
} // namespace

void buildModSnapshot (const juce::ValueTree& modsNode, ModSnapshot& out)
{
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
        route.depth = juce::jlimit (-1.0f, 1.0f, (float) (double) m[state::ids::depth]);

        out.routes[out.count++] = route;
    }
}
} // namespace engine
```

**Attenzione a `targetIndexFromString`:** presuppone che `(int) ParamSlot::cutoff` sia anche l'indice di `cutoff` in `params::kTable`. Non è vero: `ParamSlot` ha un ordine proprio. Verificare e, se non coincidono, sostituire il corpo con una tabella esplicita `{ ParamSlot, const char* }` accanto a `kModTargets`, che è comunque la scelta più leggibile:

```cpp
inline constexpr const char* kModTargetIds[] = { "cutoff", "res", "wtpos", "level", "pan", "fine", "drive" };
static_assert (std::size (kModTargetIds) == std::size (kModTargets), "le due tabelle devono restare allineate");
```

e poi confrontare `id == kModTargetIds[i]`. Usare questa versione: è quella che il test del Task 2 esercita ed è immune all'ordine di `ParamSlot`.

- [ ] **Step 5: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`, con i sei `beginTest` di `ModMatrix` verdi.

- [ ] **Step 6: Commit**

```bash
git add Source/engine/ModMatrix.h Source/engine/ModMatrix.cpp Tests/ModMatrixTests.cpp CMakeLists.txt
git commit -m "Add ModSnapshot and build it from the MODS value tree"
```

---

### Task 3: il modulo LFO

**Files:**
- Create: `Source/dsp/Lfo.h`, `Source/dsp/Lfo.cpp`
- Modify: `CMakeLists.txt` (`Source/dsp/Lfo.cpp` in entrambi i target)
- Test: `Tests/LfoTests.cpp` (nuovo, da aggiungere ai sorgenti di `XerumTests`)
- Modify: `WebUI/src/synth/mod.test.ts` (tabella di parità condivisa)

**Interfaces:**
- Consumes: niente.
- Produces: `dsp::Lfo` con `Shape { sine, triangle, saw, square, sampleHold }`, `prepare (double)`, `setShape (Shape)`, `setFrequencyHz (float)`, `setFadeSeconds (float)`, `retrigger (float startPhase01)`, `advance (int numSamples)` → `float` in −1..1, `level() const`; più le funzioni libere `dsp::lfoShapeValue (Lfo::Shape, float phase01)` e `dsp::syncedRateHz (float raw, double bpm)`.

- [ ] **Step 1: Scrivere il test che fallisce**

Creare `Tests/LfoTests.cpp`:

```cpp
#include "dsp/Lfo.h"

#include <juce_core/juce_core.h>

#include <cmath>

struct LfoTests final : juce::UnitTest
{
    LfoTests() : juce::UnitTest ("Lfo", "dsp") {}

    void runTest() override
    {
        beginTest ("le forme d'onda coincidono con lfoShape di WebUI/src/synth/mod.ts");
        {
            // Stessi valori attesi scritti anche in WebUI/src/synth/mod.test.ts: se una delle due
            // implementazioni cambia, uno dei due test si accorge della divergenza.
            const float phases[] = { 0.0f, 0.125f, 0.25f, 0.5f, 0.75f, 0.999f };

            for (auto ph : phases)
            {
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::sine, ph),
                                           std::sin (ph * 2.0f * juce::MathConstants<float>::pi), 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::triangle, ph),
                                           1.0f - 4.0f * std::abs (ph - 0.5f), 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::saw, ph),
                                           1.0f - 2.0f * ph, 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::square, ph),
                                           ph < 0.5f ? 1.0f : -1.0f, 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::sampleHold, ph),
                                           std::sin (std::floor (ph * 8.0f) * 7.3f), 1.0e-5f);
            }
        }

        beginTest ("la fase avvolge e resta in -1..1 qualunque sia la frequenza");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::sine);

            for (float hz : { 0.05f, 1.0f, 20.0f, 1.0e6f, -5.0f })
            {
                lfo.setFrequencyHz (hz);
                lfo.retrigger (0.0f);

                for (int b = 0; b < 100; ++b)
                {
                    const auto v = lfo.advance (128);
                    expect (std::isfinite (v), "livello non finito");
                    expect (v >= -1.0001f && v <= 1.0001f, "livello fuori da -1..1");
                }
            }
        }

        beginTest ("a 1 Hz un ciclo dura esattamente un secondo");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::saw);
            lfo.setFrequencyHz (1.0f);
            lfo.retrigger (0.0f);

            // Saw parte da 1 e scende a -1: dopo mezzo secondo deve valere circa 0.
            lfo.advance (24000);
            expectWithinAbsoluteError (lfo.level(), 0.0f, 0.01f);

            lfo.advance (24000);
            expectWithinAbsoluteError (lfo.level(), 1.0f, 0.01f);
        }

        beginTest ("retrigger riparte dall'offset di fase richiesto");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::saw);
            lfo.setFrequencyHz (1.0f);

            lfo.retrigger (0.0f);
            expectWithinAbsoluteError (lfo.level(), 1.0f, 1.0e-4f);

            lfo.advance (12000);
            lfo.retrigger (0.5f); // meta' ciclo: saw vale -0
            expectWithinAbsoluteError (lfo.level(), 0.0f, 1.0e-4f);
        }

        beginTest ("la dissolvenza in entrata scala il livello dal note-on");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::square); // ampiezza costante 1: isola la sola fade
            lfo.setFrequencyHz (0.05f);             // periodo 20 s: resta nella prima meta'
            lfo.setFadeSeconds (1.0f);
            lfo.retrigger (0.0f);

            expectWithinAbsoluteError (lfo.level(), 0.0f, 1.0e-4f);

            lfo.advance (24000); // mezzo secondo
            expectWithinAbsoluteError (lfo.level(), 0.5f, 0.02f);

            lfo.advance (24000); // un secondo: fade completa
            expectWithinAbsoluteError (lfo.level(), 1.0f, 0.02f);

            lfo.advance (48000); // e non supera 1
            expectWithinAbsoluteError (lfo.level(), 1.0f, 0.02f);
        }

        beginTest ("fade a zero secondi significa nessuna dissolvenza");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::square);
            lfo.setFrequencyHz (0.05f);
            lfo.setFadeSeconds (0.0f);
            lfo.retrigger (0.0f);

            expectWithinAbsoluteError (lfo.level(), 1.0f, 1.0e-4f);
        }

        beginTest ("sync: le divisioni sono quelle di Tabs.tsx e seguono il tempo dell'host");
        {
            // ["1/16", "1/8", "1/4", "1/2", "1", "2"], indice min(5, floor(raw * 6)).
            // A 120 BPM un quarto dura mezzo secondo -> 2 Hz.
            expectWithinAbsoluteError (dsp::syncedRateHz (2.0f / 6.0f + 0.01f, 120.0), 2.0f, 1.0e-3f);
            // 1/16 = un quarto di quarto -> 8 Hz.
            expectWithinAbsoluteError (dsp::syncedRateHz (0.0f, 120.0), 8.0f, 1.0e-3f);
            // "2" = due battute da quattro quarti = 8 quarti -> 0.25 Hz.
            expectWithinAbsoluteError (dsp::syncedRateHz (1.0f, 120.0), 0.25f, 1.0e-3f);
            // Il tempo scala tutto linearmente.
            expectWithinAbsoluteError (dsp::syncedRateHz (0.0f, 60.0), 4.0f, 1.0e-3f);
        }
    }
};

static LfoTests lfoTests;
```

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Aggiungere `Tests/LfoTests.cpp` e `Source/dsp/Lfo.cpp` a `CMakeLists.txt` come nel Task 2.
Run: `cmake -S . -B build/macos-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL, `'dsp/Lfo.h' file not found`.

- [ ] **Step 3: Scrivere `Source/dsp/Lfo.h`**

```cpp
#pragma once

namespace dsp
{
/**
 * LFO a tasso di controllo: avanza una volta per blocco, non per campione.
 *
 * E' una scelta, non una semplificazione mancata. Cutoff e Position gia' si aggiornano una
 * volta per blocco perche' costano tan() e ricalcolo degli indici di frame; far girare l'LFO
 * per campione non renderebbe piu' fine la loro modulazione. A 48 kHz con blocchi da 128
 * campioni il tasso di aggiornamento e' 375 Hz, abbondante per un LFO che arriva a 20 Hz.
 * Il rovescio della medaglia: FM e AM audio-rate non sono esprimibili, e non sono un obiettivo.
 */
class Lfo
{
public:
    enum class Shape { sine, triangle, saw, square, sampleHold };

    void prepare (double sampleRate) noexcept;

    void setShape (Shape shape) noexcept { shape_ = shape; }
    void setFrequencyHz (float hz) noexcept;

    /** Durata della dissolvenza in entrata, dal retrigger. Zero: nessuna dissolvenza. */
    void setFadeSeconds (float seconds) noexcept;

    /** Riparte da `startPhase01` e azzera la dissolvenza. */
    void retrigger (float startPhase01) noexcept;

    /** Avanza di `numSamples` e ritorna il livello, -1..1, dissolvenza inclusa. */
    float advance (int numSamples) noexcept;

    /** Il livello corrente, senza avanzare. */
    float level() const noexcept { return level_; }

private:
    void updateLevel() noexcept;

    double sampleRate_ { 48000.0 };
    double phase_ { 0.0 };          // 0..1
    double phaseIncrement_ { 0.0 }; // per campione
    Shape shape_ { Shape::sine };

    double fadeSamples_ { 0.0 };
    double fadeProgress_ { 1.0 };   // 0..1
    float level_ { 0.0f };
};

/** Le stesse formule di lfoShape in WebUI/src/synth/mod.ts. */
float lfoShapeValue (Lfo::Shape shape, float phase01) noexcept;

/** Hz della divisione ritmica selezionata da `raw` (0..1) al tempo dato, in BPM. */
float syncedRateHz (float raw, double bpm) noexcept;
} // namespace dsp
```

- [ ] **Step 4: Scrivere `Source/dsp/Lfo.cpp`**

```cpp
#include "dsp/Lfo.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr float kTwoPi = 6.283185307179586f;

/** Quanti quarti dura un ciclo, per ognuna delle sei divisioni di Tabs.tsx. */
constexpr double kBeatsPerCycle[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
} // namespace

float lfoShapeValue (Lfo::Shape shape, float phase01) noexcept
{
    const auto ph = phase01 - std::floor (phase01); // wrap anche per fasi negative

    switch (shape)
    {
        case Lfo::Shape::sine:       return std::sin (ph * kTwoPi);
        case Lfo::Shape::triangle:   return 1.0f - 4.0f * std::abs (ph - 0.5f);
        case Lfo::Shape::saw:        return 1.0f - 2.0f * ph;
        case Lfo::Shape::square:     return ph < 0.5f ? 1.0f : -1.0f;
        case Lfo::Shape::sampleHold: break;
    }

    // Non e' un vero sample & hold: e' il pattern pseudo-casuale deterministico che la UI
    // disegna (mod.ts). Riprodurlo identico e' cio' che tiene il puntino del tab LFO allineato
    // con quello che si sente.
    return std::sin (std::floor (ph * 8.0f) * 7.3f);
}

float syncedRateHz (float raw, double bpm) noexcept
{
    const auto clamped = std::clamp (raw, 0.0f, 1.0f);
    const auto index = std::min (5, (int) std::floor ((double) clamped * 6.0));
    const auto tempo = bpm > 0.0 ? bpm : 120.0;

    return (float) (tempo / 60.0 / kBeatsPerCycle[index]);
}

void Lfo::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    retrigger (0.0f);
}

void Lfo::setFrequencyHz (float hz) noexcept
{
    // Il passo per campione non puo' superare 1: oltre, l'avvolgimento di advance() non
    // riporterebbe la fase in [0, 1) e il livello sarebbe indefinito. Limitarlo qui costa un
    // confronto e rende impossibile ogni sorpresa a valle.
    phaseIncrement_ = std::clamp ((double) hz / sampleRate_, -1.0, 1.0);
}

void Lfo::setFadeSeconds (float seconds) noexcept
{
    fadeSamples_ = std::max (0.0, (double) seconds * sampleRate_);
}

void Lfo::retrigger (float startPhase01) noexcept
{
    phase_ = (double) (startPhase01 - std::floor (startPhase01));
    fadeProgress_ = fadeSamples_ > 0.0 ? 0.0 : 1.0;
    updateLevel();
}

float Lfo::advance (int numSamples) noexcept
{
    if (numSamples <= 0)
        return level_;

    phase_ += phaseIncrement_ * (double) numSamples;
    phase_ -= std::floor (phase_);

    if (fadeSamples_ > 0.0 && fadeProgress_ < 1.0)
        fadeProgress_ = std::min (1.0, fadeProgress_ + (double) numSamples / fadeSamples_);

    updateLevel();
    return level_;
}

void Lfo::updateLevel() noexcept
{
    const auto fade = fadeSamples_ > 0.0 ? (float) fadeProgress_ : 1.0f;
    level_ = lfoShapeValue (shape_, (float) phase_) * fade;
}
} // namespace dsp
```

- [ ] **Step 5: Aggiungere la tabella di parità nel test TypeScript**

In `WebUI/src/synth/mod.test.ts`, aggiungere:

```ts
// Stessi valori attesi del test C++ in Tests/LfoTests.cpp ("le forme d'onda coincidono con
// lfoShape di WebUI/src/synth/mod.ts"). Se una delle due implementazioni cambia, uno dei due
// test se ne accorge.
describe("parità con il DSP", () => {
  const PHASES = [0, 0.125, 0.25, 0.5, 0.75, 0.999];

  it("sine, tri, saw, square, S&H seguono le formule condivise", () => {
    for (const ph of PHASES) {
      expect(lfoShape("Sine", ph)).toBeCloseTo(Math.sin(ph * Math.PI * 2), 5);
      expect(lfoShape("Tri", ph)).toBeCloseTo(1 - 4 * Math.abs(ph - 0.5), 5);
      expect(lfoShape("Saw", ph)).toBeCloseTo(1 - 2 * ph, 5);
      expect(lfoShape("Square", ph)).toBeCloseTo(ph < 0.5 ? 1 : -1, 5);
      expect(lfoShape("S&H", ph)).toBeCloseTo(Math.sin(Math.floor(ph * 8) * 7.3), 5);
    }
  });
});
```

- [ ] **Step 6: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`.
Run: `cd WebUI && pnpm test`
Expected: tutti i test verdi.

- [ ] **Step 7: Commit**

```bash
git add Source/dsp/Lfo.h Source/dsp/Lfo.cpp Tests/LfoTests.cpp WebUI/src/synth/mod.test.ts CMakeLists.txt
git commit -m "Add the control-rate LFO module"
```

---

### Task 4: il livello corrente dell'inviluppo

**PREREQUISITO:** questo task tocca `Source/dsp/ADSREnvelope.h`, che è in lavorazione da un altro agent (fix del bug `sustain = 0`). **Non iniziare** finché quel lavoro non è rientrato nel working tree e i suoi test non sono verdi.

**Files:**
- Modify: `Source/dsp/ADSREnvelope.h`
- Test: `Tests/EnvelopeFilterTests.cpp`

**Interfaces:**
- Consumes: niente.
- Produces: `float dsp::ADSREnvelope::getLevel() const noexcept`.

- [ ] **Step 1: Scrivere il test che fallisce**

In `Tests/EnvelopeFilterTests.cpp`:

```cpp
beginTest ("getLevel() ritorna il livello corrente senza avanzare l'inviluppo");
{
    dsp::ADSREnvelope env;
    env.prepare (48000.0);
    env.setAttackSeconds (0.1f);
    env.setDecaySeconds (0.1f);
    env.setSustainLevel (0.5f);
    env.setReleaseSeconds (0.1f);

    expectWithinAbsoluteError (env.getLevel(), 0.0f, 1.0e-6f);

    env.noteOn (1.0f);

    for (int i = 0; i < 2400; ++i) // 50 ms: a meta' dell'attacco
        env.getNextSample();

    const auto snapshot = env.getLevel();
    expect (snapshot > 0.0f && snapshot < 1.0f, "l'inviluppo dovrebbe essere a meta' attacco");

    // Chiamarla due volte di fila non cambia niente: e' una lettura, non un passo.
    expectWithinAbsoluteError (env.getLevel(), snapshot, 1.0e-9f);
}
```

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Run: `cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL in compilazione, `no member named 'getLevel'`.

- [ ] **Step 3: Aggiungere l'accessore**

In `Source/dsp/ADSREnvelope.h`, accanto a `isActive()`:

```cpp
    /** Il livello corrente 0..1, senza avanzare. Serve al mod matrix: `env` come sorgente e'
        questo inviluppo riusato come modulatore (vedi docs/superpowers/specs/2026-09-19-modulation-design.md). */
    float getLevel() const noexcept { return level_; }
```

- [ ] **Step 4: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Source/dsp/ADSREnvelope.h Tests/EnvelopeFilterTests.cpp
git commit -m "Expose the envelope's current level for use as a mod source"
```

---

### Task 5: `EngineParams` porta le basi normalizzate, il tempo e il mod wheel

**Files:**
- Modify: `Source/engine/EngineParams.h`, `Source/parameters/ParamCollect.h`
- Test: `Tests/EngineTests.cpp`

**Interfaces:**
- Consumes: `engine::kNumModTargets`, `engine::ModSnapshot` (Task 2); `params::ParamSlot` esteso (Task 1).
- Produces: in `engine::EngineParams` i campi `std::array<float, engine::kNumModTargets> modBase`, `const engine::ModSnapshot* mods`, `float bpm`, `float modWheel`, `float globalLfoLevel`, e il blocco LFO `int lfoShapeIndex; float lfoRateRaw; bool lfoSync; float lfoPhaseOffset01; float lfoFadeSeconds; bool lfoRetrig;`.

- [ ] **Step 1: Scrivere il test che fallisce**

In `Tests/EngineTests.cpp`, nella suite che esercita `collectEngineParams` con l'accessore finto:

```cpp
beginTest ("collectEngineParams riempie le basi normalizzate dei target modulabili");
{
    // Ogni slot ritorna un valore diverso, cosi' uno scambio fra due target si vede.
    const auto rawFor = [] (params::ParamSlot slot) noexcept
    {
        switch (slot)
        {
            case params::ParamSlot::cutoff: return 0.11f;
            case params::ParamSlot::res:    return 0.22f;
            case params::ParamSlot::wtpos:  return 0.33f;
            case params::ParamSlot::level:  return 0.44f;
            case params::ParamSlot::pan:    return 0.55f;
            case params::ParamSlot::fine:   return 0.66f;
            case params::ParamSlot::drive:  return 0.77f;
            default:                        return 0.5f;
        }
    };

    const auto p = params::collectEngineParams (rawFor);

    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::cutoff)], 0.11f, 1.0e-6f);
    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::res)],    0.22f, 1.0e-6f);
    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::wtpos)],  0.33f, 1.0e-6f);
    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)],  0.44f, 1.0e-6f);
    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::pan)],    0.55f, 1.0e-6f);
    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::fine)],   0.66f, 1.0e-6f);
    expectWithinAbsoluteError (p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::drive)],  0.77f, 1.0e-6f);

    // I campi non modulabili restano quelli di sempre.
    expectWithinAbsoluteError (p.cutoffHz, params::cutoffHzFromRaw (0.11f), 1.0e-3f);
}

beginTest ("i parametri dell'LFO arrivano grezzi in EngineParams");
{
    const auto rawFor = [] (params::ParamSlot slot) noexcept
    {
        switch (slot)
        {
            case params::ParamSlot::lshape:  return 2.0f;   // choice: l'indice e' gia' il valore grezzo
            case params::ParamSlot::lrate:   return 0.45f;
            case params::ParamSlot::lsync:   return 1.0f;
            case params::ParamSlot::lphase:  return 0.25f;
            case params::ParamSlot::lfade:   return 0.5f;
            case params::ParamSlot::lretrig: return 0.0f;
            default:                         return 0.0f;
        }
    };

    const auto p = params::collectEngineParams (rawFor);

    expectEquals (p.lfoShapeIndex, 2);
    expectWithinAbsoluteError (p.lfoRateRaw, 0.45f, 1.0e-6f);
    expect (p.lfoSync);
    // lphase e' 0..360 gradi nella tabella: in EngineParams diventa 0..1.
    expectWithinAbsoluteError (p.lfoPhaseOffset01, 0.25f, 1.0e-6f);
    // lfade e' 0..4000 ms: in EngineParams diventa secondi.
    expectWithinAbsoluteError (p.lfoFadeSeconds, 2.0f, 1.0e-3f);
    expect (! p.lfoRetrig);
}
```

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Run: `cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL in compilazione, `no member named 'modBase' in 'engine::EngineParams'`.

- [ ] **Step 3: Estendere `EngineParams`**

In `Source/engine/EngineParams.h`, aggiungere `#include "engine/ModMatrix.h"` e `#include <array>`, poi in fondo alla struct:

```cpp
    // --- modulazione ---

    /** Valori normalizzati 0..1 dei sette target modulabili, indicizzati da engine::kModTargets.
        Sono la base su cui SynthVoice somma depth × livello sorgente, prima di denormalizzare:
        la stessa aritmetica di liveValue() in WebUI/src/synth/mod.ts. */
    std::array<float, (size_t) engine::kNumModTargets> modBase {};

    /** Le assegnazioni attive, pubblicate dal message thread. nullptr: nessuna modulazione. */
    const engine::ModSnapshot* mods { nullptr };

    float bpm { 120.0f };            // dal playhead dell'host; 120 quando non c'e'
    float modWheel { 0.0f };         // CC 1, 0..1
    float globalLfoLevel { 0.0f };   // LFO libero, usato dalle voci quando lfoRetrig e' falso

    int lfoShapeIndex { 0 };
    float lfoRateRaw { 0.0f };       // grezzo: diventa Hz o divisione a seconda di lfoSync
    bool lfoSync { false };
    float lfoPhaseOffset01 { 0.0f };
    float lfoFadeSeconds { 0.0f };
    bool lfoRetrig { true };
```

- [ ] **Step 4: Riempirli in `collectEngineParams`**

In `Source/parameters/ParamCollect.h`, prima del `return p;`:

```cpp
    // Le basi normalizzate: nessuna conversione, e' il valore grezzo dell'APVTS. La
    // denormalizzazione avviene dopo la somma delle modulazioni, dentro SynthVoice.
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::cutoff)] = rawFor (ParamSlot::cutoff);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::res)]    = rawFor (ParamSlot::res);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::wtpos)]  = rawFor (ParamSlot::wtpos);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::level)]  = rawFor (ParamSlot::level);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::pan)]    = rawFor (ParamSlot::pan);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::fine)]   = rawFor (ParamSlot::fine);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::drive)]  = rawFor (ParamSlot::drive);

    constexpr auto* specLphase = params::find ("lphase");
    constexpr auto* specLfade = params::find ("lfade");
    static_assert (specLphase != nullptr && specLfade != nullptr,
                   "lphase/lfade non sono in ParameterTable.h");

    p.lfoShapeIndex = (int) rawFor (ParamSlot::lshape); // choice: il grezzo e' gia' l'indice
    p.lfoRateRaw = rawFor (ParamSlot::lrate);
    p.lfoSync = rawFor (ParamSlot::lsync) >= 0.5f;
    p.lfoPhaseOffset01 = params::denormalise (*specLphase, rawFor (ParamSlot::lphase)) / 360.0f;
    p.lfoFadeSeconds = params::denormalise (*specLfade, rawFor (ParamSlot::lfade)) * 0.001f;
    p.lfoRetrig = rawFor (ParamSlot::lretrig) >= 0.5f;
```

`ParamCollect.h` deve includere `engine/ModMatrix.h`.

- [ ] **Step 5: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add Source/engine/EngineParams.h Source/parameters/ParamCollect.h Tests/EngineTests.cpp
git commit -m "Carry normalised bases, tempo and LFO settings in EngineParams"
```

---

### Task 6: `SynthVoice` applica la modulazione

È il cuore del lavoro: qui la somma `base + Σ depth × livello` diventa suono.

**Files:**
- Modify: `Source/engine/SynthVoice.h`, `Source/engine/SynthVoice.cpp`
- Test: `Tests/EngineTests.cpp`

**Interfaces:**
- Consumes: tutto quanto prodotto dai Task 1–5.
- Produces: `SynthVoice` che rispetta `EngineParams::mods`; `void SynthVoice::setGlobalLfoLevel (float) noexcept`; `float SynthVoice::getLfoLevel() const noexcept`.

- [ ] **Step 1: Scrivere il test che fallisce**

In `Tests/EngineTests.cpp`, con l'harness già presente (`defaultParams()`, `prepareEngine`, `renderRms`):

```cpp
beginTest ("env -> cutoff e' l'inviluppo di filtro: il timbro si apre durante l'attacco");
{
    dsp::WavetableStore store; store.setActive (1);
    engine::SynthEngine synth; prepareEngine (synth, store);

    engine::ModSnapshot mods;
    mods.count = 1;
    mods.routes[0] = { engine::ModSource::env,
                       engine::modTargetIndexFor (params::ParamSlot::cutoff), 1.0f };

    auto p = defaultParams();
    p.filterOn = true;
    p.cutoffHz = 200.0f;                 // il valore denormalizzato non conta piu' da solo:
    p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::cutoff)] = 0.15f; // conta questo
    p.attackSeconds = 0.5f;              // attacco lento: c'e' tempo per misurare due punti
    p.sustain = 1.0f;
    p.mods = &mods;
    synth.setParams (p);
    synth.setMasterGainLinear (1.0f);

    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
    juce::AudioBuffer<float> first (2, 128);
    first.clear();
    synth.process (first, m);

    const auto early = renderRms (synth, 40);   // subito dopo il note-on
    const auto late  = renderRms (synth, 40);   // piu' avanti nell'attacco

    // Cutoff piu' alto = piu' armoniche passano = piu' energia.
    expect (late > early * 1.2f, "il filtro non si e' aperto con l'inviluppo");
}

beginTest ("depth negativo modula nel verso opposto");
{
    dsp::WavetableStore store; store.setActive (1);

    const auto rmsWithDepth = [&store] (float depth)
    {
        engine::SynthEngine synth; prepareEngine (synth, store);

        engine::ModSnapshot mods;
        mods.count = 1;
        mods.routes[0] = { engine::ModSource::vel,
                           engine::modTargetIndexFor (params::ParamSlot::level), depth };

        auto p = defaultParams();
        p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.5f;
        p.mods = &mods;
        synth.setParams (p);
        synth.setMasterGainLinear (1.0f);

        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        juce::AudioBuffer<float> b (2, 128);
        b.clear();
        synth.process (b, m);

        return renderRms (synth, 20);
    };

    const auto base = rmsWithDepth (0.0f);
    expect (rmsWithDepth (0.4f) > base * 1.1f, "depth positivo non ha alzato il livello");
    expect (rmsWithDepth (-0.4f) < base * 0.9f, "depth negativo non ha abbassato il livello");
}

beginTest ("il clamp a 0..1 impedisce di superare il massimo del parametro");
{
    dsp::WavetableStore store; store.setActive (1);

    const auto rmsWith = [&store] (float base, float depth)
    {
        engine::SynthEngine synth; prepareEngine (synth, store);

        engine::ModSnapshot mods;
        mods.count = 1;
        mods.routes[0] = { engine::ModSource::vel,
                           engine::modTargetIndexFor (params::ParamSlot::level), depth };

        auto p = defaultParams();
        p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = base;
        p.mods = &mods;
        synth.setParams (p);
        synth.setMasterGainLinear (1.0f);

        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
        juce::AudioBuffer<float> b (2, 128);
        b.clear();
        synth.process (b, m);

        return renderRms (synth, 20);
    };

    // 0.9 + 1.0 × 1.0 = 1.9, clampato a 1.0: identico a partire gia' da 1.0 senza modulazione.
    expectWithinAbsoluteError (rmsWith (0.9f, 1.0f), rmsWith (1.0f, 0.0f), 1.0e-4f);
}

beginTest ("nessuna assegnazione: l'uscita e' identica campione per campione a prima");
{
    dsp::WavetableStore store; store.setActive (1);

    const auto render = [&store] (const engine::ModSnapshot* mods)
    {
        engine::SynthEngine synth; prepareEngine (synth, store);
        auto p = defaultParams();
        p.filterOn = true;
        p.mods = mods;
        synth.setParams (p);
        synth.setMasterGainLinear (0.8f);

        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        juce::AudioBuffer<float> b (2, 128);
        b.clear();
        synth.process (b, m);

        std::vector<float> out;
        juce::MidiBuffer none;
        for (int i = 0; i < 30; ++i)
        {
            juce::AudioBuffer<float> buf (2, 128);
            buf.clear();
            synth.process (buf, none);
            const auto* d = buf.getReadPointer (0);
            out.insert (out.end(), d, d + 128);
        }
        return out;
    };

    engine::ModSnapshot empty; // count = 0
    const auto withNull = render (nullptr);
    const auto withEmpty = render (&empty);

    expectEquals ((int) withNull.size(), (int) withEmpty.size());
    for (size_t i = 0; i < withNull.size(); ++i)
        expectWithinAbsoluteError (withEmpty[i], withNull[i], 0.0f);
}
```

L'ultimo test è il più importante del piano: garantisce che questo lavoro aggiunga una capacità senza cambiare il suono di chi non la usa.

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Run: `cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL in compilazione (`no member named 'mods'` è già risolto dal Task 5, quindi il fallimento atteso è sulle asserzioni: il filtro non si apre, il depth non fa niente).

- [ ] **Step 3: Estendere `SynthVoice.h`**

Aggiungere `#include "dsp/Lfo.h"` e `#include "engine/ModMatrix.h"`, poi nella parte pubblica:

```cpp
    /** Livello dell'LFO libero del motore, usato quando lfoRetrig e' falso. Thread audio. */
    void setGlobalLfoLevel (float level) noexcept { globalLfoLevel_ = level; }

    /** Il livello dell'LFO di questa voce: lo legge SynthEngine per il meter. */
    float getLfoLevel() const noexcept { return lfoRetrig_ ? lfo_.level() : globalLfoLevel_; }
```

e fra i membri privati:

```cpp
    /** Somma le route che puntano a `targetIndex` sul valore normalizzato di base, e clampa.
        Stessa aritmetica di liveValue() in WebUI/src/synth/mod.ts. */
    float modulated (int targetIndex) const noexcept;

    dsp::Lfo lfo_;
    const ModSnapshot* mods_ { nullptr };
    std::array<float, (size_t) kNumModTargets> modBase_ {};
    float sourceLevels_[(size_t) ModSource::count] {};
    float globalLfoLevel_ { 0.0f };
    bool lfoRetrig_ { true };
    float lfoPhaseOffset01_ { 0.0f };
```

- [ ] **Step 4: Implementare in `SynthVoice.cpp`**

`prepare()`: aggiungere `lfo_.prepare (sampleRate_);`.

`start()`: dopo `envelope_.noteOn (peak);` aggiungere

```cpp
    // Con lfoRetrig la fase riparte dall'offset scelto: e' cio' che rende ripetibile un
    // vibrato che deve cominciare sempre allo stesso punto. Senza, la voce eredita la fase
    // libera del motore e due note identiche suonano diverse.
    if (lfoRetrig_)
        lfo_.retrigger (lfoPhaseOffset01_);
```

`retrigger()`: **non** toccare la fase dell'LFO, per la stessa ragione per cui non tocca fase dell'oscillatore e stato del filtro — è una ribattuta, non una nota nuova. Ma la dissolvenza sì, riparte: aggiungere `if (lfoRetrig_) lfo_.retrigger (lfo_.level() >= 0.0f ? lfoPhaseOffset01_ : lfoPhaseOffset01_);` **no**: lasciare l'LFO completamente intatto e documentarlo con un commento.

`setParams()`: sostituire le assegnazioni dirette dei sette target modulabili con il percorso modulato, e configurare l'LFO:

```cpp
    mods_ = p.mods;
    modBase_ = p.modBase;
    globalLfoLevel_ = p.globalLfoLevel;
    lfoRetrig_ = p.lfoRetrig;
    lfoPhaseOffset01_ = p.lfoPhaseOffset01;

    lfo_.setShape ((dsp::Lfo::Shape) juce::jlimit (0, 4, p.lfoShapeIndex));
    lfo_.setFadeSeconds (p.lfoFadeSeconds);
    lfo_.setFrequencyHz (p.lfoSync ? dsp::syncedRateHz (p.lfoRateRaw, (double) p.bpm)
                                   : params::denormalise (*params::find ("lrate"), p.lfoRateRaw));

    // I livelli delle sorgenti si calcolano una volta per blocco, prima di applicarli: env e
    // vel sono per voce (due note tenute stanno a punti diversi del loro inviluppo), mw e'
    // globale, lfo dipende da lfoRetrig.
    sourceLevels_[(size_t) ModSource::lfo] = lfoRetrig_ ? lfo_.level() : globalLfoLevel_;
    sourceLevels_[(size_t) ModSource::env] = envelope_.getLevel();
    sourceLevels_[(size_t) ModSource::vel] = velocity_;
    sourceLevels_[(size_t) ModSource::mw]  = p.modWheel;

    const auto index = [] (params::ParamSlot s) noexcept { return modTargetIndexFor (s); };

    baseCutoffHz_ = params::cutoffHzFromRaw (modulated (index (params::ParamSlot::cutoff)));
    filter_.setResonance (params::resonanceQFromRaw (modulated (index (params::ParamSlot::res))));
    driveGain_ = params::driveGainFromRaw (modulated (index (params::ParamSlot::drive)));

    smoothedFramePosition_.setTargetValue (params::framePositionFromRaw (modulated (index (params::ParamSlot::wtpos))));
    smoothedLevel_.setTargetValue (params::levelGainFromRaw (modulated (index (params::ParamSlot::level))));
    smoothedPan_.setTargetValue (params::panFromRaw (modulated (index (params::ParamSlot::pan))));

    tuningSemitones_ = (float) (12 * p.octave + p.semitones)
                           + params::fineCentsFromRaw (modulated (index (params::ParamSlot::fine))) * 0.01f;
```

Il resto di `setParams` (oscOn, filterOn, filterType, filterStages, inviluppo, keyTrack, velocityAmount) resta com'è. `SynthVoice.cpp` deve includere `parameters/ParamCollect.h`.

`modulated()`:

```cpp
float SynthVoice::modulated (int targetIndex) const noexcept
{
    auto value = modBase_[(size_t) targetIndex];

    if (mods_ != nullptr)
        for (int i = 0; i < mods_->count; ++i)
        {
            const auto& route = mods_->routes[i];

            if (route.targetIndex == targetIndex)
                value += route.depth * sourceLevels_[(size_t) route.src];
        }

    // Il clamp e' parte del contratto, non una precauzione: liveValue() in mod.ts clampa allo
    // stesso punto, e l'anello del knob nella UI mostra quel valore. Senza, suono e schermo
    // racconterebbero due storie diverse agli estremi della corsa.
    return juce::jlimit (0.0f, 1.0f, value);
}
```

`render()`: all'inizio, dopo il controllo di `isActive()`, avanzare l'LFO della voce:

```cpp
    if (lfoRetrig_)
        lfo_.advance (numSamples);
```

**Nota sull'ordine.** `setParams` legge `envelope_.getLevel()` e `lfo_.level()` *prima* che il blocco venga renderizzato: la modulazione di un blocco usa quindi i livelli di fine blocco precedente. È un ritardo di un blocco (2.7 ms a 48 kHz con blocchi da 128), impercettibile e coerente con il fatto che cutoff e Position sono già aggiornati una volta per blocco.

**Nota sul tuning.** `tuningSemitones_` è letto da `start()` per calcolare la frequenza della nota. Una modulazione di `fine` durante la nota non cambia quindi l'intonazione di una voce già partita. Farlo richiederebbe di ricalcolare `frequencyHz_` in `setParams` e chiamare `oscillator_.setFrequencyHz`: **farlo**, aggiungendo in fondo a `setParams`:

```cpp
    // Il vibrato ha senso solo se l'intonazione segue la modulazione anche a nota gia' partita.
    if (midiNote_ >= 0)
    {
        frequencyHz_ = midiNoteToHz (midiNote_, tuningSemitones_);
        oscillator_.setFrequencyHz (frequencyHz_);
    }
```

- [ ] **Step 5: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`. Se il test di non-regressione fallisce, la causa più probabile è che una delle sette conversioni nel percorso modulato non coincide con quella del percorso precedente: confrontare con il Task 1.

- [ ] **Step 6: Commit**

```bash
git add Source/engine/SynthVoice.h Source/engine/SynthVoice.cpp Tests/EngineTests.cpp
git commit -m "Apply the mod matrix inside the voice"
```

---

### Task 7: `SynthEngine` pubblica lo snapshot, l'LFO libero e il mod wheel

**Files:**
- Modify: `Source/engine/SynthEngine.h`, `Source/engine/SynthEngine.cpp`, `Source/engine/VoiceManager.h`, `Source/engine/VoiceManager.cpp`
- Test: `Tests/EngineTests.cpp`

**Interfaces:**
- Consumes: Task 2, 3, 6.
- Produces: `void SynthEngine::setMods (const engine::ModSnapshot&)` (chiamabile da qualunque thread), `float SynthEngine::getLfoLevel() const noexcept`, `void VoiceManager::setGlobalLfoLevel (float) noexcept`.

- [ ] **Step 1: Scrivere il test che fallisce**

```cpp
beginTest ("setMods pubblica le assegnazioni e process() le applica");
{
    dsp::WavetableStore store; store.setActive (1);
    engine::SynthEngine synth; prepareEngine (synth, store);

    auto p = defaultParams();
    p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.5f;
    synth.setParams (p);
    synth.setMasterGainLinear (1.0f);

    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    juce::AudioBuffer<float> b (2, 128);
    b.clear();
    synth.process (b, m);

    const auto before = renderRms (synth, 20);

    engine::ModSnapshot mods;
    mods.count = 1;
    mods.routes[0] = { engine::ModSource::vel,
                       engine::modTargetIndexFor (params::ParamSlot::level), 0.5f };
    synth.setMods (mods);

    const auto after = renderRms (synth, 20);
    expect (after > before * 1.1f, "la nuova assegnazione non e' arrivata alle voci");
}

beginTest ("il mod wheel arriva da CC 1 e vale 0..1");
{
    dsp::WavetableStore store; store.setActive (1);
    engine::SynthEngine synth; prepareEngine (synth, store);

    engine::ModSnapshot mods;
    mods.count = 1;
    mods.routes[0] = { engine::ModSource::mw,
                       engine::modTargetIndexFor (params::ParamSlot::level), 0.5f };
    synth.setMods (mods);

    auto p = defaultParams();
    p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.4f;
    synth.setParams (p);
    synth.setMasterGainLinear (1.0f);

    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
    juce::AudioBuffer<float> b (2, 128);
    b.clear();
    synth.process (b, m);

    const auto closed = renderRms (synth, 30);

    juce::MidiBuffer cc;
    cc.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
    juce::AudioBuffer<float> b2 (2, 128);
    b2.clear();
    synth.process (b2, cc);

    const auto open = renderRms (synth, 30);
    expect (open > closed * 1.1f, "il mod wheel non ha modulato niente");
}

beginTest ("l'LFO libero avanza anche senza note e finisce nel meter");
{
    dsp::WavetableStore store; store.setActive (1);
    engine::SynthEngine synth; prepareEngine (synth, store);

    auto p = defaultParams();
    p.lfoRetrig = false;
    p.lfoShapeIndex = 2;   // saw: parte da 1 e scende
    p.lfoRateRaw = 1.0f;   // il massimo della mappa: 20 Hz
    p.lfoSync = false;
    synth.setParams (p);

    const auto first = synth.getLfoLevel();

    juce::MidiBuffer none;
    for (int i = 0; i < 10; ++i)
    {
        juce::AudioBuffer<float> b (2, 128);
        b.clear();
        synth.process (b, none);
    }

    expect (! juce::approximatelyEqual (synth.getLfoLevel(), first), "l'LFO libero non e' avanzato");
}
```

- [ ] **Step 2: Eseguire il test per verificare che fallisca**

Run: `cmake --build build/macos-debug --target XerumTests -j8`
Expected: FAIL in compilazione, `no member named 'setMods'`.

- [ ] **Step 3: Implementare in `SynthEngine.h`**

```cpp
    /**
     * Pubblica una nuova lista di assegnazioni. Chiamabile da qualunque thread: si copia lo
     * snapshot in uno slot libero dell'anello e si pubblica solo il puntatore.
     *
     * Anello di quattro e non doppio buffer: le mod cambiano molto piu' spesso di una wavetable
     * (l'utente trascina uno slider di depth), e con due soli slot il message thread potrebbe
     * riscrivere quello che il thread audio sta leggendo. Con quattro dovrebbe pubblicare
     * quattro volte dentro un singolo blocco audio per raggiungere il lettore.
     */
    void setMods (const engine::ModSnapshot& snapshot) noexcept;

    /** Il livello corrente dell'LFO, per il meter dell'editor. */
    float getLfoLevel() const noexcept { return lfoLevel_.load (std::memory_order_relaxed); }
```

membri privati:

```cpp
    dsp::Lfo globalLfo_;
    ModSnapshot modRing_[4] {};
    std::atomic<int> modWriteSlot_ { 0 };
    std::atomic<const ModSnapshot*> activeMods_ { nullptr };
    std::atomic<float> lfoLevel_ { 0.0f };
    float modWheel_ { 0.0f };
```

- [ ] **Step 4: Implementare in `SynthEngine.cpp`**

```cpp
void SynthEngine::setMods (const engine::ModSnapshot& snapshot) noexcept
{
    const auto slot = modWriteSlot_.fetch_add (1, std::memory_order_relaxed) & 3;
    modRing_[slot] = snapshot;
    activeMods_.store (&modRing_[slot], std::memory_order_release);
}
```

In `prepare()`: `globalLfo_.prepare (spec_.sampleRate);`.

In `handleMidiEvent()`, prima degli altri rami:

```cpp
    if (message.isController() && message.getControllerNumber() == 1)
    {
        modWheel_ = (float) message.getControllerValue() / 127.0f;
        return;
    }
```

In `process()`, dopo l'applicazione della wavetable in attesa e prima del ciclo MIDI:

```cpp
    // L'LFO libero avanza una volta per blocco, note o non note: e' cio' che lo rende "libero".
    globalLfo_.setShape ((dsp::Lfo::Shape) juce::jlimit (0, 4, params_.lfoShapeIndex));
    globalLfo_.setFadeSeconds (0.0f); // la dissolvenza e' per nota: non ha senso sull'LFO libero
    globalLfo_.setFrequencyHz (params_.lfoSync
                                   ? dsp::syncedRateHz (params_.lfoRateRaw, (double) params_.bpm)
                                   : params::denormalise (*params::find ("lrate"), params_.lfoRateRaw));
    const auto globalLevel = globalLfo_.advance (numSamples);

    params_.mods = activeMods_.load (std::memory_order_acquire);
    params_.modWheel = modWheel_;
    params_.globalLfoLevel = globalLevel;
    voices_.setParams (params_);

    lfoLevel_.store (params_.lfoRetrig ? voices_.getLfoLevel() : globalLevel,
                     std::memory_order_relaxed);
```

Attenzione: `numSamples` è dichiarato più in basso nella funzione attuale — spostare la sua dichiarazione sopra questo blocco.

In `VoiceManager`, aggiungere:

```cpp
    void setGlobalLfoLevel (float level) noexcept
    {
        for (auto& voice : voices_)
            voice.setGlobalLfoLevel (level);
    }

    /** Il livello dell'LFO della prima voce attiva, per il meter. Zero se non suona niente. */
    float getLfoLevel() const noexcept
    {
        for (const auto& voice : voices_)
            if (voice.isActive())
                return voice.getLfoLevel();

        return 0.0f;
    }
```

`setParams` di `VoiceManager` propaga già a tutte le voci, quindi `globalLfoLevel` arriva tramite `EngineParams`: `setGlobalLfoLevel` serve solo se si vuole aggiornarlo fuori da `setParams`. Se non serve, **non aggiungerlo**: YAGNI.

- [ ] **Step 5: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add Source/engine/SynthEngine.h Source/engine/SynthEngine.cpp Source/engine/VoiceManager.h Source/engine/VoiceManager.cpp Tests/EngineTests.cpp
git commit -m "Publish mod snapshots, the free LFO and the mod wheel from the engine"
```

---

### Task 8: `PluginProcessor` costruisce lo snapshot e legge il tempo

**Files:**
- Modify: `Source/plugin/PluginProcessor.h`, `Source/plugin/PluginProcessor.cpp`
- Test: nessun test automatico nuovo (richiederebbe un host); la verifica è manuale, vedi Step 5.

**Interfaces:**
- Consumes: Task 2, 5, 7.
- Produces: niente per altri task; è il punto in cui il sistema si accende.

- [ ] **Step 1: Registrare i nuovi slot dei parametri**

Nel costruttore di `SerumStyleSynthAudioProcessor`, accanto agli altri:

```cpp
    paramSlots_[(size_t) params::ParamSlot::lshape] = apvts_.getRawParameterValue ("lshape");
    paramSlots_[(size_t) params::ParamSlot::lrate] = apvts_.getRawParameterValue ("lrate");
    paramSlots_[(size_t) params::ParamSlot::lsync] = apvts_.getRawParameterValue ("lsync");
    paramSlots_[(size_t) params::ParamSlot::lphase] = apvts_.getRawParameterValue ("lphase");
    paramSlots_[(size_t) params::ParamSlot::lfade] = apvts_.getRawParameterValue ("lfade");
    paramSlots_[(size_t) params::ParamSlot::lretrig] = apvts_.getRawParameterValue ("lretrig");
```

- [ ] **Step 2: Ascoltare il nodo MODS e pubblicare lo snapshot**

Far derivare la classe anche da `private juce::ValueTree::Listener`, e aggiungere:

```cpp
    void rebuildModSnapshot();

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override
    {
        if (tree.hasType (state::ids::MOD) || tree.hasType (state::ids::MODS))
            rebuildModSnapshot();
    }

    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override
    {
        if (parent.hasType (state::ids::MODS))
            rebuildModSnapshot();
    }

    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override
    {
        if (parent.hasType (state::ids::MODS))
            rebuildModSnapshot();
    }

    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}
```

e nel `.cpp`:

```cpp
void SerumStyleSynthAudioProcessor::rebuildModSnapshot()
{
    // Message thread: qui si confrontano stringhe e si alloca la copia locale. Sul thread audio
    // arriva solo il puntatore, gia' risolto (vedi SynthEngine::setMods).
    engine::ModSnapshot snapshot;
    engine::buildModSnapshot (apvts_.state.getChildWithName (state::ids::MODS), snapshot);
    engine_->setMods (snapshot);
}
```

Nel costruttore, dopo `state::ensureChildren (apvts_.state);`: `apvts_.state.addListener (this); rebuildModSnapshot();`.
Nel distruttore: `apvts_.state.removeListener (this);`.

`setStateInformation` sostituisce l'albero: dopo `replaceState` va richiamato `state::ensureChildren`, riagganciato il listener al nuovo albero e rifatto `rebuildModSnapshot()`. Il progetto ha già `stateReplaced_` proprio per questo: usarlo.

- [ ] **Step 3: Leggere il tempo dell'host**

In `processBlock`, dopo `const auto params = collectParams();` — che ritorna per valore, quindi va reso non-`const` o copiato:

```cpp
    auto params = collectParams();

    // Il tempo serve solo all'LFO quando lsync e' attivo. Senza playhead (Standalone, render
    // offline di alcuni host) si resta a 120 BPM, che e' anche cio' che rende deterministici i test.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                params.bpm = (float) *bpm;

    engine_->setParams (params);
```

- [ ] **Step 4: Pubblicare il livello dell'LFO nel meter**

In `processBlock`, accanto agli altri `store` sui meter:

```cpp
    meters_.lfo.store (engine_->getLfoLevel(), std::memory_order_relaxed);
```

- [ ] **Step 5: Verifica manuale**

Compilare e lanciare lo Standalone:

```bash
cmake --build build/macos-debug --target SerumStyleSynth_Standalone -j8
```

Poi, con l'app aperta:
1. Trascinare il chip **ENV** sul knob **Cutoff**, portare il depth a +60 %, suonare una nota con attacco lungo: il timbro deve aprirsi durante l'attacco.
2. Trascinare **LFO** su **Fine**, depth 10 %, `lrate` a ~5 Hz: vibrato udibile, e il puntino del tab LFO deve muoversi a quella velocità.
3. Rimuovere le assegnazioni dal ModTab: il suono deve tornare esattamente com'era.
4. Salvare e ricaricare lo stato (cambiare preset e tornare indietro): le assegnazioni devono sopravvivere.

- [ ] **Step 6: Commit**

```bash
git add Source/plugin/PluginProcessor.h Source/plugin/PluginProcessor.cpp
git commit -m "Wire the mod snapshot, host tempo and LFO meter into the processor"
```

---

### Task 9: sicurezza real-time e documentazione

**Files:**
- Modify: `Tests/EngineTests.cpp`, `docs/architecture.md`

- [ ] **Step 1: Scrivere il test di stress**

```cpp
beginTest ("matrix pieno: nessun NaN, nessuna esplosione, uscita limitata");
{
    dsp::WavetableStore store; store.setActive (1);
    engine::SynthEngine synth; prepareEngine (synth, store);

    // Tutte le combinazioni sorgente × target, fino al tetto: e' il carico peggiore possibile.
    engine::ModSnapshot mods;
    for (int s = 0; s < (int) engine::ModSource::count; ++s)
        for (int t = 0; t < engine::kNumModTargets; ++t)
        {
            if (mods.count >= engine::ModSnapshot::kMaxRoutes)
                break;

            mods.routes[mods.count++] = { (engine::ModSource) s, t, s % 2 == 0 ? 1.0f : -1.0f };
        }

    synth.setMods (mods);

    auto p = defaultParams();
    p.filterOn = true;
    p.modBase.fill (0.5f);
    p.lfoRateRaw = 1.0f;
    synth.setParams (p);
    synth.setMasterGainLinear (1.0f);

    juce::MidiBuffer m;
    for (int i = 0; i < 16; ++i)
        m.addEvent (juce::MidiMessage::noteOn (1, 40 + i * 3, 1.0f), 0);

    juce::AudioBuffer<float> b (2, 128);
    b.clear();
    synth.process (b, m);

    const auto peak = renderPeak (synth, 200, *this);
    expect (peak <= 1.0f, "il soft clipper non ha tenuto");
}
```

- [ ] **Step 2: Eseguire i test**

Run: `cmake --build build/macos-debug --target XerumTests -j8 && ./build/macos-debug/XerumTests_artefacts/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 3: Documentare in `docs/architecture.md`**

Aggiungere una sezione "Modulazione" che copra: il percorso dal nodo `MODS` allo snapshot, l'anello di quattro e perché non due, le quattro sorgenti e il loro ambito (per voce contro globale), i sette target e il fatto che gli altri vengono scartati sul message thread, la regola che la somma avviene sul normalizzato per combaciare con `liveValue` della UI, il ritardo di un blocco nei livelli sorgente, e il limite esplicito che questo è un sistema a tasso di controllo (niente FM/AM audio-rate).

- [ ] **Step 4: Commit**

```bash
git add Tests/EngineTests.cpp docs/architecture.md
git commit -m "Stress the full mod matrix and document the modulation path"
```

---

## Cosa resta fuori

Ognuno è un lavoro a sé, non incluso qui:

- Arp (`arpOn`, `arpMode`, `arpRate`, `arpGate`, `arpOct`, `arpSwing`) e i 16 step già persistiti in `StateTree.h`.
- FX: chorus (`chRate`, `chDepth`, `chMix`) e reverb (`rvSize`, `rvDamp`, `rvMix`).
- Unison, detune, glide, `voiceMode` mono/poly — il task successivo in coda, e tocca anch'esso `SynthVoice`.
- La ritaratura del gain staging, che va fatta **per ultima**: unison e inviluppo di filtro cambiano i livelli su cui va calibrata.
- `envCurve`: la forma dell'inviluppo è indipendente dalla modulazione.
- Esporre alla UI quali target il motore sa modulare, così che un'assegnazione scartata non appaia attiva.
