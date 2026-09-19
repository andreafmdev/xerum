# Striscia bassa web — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Sostituire la striscia nativa `ui::XerumKeyboard` con una tastiera e una barra di esecuzione dentro la WebUI, aggiungendo il canale MIDI del bridge e il pitch bend nel motore.

**Architecture:** La WebView diventa l'intero editor. Le note della UI entrano nel flusso MIDI attraverso il `MidiKeyboardState` che già esiste nel processor; le wheel diventano veri messaggi MIDI anteposti al buffer in `processBlock`, così arp, motore e mod matrix non sanno che la rotella è disegnata. Il verso opposto — quali note stanno suonando — viaggia nel frame `meters` già esistente a 30 Hz come quattro interi da 32 bit.

**Tech Stack:** JUCE 8.0.6 (C++17, CMake), React 19 + TypeScript + Tailwind v4 nella WebUI, `@xerum/ui` come libreria di primitive, Vitest lato web, `juce::UnitTestRunner` lato C++.

**Spec:** `docs/superpowers/specs/2026-09-20-bottom-strip-design.md`

## Global Constraints

- C++17, JUCE 8.0.6 sotto `external/JUCE`. Niente eccezioni, niente allocazioni sul thread audio.
- `Source/parameters/parameters.json` è la **verità unica** dei parametri. Dopo ogni modifica va rigenerato tutto con `cd WebUI && pnpm gen:params`; `WebUI/src/synth/params.freshness.test.ts` fallisce se i generati sono stantii.
- Per `kind: "int"` il campo `default` sta nel **range naturale** (`map.min..map.max`), non in 0..1. Confondere i domini è il bug delle quattro ottave.
- Aggiungere un `.cpp` a `Source/` o a `Tests/` richiede un riconfigure: `cmake --preset macos-debug`.
- Test C++: `cmake --build --preset macos-debug --target XerumTests && build/macos-debug/XerumTests_artefacts/Debug/XerumTests`. Deve stampare `ALL TESTS PASSED`. Obbligatorio prima di ogni commit che tocchi `Source/dsp` o `Source/engine`.
- Test web: `cd WebUI && pnpm test` (app shell) e `pnpm ui:test` (`@xerum/ui`). `pnpm typecheck` deve passare.
- `XerumTests` non compila `Source/bridge/*` e non linka `juce_gui_extra`: niente test unitari per i canali del bridge, come già per `StateChannel` e `MeterChannel`.
- Solo tema scuro. Nessun colore scritto a mano nei componenti: si usano i token di `WebUI/packages/ui/src/theme.css`.
- Commenti e messaggi di test in italiano, come il resto del codice. Messaggi di commit in inglese.

---

## Struttura dei file

**Creati**
- `Tests/EngineHarness.h` — i tre helper di test del motore, estratti da `EngineTests.cpp` per poterli riusare.
- `Tests/PitchBendTests.cpp` — pitch bend e `pbRange`.
- `Source/bridge/MidiChannel.h` / `.cpp` — native function note e wheel.
- `WebUI/packages/ui/src/components/Wheel/Wheel.tsx` / `.test.tsx` / `.stories.tsx`
- `WebUI/packages/ui/src/components/Keybed/Keybed.tsx` / `.test.tsx` / `.stories.tsx`
- `WebUI/src/synth/ui/PerformanceBar.tsx` / `.test.tsx`
- `WebUI/src/synth/ui/BottomStrip.tsx` / `.test.tsx`

**Modificati**
- `Source/parameters/parameters.json` — `pbRange`
- `Source/engine/SynthEngine.{h,cpp}` — parsing pitch bend, mask delle note
- `Source/engine/EngineParams.h` — `pitchBend`, `pitchBendRangeSemitones`
- `Source/engine/SynthVoice.cpp:592` — il bend entra in `tuningSemitones_`
- `Source/engine/MeterFrame.h` — `notesLo`, `notesHi`
- `Source/parameters/ParamCollect.h` — raccolta di `pbRange`
- `Source/plugin/PluginProcessor.{h,cpp}` — atomici delle wheel, iniezione MIDI, pubblicazione del mask
- `Source/plugin/PluginEditor.{h,cpp}` — `MidiChannel`, rimozione della tastiera nativa
- `Source/bridge/MeterChannel.cpp` — `n0..n3` nel frame
- `CMakeLists.txt` — nuovi sorgenti, rimozione di `XerumKeyboard.cpp`
- `WebUI/src/juce/backend.ts` — `MeterFrame`, helper del mask, metodi MIDI nel `Backend`
- `WebUI/src/juce/juce-backend.ts`, `WebUI/src/juce/fake-backend.ts` — implementazioni
- `WebUI/src/synth/ui/SynthWindow.tsx` — chassis 708, `BottomStrip` al posto di `Footer`
- `WebUI/packages/ui/src/index.ts` — export delle due primitive

**Cancellati**
- `Source/ui/XerumKeyboard.h` / `.cpp`
- `WebUI/src/synth/ui/Footer.tsx`

---

## FASE 1 — Pitch bend nel motore

### Task 1: Il parametro `pbRange`

**Files:**
- Modify: `Source/parameters/parameters.json`
- Modify: `Source/parameters/ParamCollect.h:198-206`
- Modify: `Source/engine/EngineParams.h:169`
- Test: `Tests/ParameterSeamTests.cpp`

**Interfaces:**
- Produces: `params::ParamSlot::pbRange` (generato), `engine::EngineParams::pitchBendRangeSemitones` (`int`, default 2).

- [ ] **Step 1: Scrivi il test che fallisce**

In `Tests/ParameterSeamTests.cpp`, dopo il blocco `beginTest ("oct e semi coprono tutto il loro range, non solo gli estremi")`:

```cpp
        beginTest ("il range del pitch bend arriva in semitoni, non normalizzato");
        {
            // Stessa forma di oct e semi: AudioParameterInt, quindi getRawParameterValue
            // restituisce 0..24 e non 0..1. Denormalizzarlo come float darebbe 0 semitoni al
            // default, cioe' una rotella inerte, e 24 a fondo corsa qualunque cosa dica l'utente.
            expectEquals (params::collectEngineParams (raw).pitchBendRangeSemitones, 2,
                          "al default il range deve essere 2 semitoni");

            for (int semi : { 0, 1, 2, 7, 12, 24 })
            {
                setNatural (processor, "pbRange", (float) semi);
                expectEquals (params::collectEngineParams (raw).pitchBendRangeSemitones, semi,
                              "pbRange naturale " + juce::String (semi));
            }
            setNatural (processor, "pbRange", 2.0f);
        }
```

- [ ] **Step 2: Fallo fallire**

```bash
cmake --build --preset macos-debug --target XerumTests
```

Atteso: errore di compilazione, `pitchBendRangeSemitones` non è un membro di `EngineParams`.

- [ ] **Step 3: Aggiungi il parametro alla verità unica**

In `Source/parameters/parameters.json`, nell'array `params`, subito dopo la riga di `fine`:

```json
    { "id": "pbRange", "name": "Pitch bend range", "group": "osc", "kind": "int", "slot": true, "map": { "type": "linear", "min": 0, "max": 24 }, "default": 2, "unit": "SEMI" },
```

- [ ] **Step 4: Rigenera**

```bash
cd WebUI && pnpm gen:params && cd ..
git diff --stat Source/parameters/ParameterTable.h WebUI/src/synth/params.generated.ts
```

Atteso: entrambi i generati cambiano e contengono `pbRange`.

- [ ] **Step 5: Aggiungi il campo a EngineParams**

In `Source/engine/EngineParams.h`, accanto a `modWheel`:

```cpp
    float modWheel { 0.0f };         // CC 1, 0..1

    /** Ampiezza del pitch bend in semitoni a fondo corsa, dal parametro `pbRange`. Non e' una
        quantita' audio-rate: la posizione della rotella e' `pitchBend`, questo e' quanto vale. */
    int pitchBendRangeSemitones { 2 };

    /** Posizione della rotella di pitch, -1..1. Non e' un parametro dell'APVTS: la scrive
        SynthEngine leggendo i messaggi MIDI, come fa con `modWheel`. */
    float pitchBend { 0.0f };
```

- [ ] **Step 6: Raccogli il parametro**

In `Source/parameters/ParamCollect.h`, subito dopo la riga di `p.semitones` (`:205`):

```cpp
    // Stessa trappola di oct e semi, stessa difesa: Kind::Int vive nel suo range naturale.
    constexpr auto& specPbRange = specForSlot (ParamSlot::pbRange);
    static_assert (specPbRange.kind == Kind::Int,
                   "pbRange deve restare Kind::Int: da Float la conversione qui sotto cambierebbe");
    p.pitchBendRangeSemitones = juce::roundToInt (naturalFromRaw (specPbRange, rawFor (ParamSlot::pbRange)));
```

- [ ] **Step 7: Aggiorna il conteggio nei commenti**

`Tests/ParameterSeamTests.cpp` dice "tutti e 52" in due punti (l'intestazione del file e il commento sopra i cicli finali). I parametri ora sono 53: aggiorna entrambi i numeri.

- [ ] **Step 8: Verifica**

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --target XerumTests
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
cd WebUI && pnpm test && pnpm typecheck && cd ..
```

Atteso: `ALL TESTS PASSED`, e `params.freshness.test.ts` verde (i generati sono aggiornati).

- [ ] **Step 9: Commit**

```bash
git add Source/parameters/parameters.json Source/parameters/ParameterTable.h Source/parameters/ParamCollect.h \
        Source/engine/EngineParams.h WebUI/src/synth/params.generated.ts Tests/ParameterSeamTests.cpp
git commit -m "Add the pitch bend range parameter"
```

---

### Task 2: Estrai gli helper di test del motore

Gli helper che servono al pitch bend (`prepareEngine`, `defaultParams`, `measureFundamentalHz`) vivono in un namespace anonimo dentro `Tests/EngineTests.cpp`, che è il file più grande della suite: da lì non sono raggiungibili. Questa task li sposta in un header senza cambiarne una riga di logica.

**Files:**
- Create: `Tests/EngineHarness.h`
- Modify: `Tests/EngineTests.cpp:31-160` (rimozione delle tre definizioni locali)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `harness::prepareEngine(engine::SynthEngine&, dsp::WavetableStore&)`, `harness::defaultParams() -> engine::EngineParams`, `harness::measureFundamentalHz(engine::SynthEngine&, double sampleRate, int settleSamples, int measureSamples) -> float`.

- [ ] **Step 1: Crea l'header**

Crea `Tests/EngineHarness.h`. Copia **alla lettera** i corpi di `prepareEngine`, `defaultParams` e `measureFundamentalHz` da `Tests/EngineTests.cpp` (rispettivamente attorno a `:111`, `:31` e `:135`), commenti compresi, dentro questo guscio:

```cpp
#pragma once

/**
 * I tre helper con cui si monta un motore in un test: prepararlo, dargli parametri sensati e
 * misurarne la fondamentale senza usare la formula che il motore usa internamente.
 *
 * Stavano nel namespace anonimo di EngineTests.cpp, cioe' irraggiungibili da un altro file di
 * test. Qui il codice e' lo stesso, riga per riga: se un test cambia comportamento dopo questa
 * estrazione, e' la copia a essere sbagliata, non l'estrazione.
 */

#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace harness
{
// ... le tre funzioni, marcate `inline`, copiate da EngineTests.cpp ...
} // namespace harness
```

Ogni funzione va marcata `inline` (è un header incluso da più unità di compilazione).

- [ ] **Step 2: Fai usare l'header a EngineTests.cpp**

In `Tests/EngineTests.cpp`: aggiungi `#include "EngineHarness.h"` fra gli altri include, cancella le tre definizioni locali, e dentro il namespace anonimo aggiungi:

```cpp
using harness::defaultParams;
using harness::measureFundamentalHz;
using harness::prepareEngine;
```

- [ ] **Step 3: Verifica che nulla sia cambiato**

```bash
cmake --build --preset macos-debug --target XerumTests
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```

Atteso: `ALL TESTS PASSED`, **stesso numero di test di prima**. Questa task non deve cambiare nessun comportamento: se un test fallisce, la copia non è fedele.

- [ ] **Step 4: Commit**

```bash
git add Tests/EngineHarness.h Tests/EngineTests.cpp
git commit -m "Extract the engine test harness into a header"
```

---

### Task 3: Il pitch bend nel motore

**Files:**
- Create: `Tests/PitchBendTests.cpp`
- Modify: `Source/engine/SynthEngine.h` (campi privati), `Source/engine/SynthEngine.cpp:191` e `:584`
- Modify: `Source/engine/SynthVoice.cpp:592`
- Modify: `CMakeLists.txt` (aggiunta di `Tests/PitchBendTests.cpp` a `XerumTests`)

**Interfaces:**
- Consumes: `engine::EngineParams::pitchBend`, `::pitchBendRangeSemitones` (Task 1); `harness::*` (Task 2).

- [ ] **Step 1: Scrivi i test che falliscono**

Crea `Tests/PitchBendTests.cpp`:

```cpp
#include "EngineHarness.h"

#include "dsp/WavetableStore.h"
#include "engine/SynthEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
/** La frequenza attesa per una nota MIDI spostata di `semitones`. */
float hzFor (int midiNote, float semitones) noexcept
{
    return 440.0f * std::pow (2.0f, ((float) midiNote + semitones - 69.0f) / 12.0f);
}

/** Valore grezzo di pitch wheel per una posizione -1..1. 8192 e' il centro. */
int wheelFor (float position) noexcept
{
    return juce::jlimit (0, 16383, 8192 + juce::roundToInt (position * 8192.0f));
}
} // namespace

struct PitchBendTests final : juce::UnitTest
{
    PitchBendTests() : juce::UnitTest ("pitch bend", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;

        beginTest ("senza bend la nota suona alla sua altezza");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            synth.setParams (harness::defaultParams());

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            const auto hz = harness::measureFundamentalHz (synth, 48000.0, 4800, 24000);
            expectWithinAbsoluteError (hz, 440.0f, 3.0f);
        }

        beginTest ("bend a fondo corsa sposta di pbRange semitoni");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            auto p = harness::defaultParams();
            p.pitchBendRangeSemitones = 2;
            synth.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheelFor (1.0f)), 1);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            // setParams() riscrive params_ a ogni blocco: il bend deve sopravvivere, come modWheel.
            synth.setParams (p);
            const auto hz = harness::measureFundamentalHz (synth, 48000.0, 4800, 24000);
            expectWithinAbsoluteError (hz, hzFor (69, 2.0f), 4.0f);
        }

        beginTest ("bend verso il basso e ritorno al centro");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            auto p = harness::defaultParams();
            p.pitchBendRangeSemitones = 12;
            synth.setParams (p);

            juce::MidiBuffer down;
            down.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            down.addEvent (juce::MidiMessage::pitchWheel (1, wheelFor (-1.0f)), 1);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, down);
            synth.setParams (p);
            expectWithinAbsoluteError (harness::measureFundamentalHz (synth, 48000.0, 4800, 24000),
                                       hzFor (69, -12.0f), 3.0f);

            juce::MidiBuffer centre;
            centre.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
            buffer.clear();
            synth.process (buffer, centre);
            synth.setParams (p);
            expectWithinAbsoluteError (harness::measureFundamentalHz (synth, 48000.0, 4800, 24000),
                                       440.0f, 3.0f);
        }

        beginTest ("con pbRange a zero la rotella non fa niente");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            auto p = harness::defaultParams();
            p.pitchBendRangeSemitones = 0;
            synth.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheelFor (1.0f)), 1);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);
            synth.setParams (p);

            expectWithinAbsoluteError (harness::measureFundamentalHz (synth, 48000.0, 4800, 24000),
                                       440.0f, 3.0f);
        }
    }
};

static PitchBendTests pitchBendTests;
```

- [ ] **Step 2: Registra il file e fallo fallire**

In `CMakeLists.txt`, nella lista `target_sources(XerumTests ...)`, dopo `Tests/ArpeggiatorTests.cpp`:

```cmake
        Tests/PitchBendTests.cpp
```

Poi:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --target XerumTests
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```

Atteso: i tre test con bend falliscono (la frequenza resta 440 Hz), il primo passa.

- [ ] **Step 3: Leggi il messaggio di pitch wheel**

In `Source/engine/SynthEngine.cpp`, in `handleMidiEvent`, subito dopo il ramo di CC 1 (`:191-198`):

```cpp
    if (message.isPitchWheel())
    {
        // -1..1 attorno a 8192, che e' il centro della corsa a 14 bit. Come modWheel_: campo del
        // solo thread audio, letto una volta per blocco in process().
        pitchBend_ = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f;
        return;
    }
```

In `Source/engine/SynthEngine.h`, accanto a `float modWheel_`:

```cpp
    float pitchBend_ { 0.0f }; // rotella di pitch, -1..1, solo thread audio
```

- [ ] **Step 4: Propaga ai parametri del blocco**

In `Source/engine/SynthEngine.cpp`, accanto a `params_.modWheel = modWheel_;` (`:584`):

```cpp
    params_.pitchBend = pitchBend_;
```

`pitchBendRangeSemitones` invece arriva già da `setParams()`: è un parametro dell'APVTS, non un messaggio.

- [ ] **Step 5: Somma il bend all'intonazione**

In `Source/engine/SynthVoice.cpp`, sostituisci la riga `:592`:

```cpp
    const auto fineCents = value (params::ParamSlot::fine, params_.fineCents, &params::fineCentsFromRaw);

    // Il bend e' un addendo dell'offset in semitoni, come oct, semi e fine: entra dalla stessa
    // porta, si compone con il glide per costruzione (il glide muove il numero di nota, questo
    // muove l'offset, updatePitch() li somma) e con `pitchBend` a zero somma 0.0f, quindi chi
    // non tocca la rotella ottiene gli stessi bit di prima.
    const auto bendSemitones = params_.pitchBend * (float) params_.pitchBendRangeSemitones;
    tuningSemitones_ = (float) (12 * params_.octave + params_.semitones) + fineCents * 0.01f + bendSemitones;
```

- [ ] **Step 6: Verifica**

```bash
cmake --build --preset macos-debug --target XerumTests
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```

Atteso: `ALL TESTS PASSED`. In particolare i test di `GlideVoiceModeTests` e `ArpeggiatorTests` devono restare verdi: con `pitchBend` a zero il segnale è bit-identico.

- [ ] **Step 7: Commit**

```bash
git add Tests/PitchBendTests.cpp CMakeLists.txt Source/engine/SynthEngine.h Source/engine/SynthEngine.cpp Source/engine/SynthVoice.cpp
git commit -m "Add pitch bend to the engine"
```

---

## FASE 2 — Il canale MIDI del bridge

### Task 4: Il mask delle note attive nel motore

**Files:**
- Modify: `Source/engine/SynthEngine.h`, `Source/engine/SynthEngine.cpp` (`handleMidiEvent`, `reset`)
- Modify: `Source/engine/MeterFrame.h`
- Modify: `Source/plugin/PluginProcessor.cpp:298` (pubblicazione)
- Test: `Tests/PitchBendTests.cpp` → rinominato concettualmente no; i test del mask vanno in `Tests/EngineTests.cpp`

**Interfaces:**
- Produces: `engine::SynthEngine::getActiveNotesLo() -> juce::uint64`, `::getActiveNotesHi() -> juce::uint64`; `engine::MeterFrame::notesLo`, `::notesHi`.

- [ ] **Step 1: Scrivi il test che fallisce**

In `Tests/EngineTests.cpp`, in coda a `runTest()`:

```cpp
        beginTest ("il mask delle note segue cio' che il motore sta suonando");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);
            synth.setParams (defaultParams());

            const auto bitOf = [] (const engine::SynthEngine& s, int note)
            {
                const auto mask = note < 64 ? s.getActiveNotesLo() : s.getActiveNotesHi();
                return (mask & (juce::uint64 (1) << (note % 64))) != 0;
            };

            juce::AudioBuffer<float> buffer (2, 128);

            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            on.addEvent (juce::MidiMessage::noteOn (1, 100, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, on);

            expect (bitOf (synth, 60), "il DO centrale deve risultare acceso");
            expect (bitOf (synth, 100), "una nota sopra il 64 finisce nella meta' alta");
            expect (! bitOf (synth, 61), "una nota mai suonata resta spenta");

            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buffer.clear();
            synth.process (buffer, off);

            expect (! bitOf (synth, 60), "il note-off deve spegnere il bit");
            expect (bitOf (synth, 100), "e non deve toccare le altre note");

            juce::MidiBuffer panic;
            panic.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            buffer.clear();
            synth.process (buffer, panic);

            expectEquals ((int) synth.getActiveNotesLo(), 0, "all notes off pulisce la meta' bassa");
            expectEquals ((int) synth.getActiveNotesHi(), 0, "all notes off pulisce la meta' alta");
        }
```

- [ ] **Step 2: Fallo fallire**

```bash
cmake --build --preset macos-debug --target XerumTests
```

Atteso: errore di compilazione, `getActiveNotesLo` non esiste.

- [ ] **Step 3: Aggiungi il mask al motore**

In `Source/engine/SynthEngine.h`, accanto agli altri atomici di telemetria:

```cpp
    std::atomic<juce::uint64> activeNotesLo_ { 0 };   // note 0..63
    std::atomic<juce::uint64> activeNotesHi_ { 0 };   // note 64..127
```

e fra i getter pubblici, dopo `getArpStep()`:

```cpp
    /**
     * Quali note stanno suonando, un bit per nota MIDI: 0..63 in `Lo`, 64..127 in `Hi`.
     *
     * Istantaneo come `mw` e `arpStep`, e per la stessa ragione: una nota tenuta e' uno **stato**,
     * non un transitorio. Azzerarlo alla lettura spegnerebbe la tastiera della UI non appena il
     * thread audio smette di girare, mentre il tasto e' ancora premuto.
     *
     * I bit si alzano qui, cioe' **a valle dell'arpeggiatore**: ad arp acceso il mask descrive il
     * pattern che suona, non i tasti tenuti. E' la scelta voluta.
     */
    juce::uint64 getActiveNotesLo() const noexcept { return activeNotesLo_.load (std::memory_order_relaxed); }
    juce::uint64 getActiveNotesHi() const noexcept { return activeNotesHi_.load (std::memory_order_relaxed); }
```

- [ ] **Step 4: Alza e abbassa i bit**

In `Source/engine/SynthEngine.cpp`, in `handleMidiEvent`, dentro i rami di nota già esistenti (`:200` e seguenti):

```cpp
    const auto setNoteBit = [this] (int note, bool on) noexcept
    {
        auto& slot = note < 64 ? activeNotesLo_ : activeNotesHi_;
        const auto bit = juce::uint64 (1) << (note % 64);
        const auto current = slot.load (std::memory_order_relaxed);
        slot.store (on ? (current | bit) : (current & ~bit), std::memory_order_relaxed);
    };
```

Chiama `setNoteBit (message.getNoteNumber(), true)` nel ramo `isNoteOn()` e `setNoteBit (message.getNoteNumber(), false)` nel ramo `isNoteOff()`. Aggiungi inoltre un ramo per il panico:

```cpp
    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        activeNotesLo_.store (0, std::memory_order_relaxed);
        activeNotesHi_.store (0, std::memory_order_relaxed);
    }
```

posizionato **prima** del `return` che chiude la gestione, senza togliere il comportamento che quei messaggi hanno già verso le voci. In `SynthEngine::reset()` (`:124`) azzera entrambi gli atomici.

- [ ] **Step 5: Verifica il motore**

```bash
cmake --build --preset macos-debug --target XerumTests
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```

Atteso: `ALL TESTS PASSED`.

- [ ] **Step 6: Porta il mask nel MeterFrame**

In `Source/engine/MeterFrame.h`, dopo `arpStep`:

```cpp
    /** Un bit per nota MIDI: 0..63 in `notesLo`, 64..127 in `notesHi`. Istantanei come `lfo` e
        `mw` — il lettore fa load(), non exchange(0) — perche' una nota tenuta e' uno stato. */
    std::atomic<juce::uint64> notesLo { 0 };
    std::atomic<juce::uint64> notesHi { 0 };
```

`MeterFrame.h` include oggi solo `<atomic>`: aggiungi `#include <juce_core/juce_core.h>` per `juce::uint64`.

In `Source/plugin/PluginProcessor.cpp`, accanto a `meters_.arpStep.store (...)` (`:302`):

```cpp
    // Quali note stanno suonando: e' cio' che accende i tasti nella striscia della UI.
    meters_.notesLo.store (engine_->getActiveNotesLo(), std::memory_order_relaxed);
    meters_.notesHi.store (engine_->getActiveNotesHi(), std::memory_order_relaxed);
```

- [ ] **Step 7: Verifica che il plugin compili**

```bash
cmake --build --preset macos-debug --target SerumStyleSynth
```

Atteso: build pulita.

- [ ] **Step 8: Commit**

```bash
git add Source/engine/SynthEngine.h Source/engine/SynthEngine.cpp Source/engine/MeterFrame.h \
        Source/plugin/PluginProcessor.cpp Tests/EngineTests.cpp
git commit -m "Track which notes are sounding in the meter frame"
```

---

### Task 5: Il mask arriva alla WebUI

**Files:**
- Modify: `Source/bridge/MeterChannel.cpp`
- Modify: `WebUI/src/juce/backend.ts`
- Modify: `WebUI/src/juce/fake-backend.ts`
- Test: `WebUI/src/juce/fake-backend.test.ts`, `WebUI/src/juce/juce-backend.test.ts`

**Interfaces:**
- Produces: `MeterFrame.n0..n3` (quattro interi da 32 bit), `NoteMask`, `noteMaskOf(m)`, `isNoteActive(mask, note)` da `WebUI/src/juce/backend.ts`.

- [ ] **Step 1: Scrivi il test che fallisce**

In `WebUI/src/juce/fake-backend.test.ts`, in coda:

```ts
import { isNoteActive, noteMaskOf, ZERO_METERS } from "./backend";

describe("mask delle note", () => {
  it("legge il bit giusto in ognuna delle quattro parole", () => {
    const frame = { ...ZERO_METERS, n0: 1 << 5, n1: 1 << 0, n2: 1 << 31, n3: 1 << 7 };
    const mask = noteMaskOf(frame);
    expect(isNoteActive(mask, 5)).toBe(true);
    expect(isNoteActive(mask, 32)).toBe(true);
    expect(isNoteActive(mask, 95)).toBe(true);
    expect(isNoteActive(mask, 103)).toBe(true);
    expect(isNoteActive(mask, 6)).toBe(false);
    expect(isNoteActive(mask, 127)).toBe(false);
  });

  it("un frame a zero non ha note accese", () => {
    const mask = noteMaskOf(ZERO_METERS);
    for (let n = 0; n < 128; n++) expect(isNoteActive(mask, n)).toBe(false);
  });
});
```

- [ ] **Step 2: Fallo fallire**

```bash
cd WebUI && pnpm test
```

Atteso: FAIL, `noteMaskOf` non esportato.

- [ ] **Step 3: Estendi il contratto lato web**

In `WebUI/src/juce/backend.ts`, aggiungi i quattro campi a `MeterFrame` (dopo `arpStep`), documentandoli:

```ts
  /**
   * Quali note stanno suonando, un bit per nota MIDI: `n0` copre 0..31, `n3` copre 96..127.
   *
   * Quattro parole da 32 bit e non due da 64 perché un uint64 non entra esatto nella mantissa di
   * un double, e il frame viaggia come JSON. Il contratto sta in Source/engine/MeterFrame.h e in
   * Source/bridge/MeterChannel.cpp.
   */
  n0: number;
  n1: number;
  n2: number;
  n3: number;
```

Aggiorna `ZERO_METERS` con `n0: 0, n1: 0, n2: 0, n3: 0` e aggiungi in coda al file:

```ts
export type NoteMask = readonly [number, number, number, number];

export const noteMaskOf = (m: MeterFrame): NoteMask => [m.n0, m.n1, m.n2, m.n3];

/** Il bit della nota. Gli operatori bit a bit di JS lavorano su int32: una parola oltre 2^31
    arriva qui come numero positivo grande e viene riconvertita a int32 dall'`&`, quindi il
    confronto resta corretto anche per il bit più alto. */
export const isNoteActive = (mask: NoteMask, note: number): boolean =>
  ((mask[note >> 5] ?? 0) & (1 << (note & 31))) !== 0;
```

- [ ] **Step 4: Fai suonare qualcosa al backend finto**

In `WebUI/src/juce/fake-backend.ts`, dentro `startDemo()`: la nota finta che già guida `env` e `vel` deve accendere anche il bit corrispondente nel frame emesso, così la tastiera in Storybook si illumina in tempo con l'inviluppo.

```ts
    // La stessa nota finta che muove env/vel accende il suo bit: senza, in Storybook la tastiera
    // resterebbe spenta mentre tutto il resto respira.
    const fakeNote = 48 + (noteIndex % 12);
    const bits = [0, 0, 0, 0];
    if (noteHeld) bits[fakeNote >> 5] |= 1 << (fakeNote & 31);

    this.emitMeters({ ...frame, n0: bits[0]!, n1: bits[1]!, n2: bits[2]!, n3: bits[3]! });
```

`noteIndex` e `noteHeld` sono le variabili che il clock finto usa già per pilotare l'inviluppo: riusale invece di introdurne di nuove. Se hanno altri nomi nel file, adegua queste due righe.

- [ ] **Step 5: Verifica**

```bash
cd WebUI && pnpm test && pnpm typecheck
```

Atteso: PASS.

- [ ] **Step 6: Manda i quattro numeri dal C++**

In `Source/bridge/MeterChannel.cpp`, nel `timerCallback`, dopo `arpStep`:

```cpp
    // Quattro parole da 32 bit e non due da 64: un uint64 non entra esatto nella mantissa di un
    // double, e questo frame viaggia come JSON. Il lettore le ricompone in WebUI/src/juce/backend.ts.
    const auto lo = frame_.notesLo.load (std::memory_order_relaxed);
    const auto hi = frame_.notesHi.load (std::memory_order_relaxed);
    obj->setProperty ("n0", (double) (juce::uint32) (lo & 0xffffffffu));
    obj->setProperty ("n1", (double) (juce::uint32) (lo >> 32));
    obj->setProperty ("n2", (double) (juce::uint32) (hi & 0xffffffffu));
    obj->setProperty ("n3", (double) (juce::uint32) (hi >> 32));
```

- [ ] **Step 7: Verifica**

```bash
cmake --build --preset macos-debug --target SerumStyleSynth
cd WebUI && pnpm test && pnpm typecheck && cd ..
```

- [ ] **Step 8: Commit**

```bash
git add Source/bridge/MeterChannel.cpp WebUI/src/juce/backend.ts WebUI/src/juce/fake-backend.ts WebUI/src/juce/fake-backend.test.ts
git commit -m "Send the sounding-note mask to the Web UI"
```

---

### Task 6: `MidiChannel`, il verso web → nativo

**Files:**
- Create: `Source/bridge/MidiChannel.h`, `Source/bridge/MidiChannel.cpp`
- Modify: `Source/plugin/PluginProcessor.h`, `Source/plugin/PluginProcessor.cpp:275`
- Modify: `Source/plugin/PluginEditor.h`, `Source/plugin/PluginEditor.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: native function `noteOn(note, velocity)`, `noteOff(note)`, `allNotesOff()`, `setWheel(kind, value)` con `kind` `"pitch"` o `"mod"` e `value` 0..1; `SerumStyleSynthAudioProcessor::setUiPitchBend(float)`, `::setUiModWheel(float)`.

- [ ] **Step 1: Gli atomici delle wheel nel processor**

In `Source/plugin/PluginProcessor.h`, fra i membri privati:

```cpp
    /**
     * Le due rotelle disegnate nella UI, in unita' MIDI grezze: 0..16383 per il pitch (8192 e' il
     * centro), 0..127 per il mod wheel. `-1` significa "mai toccata": finche' resta li' il
     * processor non inietta niente, cosi' una UI mai aperta non sovrascrive un controller vero.
     *
     * Non passano da MidiKeyboardState — quello trasporta solo note — ma diventano **veri
     * messaggi MIDI** anteposti al buffer in processBlock: da li' in giu' arp, motore e mod matrix
     * non sanno ne' devono sapere che quella rotella e' disegnata.
     */
    std::atomic<int> uiPitchBend_ { -1 };
    std::atomic<int> uiModWheel_ { -1 };
    int lastSentPitchBend_ { -1 };   // solo thread audio
    int lastSentModWheel_ { -1 };    // solo thread audio
```

e fra i metodi pubblici:

```cpp
    /** Posizione della rotella di pitch dalla UI, 0..1 (0.5 = centro). Message thread. */
    void setUiPitchBend (float value01) noexcept
    {
        uiPitchBend_.store (juce::jlimit (0, 16383, juce::roundToInt (value01 * 16383.0f)),
                            std::memory_order_relaxed);
    }

    /** Posizione del mod wheel dalla UI, 0..1. Message thread. */
    void setUiModWheel (float value01) noexcept
    {
        uiModWheel_.store (juce::jlimit (0, 127, juce::roundToInt (value01 * 127.0f)),
                           std::memory_order_relaxed);
    }
```

- [ ] **Step 2: Inietta i messaggi nel buffer**

In `Source/plugin/PluginProcessor.cpp`, **subito prima** della riga `keyboardState_.processNextMidiBuffer (...)` (`:275`):

```cpp
    // Le rotelle della UI diventano messaggi veri, e solo quando cambiano: mandarli a ogni blocco
    // riscriverebbe di continuo sopra un controller hardware che sta mandando gli stessi CC.
    if (const auto bend = uiPitchBend_.load (std::memory_order_relaxed);
        bend >= 0 && bend != lastSentPitchBend_)
    {
        midi.addEvent (juce::MidiMessage::pitchWheel (1, bend), 0);
        lastSentPitchBend_ = bend;
    }

    if (const auto mw = uiModWheel_.load (std::memory_order_relaxed);
        mw >= 0 && mw != lastSentModWheel_)
    {
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, mw), 0);
        lastSentModWheel_ = mw;
    }
```

In `prepareToPlay`, accanto a `keyboardState_.reset()` (`:213`), rimetti `lastSentPitchBend_` e `lastSentModWheel_` a `-1`.

- [ ] **Step 3: Scrivi il canale**

Crea `Source/bridge/MidiChannel.h`:

```cpp
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

class SerumStyleSynthAudioProcessor;

namespace bridge
{
/**
 * Le note e le rotelle suonate dentro la WebView.
 *
 * Native function: noteOn(note, velocity), noteOff(note), allNotesOff(), setWheel(kind, value).
 *
 * Le note finiscono nel MidiKeyboardState del processor, che processBlock() fonde gia' nel flusso
 * dell'host: una nota della UI e' quindi indistinguibile da una del DAW e non esiste codice nuovo
 * a valle. Le rotelle non possono passare di li' — MidiKeyboardState trasporta solo note — e
 * vanno invece in due atomici che il processor traduce in messaggi MIDI veri.
 *
 * Nessun evento verso la UI: quali note suonano lo dice il mask dentro il frame `meters`.
 */
class MidiChannel final
{
public:
    MidiChannel (juce::MidiKeyboardState& keyboardState, SerumStyleSynthAudioProcessor& processor);

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

private:
    juce::MidiKeyboardState& keyboardState_;
    SerumStyleSynthAudioProcessor& processor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiChannel)
};
} // namespace bridge
```

Crea `Source/bridge/MidiChannel.cpp`:

```cpp
#include "bridge/MidiChannel.h"

#include "plugin/PluginProcessor.h"

namespace bridge
{
namespace
{
/** Il canale su cui la UI suona. Uno solo: la striscia non ha un selettore di canale. */
constexpr int kChannel = 1;

int noteArg (const juce::Array<juce::var>& args, int index)
{
    return index < args.size() ? juce::jlimit (0, 127, (int) args[index]) : -1;
}
} // namespace

MidiChannel::MidiChannel (juce::MidiKeyboardState& keyboardState,
                          SerumStyleSynthAudioProcessor& processor)
    : keyboardState_ (keyboardState), processor_ (processor)
{
}

juce::WebBrowserComponent::Options MidiChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("noteOn",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 const auto note = noteArg (args, 0);
                                 const auto velocity = args.size() > 1 ? (float) args[1] : 0.8f;

                                 if (note >= 0)
                                     keyboardState_.noteOn (kChannel, note, juce::jlimit (0.0f, 1.0f, velocity));

                                 done (juce::var());
                             })
        .withNativeFunction ("noteOff",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (const auto note = noteArg (args, 0); note >= 0)
                                     keyboardState_.noteOff (kChannel, note, 0.0f);

                                 done (juce::var());
                             })
        .withNativeFunction ("allNotesOff",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 // Non e' cosmetico: se la WebView perde il puntatore a meta'
                                 // click, la nota resta appesa e suona per sempre.
                                 keyboardState_.allNotesOff (kChannel);
                                 done (juce::var());
                             })
        .withNativeFunction ("setWheel",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.size() >= 2)
                                 {
                                     const auto kind = args[0].toString();
                                     const auto value = juce::jlimit (0.0f, 1.0f, (float) args[1]);

                                     if (kind == "pitch")
                                         processor_.setUiPitchBend (value);
                                     else if (kind == "mod")
                                         processor_.setUiModWheel (value);
                                 }

                                 done (juce::var());
                             });
}
} // namespace bridge
```

- [ ] **Step 4: Cabla il canale nell'editor**

In `Source/plugin/PluginEditor.h`: aggiungi `#include "bridge/MidiChannel.h"` e, **prima** di `webView_` (come `relays_` e `stateChannel_`, perché le Options si costruiscono da lui):

```cpp
    /** Note e rotelle suonate dentro la WebView: anche lui prima di webView_. */
    bridge::MidiChannel midiChannel_;
```

In `Source/plugin/PluginEditor.cpp`: aggiungi il parametro a `makeWebOptions` e applicalo dopo `stateChannel_`:

```cpp
    // Il canale MIDI aggiunge noteOn / noteOff / allNotesOff / setWheel.
    options = midiChannel_.applyTo (options);
```

e nella lista di inizializzazione, prima di `webView_`:

```cpp
      midiChannel_ (p.getKeyboardState(), p),
```

- [ ] **Step 5: Registra il sorgente e compila**

In `CMakeLists.txt`, in `target_sources(SerumStyleSynth ...)`, dopo `Source/bridge/MeterChannel.cpp`:

```cmake
        Source/bridge/MidiChannel.cpp
```

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --target SerumStyleSynth
```

Atteso: build pulita.

- [ ] **Step 6: Prova a mano**

Apri lo Standalone (`scripts/dev.sh`), poi nella console della WebView:

```js
window.__JUCE__.backend.emitEvent // deve esistere
```

La verifica vera arriva con la UI: qui basta che il plugin si apra senza crash e che la tastiera nativa (ancora presente) suoni come prima.

- [ ] **Step 7: Commit**

```bash
git add Source/bridge/MidiChannel.h Source/bridge/MidiChannel.cpp Source/plugin/PluginProcessor.h \
        Source/plugin/PluginProcessor.cpp Source/plugin/PluginEditor.h Source/plugin/PluginEditor.cpp CMakeLists.txt
git commit -m "Add the MIDI bridge channel for notes and wheels"
```

---

### Task 7: I metodi MIDI nel `Backend` lato web

**Files:**
- Modify: `WebUI/src/juce/backend.ts` (interfaccia `Backend`)
- Modify: `WebUI/src/juce/juce-backend.ts`, `WebUI/src/juce/fake-backend.ts`
- Test: `WebUI/src/juce/fake-backend.test.ts`

**Interfaces:**
- Consumes: le native function di Task 6.
- Produces: `Backend.noteOn(note, velocity)`, `.noteOff(note)`, `.allNotesOff()`, `.setWheel(kind, value)` — tutte `Promise<void>`.

- [ ] **Step 1: Scrivi il test che fallisce**

In `WebUI/src/juce/fake-backend.test.ts`:

```ts
describe("note e rotelle", () => {
  it("registra le note suonate dalla UI", async () => {
    const b = new FakeBackend();
    await b.noteOn(60, 0.8);
    await b.noteOn(64, 0.8);
    await b.noteOff(60);
    expect([...b.playing]).toEqual([64]);
  });

  it("allNotesOff svuota tutto", async () => {
    const b = new FakeBackend();
    await b.noteOn(60, 0.8);
    await b.noteOn(64, 0.8);
    await b.allNotesOff();
    expect(b.playing.size).toBe(0);
  });

  it("le rotelle restano dove le si lascia", async () => {
    const b = new FakeBackend();
    await b.setWheel("mod", 0.25);
    await b.setWheel("pitch", 0.75);
    expect(b.wheels).toEqual({ pitch: 0.75, mod: 0.25 });
  });
});
```

- [ ] **Step 2: Fallo fallire**

```bash
cd WebUI && pnpm test
```

Atteso: FAIL, `noteOn` non esiste su `FakeBackend`.

- [ ] **Step 3: Estendi l'interfaccia**

In `WebUI/src/juce/backend.ts`, in `interface Backend`:

```ts
  /** Note suonate dentro la UI. Finiscono nel MidiKeyboardState del processor: dal punto di vista
      del motore sono indistinguibili da quelle dell'host. `velocity` è 0..1. */
  noteOn(note: number, velocity: number): Promise<void>;
  noteOff(note: number): Promise<void>;
  /** Da chiamare su pointercancel, blur e smontaggio: senza, una nota può restare appesa. */
  allNotesOff(): Promise<void>;
  /** Posizione di una rotella, 0..1. `pitch` ha il centro a 0.5. */
  setWheel(kind: "pitch" | "mod", value: number): Promise<void>;
```

- [ ] **Step 4: Implementa in `JuceBackend`**

In `WebUI/src/juce/juce-backend.ts`, dentro `createJuceBackend`, accanto alle altre chiamate che usano `call(...)`:

```ts
  const noteOnFn = call("noteOn");
  const noteOffFn = call("noteOff");
  const allNotesOffFn = call("allNotesOff");
  const setWheelFn = call("setWheel");
```

e nell'oggetto restituito:

```ts
    async noteOn(note, velocity) { await noteOnFn(note, velocity); },
    async noteOff(note) { await noteOffFn(note); },
    async allNotesOff() { await allNotesOffFn(); },
    async setWheel(kind, value) { await setWheelFn(kind, value); },
```

- [ ] **Step 5: Implementa in `FakeBackend`**

In `WebUI/src/juce/fake-backend.ts`:

```ts
  /** Le note che la UI sta tenendo premute. Pubblico: è ciò su cui i test guardano. */
  readonly playing = new Set<number>();
  wheels = { pitch: 0.5, mod: 0 };

  async noteOn(note: number, _velocity: number) { this.playing.add(note); }
  async noteOff(note: number) { this.playing.delete(note); }
  async allNotesOff() { this.playing.clear(); }
  async setWheel(kind: "pitch" | "mod", value: number) { this.wheels = { ...this.wheels, [kind]: value }; }
```

Nel clock finto, accendi i bit `n0..n3` del frame emesso anche per le note in `playing`, così la tastiera in Storybook reagisce ai click.

- [ ] **Step 6: Verifica**

```bash
cd WebUI && pnpm test && pnpm typecheck
```

Atteso: PASS.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/juce/backend.ts WebUI/src/juce/juce-backend.ts WebUI/src/juce/fake-backend.ts WebUI/src/juce/fake-backend.test.ts
git commit -m "Expose notes and wheels on the Web UI backend"
```

---

## FASE 3 — Le primitive

### Task 8: La primitiva `Wheel`

**Files:**
- Create: `WebUI/packages/ui/src/components/Wheel/Wheel.tsx`, `Wheel.test.tsx`, `Wheel.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Produces:

```ts
export type WheelProps = {
  /** 0..1. Per una rotella bipolare il centro è 0.5. */
  value: number;
  onChange: (v: number) => void;
  /** Nome accessibile dello slider. */
  label: string;
  /** Valore di riposo: dove torna il doppio click e, con springBack, il rilascio. Default 0. */
  defaultValue?: number;
  /** Riempimento e detent a partire dal centro invece che dal basso. */
  bipolar?: boolean;
  /** Al rilascio torna a defaultValue, come una rotella di pitch molleggiata. */
  springBack?: boolean;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
};
```

- [ ] **Step 1: Scrivi i test che falliscono**

Crea `WebUI/packages/ui/src/components/Wheel/Wheel.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Wheel } from "./Wheel";

describe("Wheel", () => {
  it("è uno slider verticale", () => {
    render(<Wheel value={0.5} onChange={() => {}} label="Pitch" />);
    const s = screen.getByRole("slider", { name: "Pitch" });
    expect(s).toHaveAttribute("aria-orientation", "vertical");
    expect(s).toHaveAttribute("aria-valuenow", "0.5");
  });

  it("il trascinamento verso l'alto aumenta il valore", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.5} onChange={onChange} label="Mod" />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerMove(s, { clientX: 0, clientY: 60, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("con springBack torna al riposo al rilascio", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.8} defaultValue={0.5} onChange={onChange} label="Pitch" springBack bipolar />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerUp(s, { clientX: 0, clientY: 100, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(0.5);
  });

  it("senza springBack resta dov'è al rilascio", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.8} onChange={onChange} label="Mod" />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerUp(s, { clientX: 0, clientY: 100, pointerId: 1 });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("bipolare: il riempimento parte dal centro", () => {
    render(<Wheel value={0.75} defaultValue={0.5} onChange={() => {}} label="Pitch" bipolar />);
    const fill = screen.getByTestId("wheel-fill");
    expect(fill.style.height).toBe("25%");
    expect(fill.style.bottom).toBe("50%");
  });

  it("unipolare: il riempimento parte dal basso", () => {
    render(<Wheel value={0.4} onChange={() => {}} label="Mod" />);
    const fill = screen.getByTestId("wheel-fill");
    expect(fill.style.height).toBe("40%");
    expect(fill.style.bottom).toBe("0%");
  });

  it("è inerte quando disabilitata", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.5} onChange={onChange} label="Mod" disabled />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });
});
```

- [ ] **Step 2: Fallo fallire**

```bash
cd WebUI && pnpm ui:test
```

Atteso: FAIL, modulo `./Wheel` non trovato.

- [ ] **Step 3: Scrivi il componente**

Crea `WebUI/packages/ui/src/components/Wheel/Wheel.tsx`:

```tsx
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type WheelProps = {
  /** 0..1. Per una rotella bipolare il centro e' 0.5. */
  value: number;
  onChange: (v: number) => void;
  /** Nome accessibile dello slider. */
  label: string;
  /** Valore di riposo: dove torna il doppio click e, con springBack, il rilascio. */
  defaultValue?: number;
  /** Riempimento e detent a partire dal centro invece che dal basso. */
  bipolar?: boolean;
  /** Al rilascio torna a defaultValue, come una rotella di pitch molleggiata. */
  springBack?: boolean;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
};

/**
 * Rotella verticale: pitch e modulazione della striscia bassa.
 *
 * Non e' un Fader stretto. Un fader ha una scala — cinque tacche e un readout numerico — perche'
 * il suo valore si legge; una rotella si guarda solo per sapere dov'e' rispetto al riposo, e il
 * suo riposo per il pitch sta al centro, non in fondo. Da qui le due differenze che contano:
 * niente tacche, e un riempimento che puo' partire dal centro.
 */
export function Wheel({
  value,
  onChange,
  label,
  defaultValue = 0,
  bipolar = false,
  springBack = false,
  tone,
  disabled = false,
  className,
}: WheelProps) {
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue,
    onChange,
    disabled,
    axis: "y",
    // Il ritorno a riposo e' esattamente "la gesture e' finita": useDragValue chiama onChangeEnd
    // su pointer-up, pointer-cancel, rotella, tastiera e doppio click, cioe' in tutti i casi in
    // cui una rotella vera tornerebbe al centro.
    onChangeEnd: springBack ? () => onChange(defaultValue) : undefined,
  });

  // Bipolare: il riempimento cresce dal centro verso l'alto o verso il basso. Unipolare: dal
  // fondo, come un fader. In entrambi i casi e' `bottom` + `height`, mai un transform.
  const fill = bipolar
    ? { bottom: `${Math.min(value, defaultValue) * 100}%`, height: `${Math.abs(value - defaultValue) * 100}%` }
    : { bottom: "0%", height: `${value * 100}%` };

  return (
    <div
      data-slot="wheel"
      data-dragging={dragging}
      className={cn("group/wheel flex flex-col items-center gap-1", className)}
      style={toneStyle(tone)}
    >
      <div
        ref={ref}
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-orientation="vertical"
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-disabled={disabled || undefined}
        data-dragging={dragging}
        className={cn(
          "relative w-5 flex-1 cursor-ns-resize overflow-hidden rounded-control outline-none select-none touch-none",
          "bg-linear-to-r from-cap-lo via-cap-hi to-cap-lo shadow-well",
          "focus-visible:ring-2 focus-visible:ring-(--tone) focus-visible:ring-offset-2 focus-visible:ring-offset-background",
          disabled && "cursor-not-allowed opacity-50",
        )}
        {...handlers}
      >
        <div data-testid="wheel-fill" className="absolute inset-x-px rounded-[2px] bg-(--tone)" style={fill} />
        {/* Il segno del riposo: al centro per la bipolare, assente per l'altra. */}
        {bipolar && <span aria-hidden className="absolute inset-x-0 top-1/2 h-px bg-tick" />}
      </div>
      <span className={cn("text-(length:--text-label)/4", disabled ? "text-text-dim" : "text-muted-foreground")}>
        {label}
      </span>
    </div>
  );
}
```

- [ ] **Step 4: Verifica**

```bash
cd WebUI && pnpm ui:test
```

Atteso: PASS.

- [ ] **Step 5: Esporta e scrivi le storie**

In `WebUI/packages/ui/src/index.ts`:

```ts
export { Wheel, type WheelProps } from "@/components/Wheel/Wheel";
```

Crea `Wheel.stories.tsx` sul modello di `Fader.stories.tsx`, con un componente `Controlled` e le storie `Pitch` (`bipolar`, `springBack`, `defaultValue: 0.5`), `Mod`, `Tones`, `Disabled`.

- [ ] **Step 6: Verifica e commit**

```bash
cd WebUI && pnpm ui:test && pnpm ui:build && pnpm typecheck && cd ..
git add WebUI/packages/ui/src/components/Wheel WebUI/packages/ui/src/index.ts
git commit -m "Add the Wheel primitive"
```

---

### Task 9: La primitiva `Keybed`

**Files:**
- Create: `WebUI/packages/ui/src/components/Keybed/Keybed.tsx`, `Keybed.test.tsx`, `Keybed.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Produces:

```ts
export type KeybedNoteMask = readonly [number, number, number, number];

export type KeybedProps = {
  /** Nota MIDI del primo tasto bianco visibile. */
  firstNote: number;
  /** Quante ottave mostrare. Default 4. */
  octaves?: number;
  /** Velocity 0..1 assegnata a ogni nota suonata col puntatore. */
  velocity: number;
  onNoteOn: (note: number, velocity: number) => void;
  onNoteOff: (note: number) => void;
  onAllNotesOff: () => void;
  /**
   * Sottoscrizione al mask delle note attive. NON è una prop di valore: se lo fosse, i 30 frame
   * al secondo dell'host farebbero rirenderizzare 48 nodi trenta volte al secondo dentro una
   * WebView. Il componente si iscrive una volta e muta le classi via ref.
   */
  subscribeNotes?: (cb: (mask: KeybedNoteMask) => void) => () => void;
  className?: string;
};
```

- [ ] **Step 1: Scrivi i test che falliscono**

Crea `WebUI/packages/ui/src/components/Keybed/Keybed.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Keybed } from "./Keybed";

const noop = () => {};

describe("Keybed", () => {
  it("disegna sette tasti bianchi e cinque neri per ottava", () => {
    render(<Keybed firstNote={48} octaves={2} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={noop} />);
    expect(screen.getAllByTestId("key-white")).toHaveLength(14);
    expect(screen.getAllByTestId("key-black")).toHaveLength(10);
  });

  it("il primo tasto bianco è firstNote", () => {
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={noop} />);
    expect(screen.getAllByTestId("key-white")[0]).toHaveAttribute("data-note", "48");
  });

  it("premere un tasto manda noteOn con la velocity data", () => {
    const onNoteOn = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.6} onNoteOn={onNoteOn} onNoteOff={noop} onAllNotesOff={noop} />);
    fireEvent.pointerDown(screen.getAllByTestId("key-white")[2], { button: 0, pointerId: 1 });
    expect(onNoteOn).toHaveBeenCalledWith(52, 0.6);
  });

  it("rilasciare manda noteOff", () => {
    const onNoteOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.6} onNoteOn={noop} onNoteOff={onNoteOff} onAllNotesOff={noop} />);
    const key = screen.getAllByTestId("key-white")[0];
    fireEvent.pointerDown(key, { button: 0, pointerId: 1 });
    fireEvent.pointerUp(key, { pointerId: 1 });
    expect(onNoteOff).toHaveBeenCalledWith(48);
  });

  it("trascinare da un tasto all'altro spegne il primo e accende il secondo", () => {
    const onNoteOn = vi.fn();
    const onNoteOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={onNoteOn} onNoteOff={onNoteOff} onAllNotesOff={noop} />);
    const keys = screen.getAllByTestId("key-white");
    fireEvent.pointerDown(keys[0], { button: 0, pointerId: 1 });
    fireEvent.pointerEnter(keys[1], { buttons: 1, pointerId: 1 });
    expect(onNoteOff).toHaveBeenCalledWith(48);
    expect(onNoteOn).toHaveBeenLastCalledWith(50, 0.8);
  });

  it("pointercancel chiama onAllNotesOff", () => {
    const onAllNotesOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={onAllNotesOff} />);
    fireEvent.pointerCancel(screen.getAllByTestId("key-white")[0], { pointerId: 1 });
    expect(onAllNotesOff).toHaveBeenCalled();
  });

  it("accende i tasti dal mask senza rirenderizzare", () => {
    let emit: ((m: readonly [number, number, number, number]) => void) | null = null;
    const subscribeNotes = (cb: (m: readonly [number, number, number, number]) => void) => {
      emit = cb;
      return () => { emit = null; };
    };
    render(
      <Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop}
              onAllNotesOff={noop} subscribeNotes={subscribeNotes} />,
    );
    const keys = screen.getAllByTestId("key-white");
    expect(keys[0]).toHaveAttribute("data-active", "false");
    emit!([1 << 16, 0, 0, 0]); // nota 48
    expect(keys[0]).toHaveAttribute("data-active", "true");
    emit!([0, 0, 0, 0]);
    expect(keys[0]).toHaveAttribute("data-active", "false");
  });
});
```

- [ ] **Step 2: Fallo fallire**

```bash
cd WebUI && pnpm ui:test
```

Atteso: FAIL, modulo `./Keybed` non trovato.

- [ ] **Step 3: Scrivi il componente**

Crea `WebUI/packages/ui/src/components/Keybed/Keybed.tsx`:

```tsx
import { useCallback, useEffect, useRef } from "react";
import { cn } from "@/lib/utils";

export type KeybedNoteMask = readonly [number, number, number, number];

export type KeybedProps = {
  /** Nota MIDI del primo tasto bianco visibile. */
  firstNote: number;
  /** Quante ottave mostrare. */
  octaves?: number;
  /** Velocity 0..1 assegnata a ogni nota suonata col puntatore. */
  velocity: number;
  onNoteOn: (note: number, velocity: number) => void;
  onNoteOff: (note: number) => void;
  onAllNotesOff: () => void;
  /**
   * Sottoscrizione al mask delle note attive. NON e' una prop di valore: se lo fosse, i 30 frame
   * al secondo dell'host farebbero rirenderizzare 48 nodi trenta volte al secondo dentro una
   * WebView. Il componente si iscrive una volta e muta `data-active` via ref.
   */
  subscribeNotes?: (cb: (mask: KeybedNoteMask) => void) => () => void;
  className?: string;
};

/** I semitoni dei tasti bianchi e di quelli neri dentro un'ottava. */
const WHITE = [0, 2, 4, 5, 7, 9, 11];
const BLACK = [1, 3, 6, 8, 10];

/** Quanti tasti bianchi stanno alla sinistra di un nero: decide la sua posizione orizzontale. */
const WHITE_BEFORE: Record<number, number> = { 1: 1, 3: 2, 6: 4, 8: 5, 10: 6 };

const bitOf = (mask: KeybedNoteMask, note: number) =>
  ((mask[note >> 5] ?? 0) & (1 << (note & 31))) !== 0;

/** Tastiera suonabile col puntatore, con i tasti che si accendono su cio' che il motore suona. */
export function Keybed({
  firstNote,
  octaves = 4,
  velocity,
  onNoteOn,
  onNoteOff,
  onAllNotesOff,
  subscribeNotes,
  className,
}: KeybedProps) {
  // La nota attualmente premuta col puntatore. Un ref e non uno stato: cambiarla non deve
  // ridisegnare la tastiera, e il gestore di pointermove la legge sempre aggiornata.
  const held = useRef<number | null>(null);
  const keys = useRef(new Map<number, HTMLElement>());
  const latest = useRef({ velocity, onNoteOn, onNoteOff, onAllNotesOff });
  latest.current = { velocity, onNoteOn, onNoteOff, onAllNotesOff };

  const press = useCallback((note: number) => {
    if (held.current === note) return;
    if (held.current !== null) latest.current.onNoteOff(held.current);
    held.current = note;
    latest.current.onNoteOn(note, latest.current.velocity);
  }, []);

  const release = useCallback(() => {
    if (held.current === null) return;
    latest.current.onNoteOff(held.current);
    held.current = null;
  }, []);

  const panic = useCallback(() => {
    held.current = null;
    latest.current.onAllNotesOff();
  }, []);

  // Una nota appesa non e' un difetto estetico: suona per sempre. Il puntatore puo' sparire
  // senza pointerup — cambio di finestra, pointercancel della WebView — quindi si chiude anche
  // sul blur della finestra.
  useEffect(() => {
    window.addEventListener("blur", panic);
    return () => window.removeEventListener("blur", panic);
  }, [panic]);

  useEffect(() => {
    if (!subscribeNotes) return;
    return subscribeNotes((mask) => {
      for (const [note, el] of keys.current) el.dataset.active = String(bitOf(mask, note));
    });
  }, [subscribeNotes]);

  const register = useCallback((note: number) => (el: HTMLElement | null) => {
    if (el) keys.current.set(note, el);
    else keys.current.delete(note);
  }, []);

  const whiteNotes: number[] = [];
  const blackNotes: { note: number; index: number }[] = [];

  for (let o = 0; o < octaves; o++) {
    for (const semi of WHITE) whiteNotes.push(firstNote + o * 12 + semi);
    for (const semi of BLACK) blackNotes.push({ note: firstNote + o * 12 + semi, index: o * 7 + WHITE_BEFORE[semi]! });
  }

  const unit = 100 / whiteNotes.length;

  return (
    <div
      data-slot="keybed"
      className={cn("relative flex h-full w-full select-none touch-none", className)}
      onPointerDown={(e) => {
        if (e.button !== 0) return;
        e.currentTarget.setPointerCapture?.(e.pointerId);
      }}
      onPointerUp={release}
      onPointerLeave={release}
      onPointerCancel={panic}
    >
      {whiteNotes.map((note) => (
        <div
          key={note}
          ref={register(note)}
          data-testid="key-white"
          data-note={note}
          data-active="false"
          className={cn(
            "h-full flex-1 rounded-b-[3px] border-r border-edge-dark last:border-r-0",
            "bg-linear-to-b from-[#e9edf3] to-[#b9c0cc]",
            "data-[active=true]:from-(--tone) data-[active=true]:to-[#8f96a4]",
          )}
          onPointerDown={(e) => { if (e.button === 0) press(note); }}
          onPointerEnter={(e) => { if (e.buttons === 1) press(note); }}
        />
      ))}

      {blackNotes.map(({ note, index }) => (
        <div
          key={note}
          ref={register(note)}
          data-testid="key-black"
          data-note={note}
          data-active="false"
          className={cn(
            "absolute top-0 z-10 h-[62%] rounded-b-[3px] shadow-cap",
            "bg-linear-to-b from-[#2a2f3c] to-[#0d0f15]",
            "data-[active=true]:from-(--tone) data-[active=true]:to-[#1b1e27]",
          )}
          style={{ left: `${index * unit - unit * 0.3}%`, width: `${unit * 0.6}%` }}
          onPointerDown={(e) => { if (e.button === 0) press(note); }}
          onPointerEnter={(e) => { if (e.buttons === 1) press(note); }}
        />
      ))}
    </div>
  );
}
```

Il `setPointerCapture` sta sul **contenitore**, non sul singolo tasto: e' cio' che tiene il trascinamento fra tasti dentro lo stesso gestore. Se il bug di pointer capture dentro la web view di JUCE si manifesta, e' qui che va aggirato.

- [ ] **Step 4: Verifica**

```bash
cd WebUI && pnpm ui:test
```

Atteso: PASS.

- [ ] **Step 5: Esporta e scrivi le storie**

In `WebUI/packages/ui/src/index.ts`:

```ts
export { Keybed, type KeybedProps, type KeybedNoteMask } from "@/components/Keybed/Keybed";
```

Crea `Keybed.stories.tsx` con una storia `Default` (4 ottave da C2) e una `Playing` che accende qualche nota con un `subscribeNotes` finto guidato da un `setInterval`.

- [ ] **Step 6: Verifica e commit**

```bash
cd WebUI && pnpm ui:test && pnpm ui:build && pnpm typecheck && cd ..
git add WebUI/packages/ui/src/components/Keybed WebUI/packages/ui/src/index.ts
git commit -m "Add the Keybed primitive"
```

---

## FASE 4 — La striscia

### Task 10: `PerformanceBar` e la fine del `Footer`

**Files:**
- Create: `WebUI/src/synth/ui/PerformanceBar.tsx`, `PerformanceBar.test.tsx`
- Delete: `WebUI/src/synth/ui/Footer.tsx`

**Interfaces:**
- Consumes: `useMeterFrame()` da `./MetersContext`, `isNoteActive`/`noteMaskOf` da `../../juce/backend`.
- Produces:

```ts
export type PerformanceBarProps = {
  /** Nota MIDI del primo tasto visibile. */
  firstNote: number;
  onOctaveDown: () => void;
  onOctaveUp: () => void;
  /** Velocity 1..127 mostrata e regolata dalla barra. */
  velocity: number;
  onVelocityChange: (v: number) => void;
};
```

- [ ] **Step 1: Scrivi i test che falliscono**

Crea `WebUI/src/synth/ui/PerformanceBar.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { MetersProvider } from "./MetersContext";
import { PerformanceBar } from "./PerformanceBar";

function mount(props: Partial<React.ComponentProps<typeof PerformanceBar>> = {}) {
  const all = {
    firstNote: 36,
    onOctaveDown: () => {},
    onOctaveUp: () => {},
    velocity: 80,
    onVelocityChange: () => {},
    ...props,
  };
  render(
    <BridgeProvider backend={new FakeBackend()}>
      <MetersProvider>
        <PerformanceBar {...all} />
      </MetersProvider>
    </BridgeProvider>,
  );
}

describe("PerformanceBar", () => {
  it("mostra la nota piu' bassa visibile", () => {
    mount({ firstNote: 36 });
    expect(screen.getByTestId("octave-readout")).toHaveTextContent("C2");
  });

  it("nomina bene anche le ottave negative", () => {
    mount({ firstNote: 0 });
    expect(screen.getByTestId("octave-readout")).toHaveTextContent("C-1");
  });

  it("i due bottoni d'ottava chiamano i loro callback", () => {
    const onOctaveDown = vi.fn();
    const onOctaveUp = vi.fn();
    mount({ onOctaveDown, onOctaveUp });
    fireEvent.click(screen.getByRole("button", { name: "Octave down" }));
    fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
    expect(onOctaveDown).toHaveBeenCalledTimes(1);
    expect(onOctaveUp).toHaveBeenCalledTimes(1);
  });

  it("la velocity si legge e si cambia", () => {
    const onVelocityChange = vi.fn();
    mount({ velocity: 80, onVelocityChange });
    const spin = screen.getByRole("spinbutton", { name: "Velocity" });
    expect(spin).toHaveAttribute("aria-valuenow", "80");
    fireEvent.keyDown(spin, { key: "ArrowUp" });
    expect(onVelocityChange).toHaveBeenCalledWith(81);
  });

  it("mostra la sorgente MIDI e una spia spenta senza note", () => {
    mount();
    expect(screen.getByTestId("midi-source")).toHaveTextContent("HOST");
    expect(screen.getByTestId("midi-activity")).toHaveAttribute("data-on", "false");
  });

  it("tiene i due meter ereditati dal footer", () => {
    mount();
    expect(screen.getByRole("meter", { name: "Input" })).toBeInTheDocument();
    expect(screen.getByRole("meter", { name: "Output" })).toBeInTheDocument();
  });

  it("non mostra piu' VOICES ne' CPU", () => {
    // Erano numeri finti — lo diceva il commento del vecchio Footer — e non tornano finche'
    // l'host non li espone davvero.
    mount();
    expect(screen.queryByText(/VOICES/)).toBeNull();
    expect(screen.queryByText(/CPU/)).toBeNull();
  });
});
```

- [ ] **Step 2: Fallo fallire**

```bash
cd WebUI && pnpm test
```

Atteso: FAIL, modulo `./PerformanceBar` non trovato.

- [ ] **Step 3: Scrivi il componente**

Crea `WebUI/src/synth/ui/PerformanceBar.tsx`:

```tsx
import { Button, Meter, Stepper } from "@xerum/ui";
import { useMeterFrame } from "./MetersContext";

export type PerformanceBarProps = {
  /** Nota MIDI del primo tasto visibile. */
  firstNote: number;
  onOctaveDown: () => void;
  onOctaveUp: () => void;
  /** Velocity 1..127 mostrata e regolata dalla barra. */
  velocity: number;
  onVelocityChange: (v: number) => void;
};

const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

/** Nome di una nota MIDI con la convenzione in cui il DO centrale (60) e' C4. */
const noteName = (n: number) => `${NAMES[n % 12]}${Math.floor(n / 12) - 1}`;

/**
 * Barra di esecuzione sopra i tasti: ottava, velocity, stato MIDI e i due meter.
 *
 * Ha preso il posto del Footer, che portava anche VOICES e CPU: erano numeri inventati — lo
 * diceva il commento del file stesso — e sono spariti invece di essere trasportati.
 *
 * Lo spazio della sorgente MIDI mostra oggi solo "HOST" e una spia di attivita' presa dal mask
 * delle note. E' il punto in cui atterrera' la scelta del device con il sottoprogetto B.
 */
export function PerformanceBar({
  firstNote,
  onOctaveDown,
  onOctaveUp,
  velocity,
  onVelocityChange,
}: PerformanceBarProps) {
  const m = useMeterFrame();
  const anyNote = (m.n0 | m.n1 | m.n2 | m.n3) !== 0;

  return (
    <div className="flex h-7 shrink-0 items-center gap-3 px-1.5 font-mono text-2xs tracking-wider text-text-dim">
      <span>OCT</span>
      <Button size="sm" aria-label="Octave down" onClick={onOctaveDown}>−</Button>
      <span data-testid="octave-readout" className="w-8 text-center font-medium text-foreground">
        {noteName(firstNote)}
      </span>
      <Button size="sm" aria-label="Octave up" onClick={onOctaveUp}>+</Button>

      <Stepper value={velocity} onChange={onVelocityChange} min={1} max={127} label="Velocity" unit="VEL" />

      <span className="flex-1" />

      <span data-testid="midi-source">MIDI HOST</span>
      <span
        data-testid="midi-activity"
        data-on={anyNote}
        aria-hidden
        className="size-1.5 rounded-full bg-tick data-[on=true]:bg-(--tone)"
      />

      <span className="flex-1" />

      <span>IN</span>
      <Meter level={m.in} label="Input" tone="osc" />
      <Meter level={m.out} label="Output" tone="master" />
      <span>OUT</span>
    </div>
  );
}
```

Se `Button` non accetta `size="sm"` o `Stepper` non espone il `label` come nome accessibile dello spinbutton, adegua le prop a quelle vere dei due componenti in `WebUI/packages/ui/src/components/` — il contratto che i test fissano è il nome accessibile, non il nome della prop.

- [ ] **Step 4: Cancella il Footer**

```bash
git rm WebUI/src/synth/ui/Footer.tsx
```

`MetersContext.tsx` non si tocca: resta e ora lo consuma `PerformanceBar`. Se `SynthWindow.tsx` non compila perché importa ancora `Footer`, lascialo rotto fino alla Task 11 — oppure esegui le Task 10 e 11 di seguito prima di lanciare la suite.

- [ ] **Step 5: Verifica**

```bash
cd WebUI && pnpm test -- PerformanceBar && pnpm typecheck
```

- [ ] **Step 6: Commit**

```bash
git add WebUI/src/synth/ui/PerformanceBar.tsx WebUI/src/synth/ui/PerformanceBar.test.tsx
git commit -m "Replace the footer with a performance bar"
```

---

### Task 11: `BottomStrip` e lo chassis a 708

**Files:**
- Create: `WebUI/src/synth/ui/BottomStrip.tsx`, `BottomStrip.test.tsx`
- Modify: `WebUI/src/synth/ui/SynthWindow.tsx`, `WebUI/src/synth/ui/SynthWindow.test.tsx`
- Modify: `WebUI/src/synth/ui/synth.css`

**Interfaces:**
- Consumes: `Keybed`, `Wheel` (Task 8-9), `PerformanceBar` (Task 10), `useBackend()` da `../../juce/provider`.

- [ ] **Step 1: Scrivi i test che falliscono**

Crea `WebUI/src/synth/ui/BottomStrip.test.tsx`:

```tsx
import { describe, expect, it } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { MetersProvider } from "./MetersContext";
import { BottomStrip } from "./BottomStrip";

function mount() {
  const backend = new FakeBackend();
  render(
    <BridgeProvider backend={backend}>
      <MetersProvider>
        <BottomStrip />
      </MetersProvider>
    </BridgeProvider>,
  );
  return backend;
}

const firstKey = () => screen.getAllByTestId("key-white")[0]!;

describe("BottomStrip", () => {
  it("parte da C2 e mostra quattro ottave", () => {
    mount();
    expect(firstKey()).toHaveAttribute("data-note", "36");
    expect(screen.getAllByTestId("key-white")).toHaveLength(28);
  });

  it("suonare un tasto arriva al backend", async () => {
    const backend = mount();
    fireEvent.pointerDown(firstKey(), { button: 0, pointerId: 1 });
    await Promise.resolve();
    expect([...backend.playing]).toEqual([36]);

    fireEvent.pointerUp(firstKey(), { pointerId: 1 });
    await Promise.resolve();
    expect(backend.playing.size).toBe(0);
  });

  it("i bottoni d'ottava spostano i tasti", () => {
    mount();
    fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
    expect(firstKey()).toHaveAttribute("data-note", "48");
    fireEvent.click(screen.getByRole("button", { name: "Octave down" }));
    expect(firstKey()).toHaveAttribute("data-note", "36");
  });

  it("l'ottava non esce dal range MIDI", () => {
    mount();
    for (let i = 0; i < 8; i++) fireEvent.click(screen.getByRole("button", { name: "Octave down" }));
    expect(Number(firstKey().getAttribute("data-note"))).toBeGreaterThanOrEqual(0);

    for (let i = 0; i < 16; i++) fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
    const highest = Math.max(...screen.getAllByTestId("key-white").map((k) => Number(k.getAttribute("data-note"))));
    expect(highest).toBeLessThanOrEqual(127);
  });

  it("muovere la mod wheel chiama setWheel", async () => {
    const backend = mount();
    const wheel = screen.getByRole("slider", { name: "MW" });
    fireEvent.pointerDown(wheel, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerMove(wheel, { clientX: 0, clientY: 60, pointerId: 1 });
    await Promise.resolve();
    expect(backend.wheels.mod).toBeCloseTo(0.2, 5);
  });

  it("la pitch wheel torna al centro al rilascio", async () => {
    const backend = mount();
    const wheel = screen.getByRole("slider", { name: "PB" });
    fireEvent.pointerDown(wheel, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerMove(wheel, { clientX: 0, clientY: 60, pointerId: 1 });
    await Promise.resolve();
    expect(backend.wheels.pitch).toBeGreaterThan(0.5);

    fireEvent.pointerUp(wheel, { clientX: 0, clientY: 60, pointerId: 1 });
    await Promise.resolve();
    expect(backend.wheels.pitch).toBe(0.5);
  });
});
```

- [ ] **Step 2: Fallo fallire**

```bash
cd WebUI && pnpm test
```

- [ ] **Step 3: Scrivi il componente**

Crea `WebUI/src/synth/ui/BottomStrip.tsx`:

```tsx
import { useCallback, useState } from "react";
import { Keybed, Wheel, type KeybedNoteMask } from "@xerum/ui";
import { noteMaskOf } from "../../juce/backend";
import { useBackend } from "../../juce/provider";
import { PerformanceBar } from "./PerformanceBar";

const OCTAVES = 4;
const LOWEST = 0;
const HIGHEST = 127 - 12 * OCTAVES;

/**
 * La striscia bassa: rotelle a sinistra, barra di esecuzione e tasti a destra.
 *
 * Le rotelle stanno in una colonna a tutta altezza e non dentro la barra perche' hanno bisogno di
 * corsa verticale: in 28 px non si suona niente.
 */
export function BottomStrip() {
  const backend = useBackend();
  const [firstNote, setFirstNote] = useState(36); // C2, la stessa nota da cui partiva la striscia nativa
  const [velocity, setVelocity] = useState(80);
  const [pitch, setPitch] = useState(0.5);
  const [mod, setMod] = useState(0);

  const shift = (by: number) =>
    setFirstNote((n) => Math.min(HIGHEST, Math.max(LOWEST, n + by)));

  const onPitch = useCallback((v: number) => { setPitch(v); void backend.setWheel("pitch", v); }, [backend]);
  const onMod = useCallback((v: number) => { setMod(v); void backend.setWheel("mod", v); }, [backend]);

  const onNoteOn = useCallback((note: number, vel: number) => { void backend.noteOn(note, vel); }, [backend]);
  const onNoteOff = useCallback((note: number) => { void backend.noteOff(note); }, [backend]);
  const onAllNotesOff = useCallback(() => { void backend.allNotesOff(); }, [backend]);

  // Non passa da useMeterFrame: leggere quel contesto qui rirenderizzerebbe la striscia trenta
  // volte al secondo, che e' esattamente cio' che Keybed evita mutando data-active via ref.
  const subscribeNotes = useCallback(
    (cb: (mask: KeybedNoteMask) => void) => backend.onMeters((m) => cb(noteMaskOf(m))),
    [backend],
  );

  return (
    <div className="flex h-[108px] shrink-0 gap-2">
      <div className="flex w-16 shrink-0 gap-2 py-1">
        <Wheel value={pitch} onChange={onPitch} label="PB" defaultValue={0.5} bipolar springBack tone="osc" />
        <Wheel value={mod} onChange={onMod} label="MW" tone="lfo" />
      </div>

      <div className="flex min-w-0 flex-1 flex-col">
        <PerformanceBar
          firstNote={firstNote}
          onOctaveDown={() => shift(-12)}
          onOctaveUp={() => shift(12)}
          velocity={velocity}
          onVelocityChange={setVelocity}
        />
        <Keybed
          firstNote={firstNote}
          octaves={OCTAVES}
          velocity={velocity / 127}
          onNoteOn={onNoteOn}
          onNoteOff={onNoteOff}
          onAllNotesOff={onAllNotesOff}
          subscribeNotes={subscribeNotes}
          className="flex-1"
        />
      </div>
    </div>
  );
}
```

- [ ] **Step 4: Monta la striscia nella finestra**

In `WebUI/src/synth/ui/SynthWindow.tsx`:

- `const H = 708;` al posto di `600`;
- sostituisci `<Footer />` con `<BottomStrip />` e togli l'import di `Footer`.

In `WebUI/src/synth/ui/synth.css`, togli la regola che squadra gli angoli bassi dello chassis quando `data-attached` è presente: la striscia adesso è dentro lo chassis e il raggio torna quello del pannello. `data-attached` resta per il margine.

- [ ] **Step 5: Riscrivi il test dello chassis**

`data-attached` **resta**: continua a significare "niente margine, lo chassis riempie la WebView", e su quello il test passa già. Cambia il suo significato dichiarato, perché non c'è più nessuna tastiera nativa da agganciare. In `WebUI/src/synth/ui/SynthWindow.test.tsx:24` sostituisci il test:

```tsx
  it("gutter=0 lets the chassis fill the whole WebView", async () => {
    // Il nome di prima diceva "so the native keyboard can attach to it": la striscia era una
    // MidiKeyboardComponent montata sotto la WebView, e lo chassis doveva squadrare il proprio
    // fondo perché i due leggessero come un corpo solo. Adesso i tasti sono dentro lo chassis,
    // l'angolo arrotondato è tornato (vedi synth.css) e data-attached serve solo a togliere il
    // margine.
    mount(undefined, { gutter: 0 });
    expect(screen.getByTestId("chassis")).toHaveAttribute("data-attached");
  });
```

Aggiungi accanto il test che fissa la nuova altezza:

```tsx
  it("il chassis è alto 708: 600 di pannello più 108 di striscia", async () => {
    mount();
    expect(screen.getByTestId("chassis")).toHaveStyle({ height: "708px" });
  });
```

Se l'altezza dello chassis è fissata in `synth.css` e non inline, asserisci invece sulla regola CSS o sul `getBoundingClientRect` in jsdom — l'importante è che il 708 sia coperto da un test, perché è il numero da cui dipende tutta la geometria dell'editor.

Infine, verifica che la striscia sia montata:

```tsx
  it("monta la striscia bassa al posto del footer", async () => {
    mount();
    expect(screen.getAllByTestId("key-white").length).toBeGreaterThan(0);
    expect(screen.getByRole("slider", { name: "PB" })).toBeInTheDocument();
  });
```

- [ ] **Step 6: Verifica**

```bash
cd WebUI && pnpm test && pnpm typecheck && pnpm build
```

Atteso: PASS, e `pnpm build` (che include `tsc --noEmit`) pulito.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/ui/BottomStrip.tsx WebUI/src/synth/ui/BottomStrip.test.tsx \
        WebUI/src/synth/ui/SynthWindow.tsx WebUI/src/synth/ui/SynthWindow.test.tsx WebUI/src/synth/ui/synth.css
git commit -m "Move the keybed inside the chassis"
```

---

## FASE 5 — La rimozione

### Task 12: Via la tastiera nativa

Il punto di non ritorno: da qui in poi la striscia esiste solo nella WebView. Ultima task, da sola.

**Files:**
- Delete: `Source/ui/XerumKeyboard.h`, `Source/ui/XerumKeyboard.cpp`
- Modify: `Source/plugin/PluginEditor.h`, `Source/plugin/PluginEditor.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/architecture.md`, `docs/build.md`

- [ ] **Step 1: Cancella il componente**

```bash
git rm Source/ui/XerumKeyboard.h Source/ui/XerumKeyboard.cpp
```

In `CMakeLists.txt`, togli `Source/ui/XerumKeyboard.cpp` da `target_sources(SerumStyleSynth ...)`.

- [ ] **Step 2: Snellisci l'header dell'editor**

In `Source/plugin/PluginEditor.h`: togli `#include "ui/XerumKeyboard.h"`, il membro `keyboard_`, la dichiarazione di `configureKeyboard()` e le tre costanti `kLowestNote`, `kHighestNote`, `kWhiteKeysVisible`.

- [ ] **Step 3: Snellisci l'implementazione**

In `Source/plugin/PluginEditor.cpp`:

- togli `kKeyboardHeight`, `kChassisCorner` e la funzione `webHeightForWidth`;
- `kChassisHeight` passa da `600` a `708`;
- `heightForWidth` diventa:

```cpp
int heightForWidth (int width) noexcept
{
    // La WebView e' l'intero editor: l'altezza e' quella dello chassis scalato, punto. Prima
    // qui si sommava la striscia nativa, che adesso vive dentro lo chassis.
    return static_cast<int> (std::ceil (kChassisHeight * scaleForWidth (width)));
}
```

- togli `addAndMakeVisible (keyboard_)`, la chiamata a `configureKeyboard()` e il corpo della funzione;
- `resized()` diventa:

```cpp
void SerumStyleSynthAudioProcessorEditor::resized()
{
    webView_.setBounds (getLocalBounds());
}
```

- nel blocco `JUCE_DEBUG` dello snapshot, il commento dice che la WebView renderizza vuota e che lo snapshot mostra la striscia nativa. Adesso lo snapshot è vuoto per intero: aggiorna il commento dicendolo, oppure togli il blocco se non serve più.

- [ ] **Step 4: Verifica la build**

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```

Atteso: build pulita di tutti i target, `ALL TESTS PASSED`.

- [ ] **Step 5: Prova a mano — lista completa**

Con `scripts/dev.sh` e lo Standalone aperto:

1. la finestra si apre senza banda vuota fra pannello e tasti, e ridimensionandola i due restano solidali;
2. cliccando un tasto si sente la nota, rilasciando si spegne;
3. trascinando da un tasto all'altro col pulsante premuto la nota cambia e non ne resta nessuna appesa;
4. `OCT −/+` spostano la tastiera e il readout segue;
5. cambiando `VEL` il click suona più piano o più forte;
6. la mod wheel muove una route `mw → cutoff` nella mod matrix;
7. la pitch wheel piega la nota di `pbRange` semitoni e torna al centro al rilascio;
8. suonando da un controller MIDI esterno i tasti sulla striscia si accendono;
9. con l'arp acceso i tasti seguono il pattern;
10. cambiando finestra a metà click la nota non resta appesa.

Se il punto 3 o il 10 falliscono, è il bug di pointer capture dentro la web view: è il rischio accettato nella spec e va risolto in `Keybed` prima di chiudere.

- [ ] **Step 6: Aggiorna la documentazione**

In `docs/architecture.md`, la sezione che descrive la tastiera nativa e la geometria a due pezzi va riscritta: WebView unica, striscia dentro lo chassis, canale MIDI del bridge, mask delle note nel frame `meters`, pitch bend. In `docs/build.md`, la riga della smoke checklist che parla della tastiera nativa va allineata.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "Remove the native keyboard strip"
```

---

## Self-review

**Copertura della spec**

| Sezione della spec | Task |
|---|---|
| Geometria, chassis 708 | 11, 12 |
| `Wheel`, `Keybed` in `@xerum/ui` | 8, 9 |
| `BottomStrip`, `PerformanceBar` | 10, 11 |
| `Footer` eliminato, VOICES/CPU via | 10 |
| Editor snellito, `XerumKeyboard` cancellato | 12 |
| `MidiChannel`, quattro native function | 6 |
| Wheel come messaggi MIDI veri | 6 |
| Mask delle note in `MeterFrame`, `n0..n3` | 4, 5 |
| Pitch bend, `pbRange`, propagazione, arp | 1, 3 |
| Test C++ (pitch bend, seam, mask) | 1, 3, 4 |
| Test web (Keybed, Wheel, chassis) | 8, 9, 11 |
| Ordine in cinque fasi | Fasi 1-5 |

La Task 2 (estrazione degli helper di test) non è nella spec: è un prerequisito emerso scrivendo il piano, perché gli helper del motore sono chiusi nel namespace anonimo del file di test più grande.

**Nomi verificati end-to-end**

`pbRange` (json) → `ParamSlot::pbRange` (generato) → `EngineParams::pitchBendRangeSemitones`; `EngineParams::pitchBend` ← `SynthEngine::pitchBend_`; `SynthEngine::getActiveNotesLo/Hi` → `MeterFrame::notesLo/notesHi` → `n0..n3` sul filo → `MeterFrame.n0..n3` in TypeScript → `noteMaskOf` → `isNoteActive`; `Backend.noteOn/noteOff/allNotesOff/setWheel` ↔ native function omonime.
