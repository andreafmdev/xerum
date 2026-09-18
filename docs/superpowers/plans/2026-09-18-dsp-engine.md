# DSP Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the plugin audible — a band-limited wavetable oscillator, a real ADSR, a state variable filter, 22 parameters actually connected, and factory presets.

**Architecture:** Single-cycle waveforms from AKWF (CC0) are downloaded and resampled offline by a Node script into `.xwt` blobs embedded in the binary. At runtime `dsp::WavetableStore` parses a blob and builds a band-limited mipmap (level *k* = `2048 >> k` samples) on the message thread, publishing it to the audio thread through an atomic pointer; built tables are never freed, which is what makes that pointer safe. Each voice is oscillator → drive → SVF → ADSR → pan; parameters are denormalised once per block into an `EngineParams` struct.

**Tech Stack:** C++17, JUCE 8.0.6 (`juce_dsp` for FFT, `juce::UnitTest` for tests), CMake presets, Node ≥ 18 (`node:test`, `fetch`, no npm dependencies), TypeScript/React + vitest for the web side.

**Spec:** `docs/superpowers/specs/2026-09-18-dsp-engine-design.md`

## Global Constraints

- **No allocation, no locks, no I/O, no logging in `processBlock`** or anything it calls. Allocating work (mipmap build, table parse) happens in `prepareToPlay` or on the message thread.
- **No `libm` per sample.** `std::pow`, `std::exp`, `std::tan`, `std::tanh`, `std::log` are called at note-on or on parameter change only.
- **Parameters are read once per block**, never per sample.
- **`Source/parameters/parameters.json` is the single source of truth** for ranges and mappings. C++ denormalises through `params::denormalise` (`Source/parameters/ParameterMapping.h`); never hardcode a range.
- **Adding a `.cpp` to `Source/` requires a CMake reconfigure**: `cmake --preset macos-debug` before `cmake --build`.
- **Comments and identifiers follow the existing codebase**: comments in Italian where the surrounding file uses Italian (`Source/bridge/`, `Source/parameters/`, `WebUI/src/`), English where the file uses English (`Source/plugin/PluginEditor.cpp` mixes both — match the block you are editing). Commit messages in English.
- **Frame geometry is fixed**: 64 frames per table, 2048 samples per frame, mipmap levels 0…6 (2048 down to 32 samples).
- The user runs `terraform` and `aws` commands themselves — not applicable here, but never invoke them.

---

## File Structure

**New**

| file | responsibility |
|---|---|
| `scripts/wavetable-dsp.mjs` | pure functions: WAV parse, DFT resample, DC/normalise, frame selection, `.xwt` encode. No I/O, no network — this is what the tests exercise. |
| `scripts/wavetable-dsp.test.mjs` | `node --test` suite for the above |
| `scripts/fetch-wavetables.mjs` | the I/O shell: downloads AKWF, calls `wavetable-dsp.mjs`, writes `Resources/wavetables/*.xwt` and `CREDITS.md` |
| `Resources/wavetables/*.xwt` (6) | the committed tables |
| `Resources/wavetables/CREDITS.md` | source, author, licence, date |
| `Source/dsp/WavetableBlob.h/.cpp` | `.xwt` parser: validation only, no ownership |
| `Source/dsp/MipTable.h/.cpp` | the audio-ready table: frames × mipmap levels, contiguous storage |
| `Tests/main.cpp` | `juce::UnitTestRunner` entry point |
| `Tests/WavetableTests.cpp` | blob parser + mipmap + oscillator tests |
| `Tests/EnvelopeFilterTests.cpp` | ADSR + SVF tests |
| `Tests/EngineTests.cpp` | parameter sweep, voice behaviour |
| `Source/parameters/presets.json` | factory preset values (normalised) |
| `Source/parameters/PresetTable.h` | generated from the above |
| `WebUI/src/synth/presets.generated.ts` | generated from the above |

**Modified**

`Source/dsp/WavetableStore.h/.cpp`, `Source/dsp/WavetableOscillator.h/.cpp`, `Source/dsp/ADSREnvelope.h/.cpp`, `Source/dsp/StateVariableFilter.h/.cpp`, `Source/engine/SynthVoice.h/.cpp`, `Source/engine/VoiceManager.h/.cpp`, `Source/engine/SynthEngine.h/.cpp`, `Source/plugin/PluginProcessor.h/.cpp`, `Source/bridge/StateChannel.h/.cpp`, `Source/util/RealtimeHelpers.h`, `scripts/gen-params.mjs`, `WebUI/src/juce/backend.ts`, `WebUI/src/juce/juce-backend.ts`, `WebUI/src/juce/fake-backend.ts`, `WebUI/src/synth/presets.ts`, `WebUI/src/synth/useSynth.ts`, `CMakeLists.txt`, `docs/architecture.md`, `docs/build.md`.

---

### Task 1: Wavetable conversion pipeline (Node)

Produces the `.xwt` files everything else consumes. Pure functions are unit-tested; the network shell is not.

**Files:**
- Create: `scripts/wavetable-dsp.mjs`
- Create: `scripts/wavetable-dsp.test.mjs`
- Create: `scripts/fetch-wavetables.mjs`
- Create: `Resources/wavetables/*.xwt` (generated, committed), `Resources/wavetables/CREDITS.md`

**Interfaces:**
- Consumes: nothing
- Produces: `.xwt` files in the format Task 2 parses — `"XWT1"` magic, `uint32 frames` (64), `uint32 frameSize` (2048), then `frames × frameSize` little-endian `float32`.

- [ ] **Step 1: Write the failing tests**

Create `scripts/wavetable-dsp.test.mjs`:

```js
import test from "node:test";
import assert from "node:assert/strict";
import { encodeXwt, parseWav16Mono, removeDcAndNormalise, resampleCycle, selectFrames } from "./wavetable-dsp.mjs";

/** Un ciclo di seno su n campioni. */
function sineCycle(n, harmonic = 1) {
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = Math.sin((2 * Math.PI * harmonic * i) / n);
  return out;
}

test("resampleCycle porta un seno da 600 a 2048 campioni senza distorsione", () => {
  const out = resampleCycle(sineCycle(600), 2048);
  assert.equal(out.length, 2048);
  let maxErr = 0;
  for (let i = 0; i < 2048; i++) maxErr = Math.max(maxErr, Math.abs(out[i] - Math.sin((2 * Math.PI * i) / 2048)));
  assert.ok(maxErr < 1e-4, `errore massimo ${maxErr}`);
});

test("resampleCycle conserva l'armonica alta senza aliasing", () => {
  const out = resampleCycle(sineCycle(600, 37), 2048);
  let maxErr = 0;
  for (let i = 0; i < 2048; i++) maxErr = Math.max(maxErr, Math.abs(out[i] - Math.sin((2 * Math.PI * 37 * i) / 2048)));
  assert.ok(maxErr < 1e-3, `errore massimo ${maxErr}`);
});

test("removeDcAndNormalise toglie la continua e porta il picco a 1", () => {
  const f = Float32Array.from([0.5, 0.7, 0.5, 0.3]);
  removeDcAndNormalise(f);
  const mean = f.reduce((a, b) => a + b, 0) / f.length;
  const peak = Math.max(...Array.from(f, Math.abs));
  assert.ok(Math.abs(mean) < 1e-6, `media ${mean}`);
  assert.ok(Math.abs(peak - 1) < 1e-6, `picco ${peak}`);
});

test("selectFrames interpola fra le ancore quando le onde sono meno dei frame", () => {
  const a = Float32Array.from([0, 0]);
  const b = Float32Array.from([1, 1]);
  const frames = selectFrames([a, b], 5);
  assert.equal(frames.length, 5);
  assert.ok(Math.abs(frames[0][0] - 0) < 1e-6);
  assert.ok(Math.abs(frames[2][0] - 0.5) < 1e-6);
  assert.ok(Math.abs(frames[4][0] - 1) < 1e-6);
});

test("selectFrames copre tutta la famiglia quando le onde sono più dei frame", () => {
  const waves = Array.from({ length: 100 }, (_, i) => Float32Array.from([i / 99]));
  const frames = selectFrames(waves, 64);
  assert.equal(frames.length, 64);
  assert.ok(Math.abs(frames[0][0] - 0) < 1e-6);
  assert.ok(Math.abs(frames[63][0] - 1) < 1e-6);
});

test("encodeXwt scrive header e campioni nell'ordine atteso", () => {
  const buf = encodeXwt([Float32Array.from([0.25, -0.5])], 2);
  assert.equal(buf.length, 12 + 2 * 4);
  assert.equal(buf.toString("ascii", 0, 4), "XWT1");
  assert.equal(buf.readUInt32LE(4), 1);
  assert.equal(buf.readUInt32LE(8), 2);
  assert.ok(Math.abs(buf.readFloatLE(12) - 0.25) < 1e-7);
  assert.ok(Math.abs(buf.readFloatLE(16) + 0.5) < 1e-7);
});

test("parseWav16Mono legge un wav PCM 16 bit mono", () => {
  // Header canonico da 44 byte + due campioni: -32768 e 32767.
  const data = Buffer.alloc(48);
  data.write("RIFF", 0, "ascii"); data.writeUInt32LE(40, 4); data.write("WAVE", 8, "ascii");
  data.write("fmt ", 12, "ascii"); data.writeUInt32LE(16, 16); data.writeUInt16LE(1, 20);
  data.writeUInt16LE(1, 22); data.writeUInt32LE(44100, 24); data.writeUInt32LE(88200, 28);
  data.writeUInt16LE(2, 32); data.writeUInt16LE(16, 34);
  data.write("data", 36, "ascii"); data.writeUInt32LE(4, 40);
  data.writeInt16LE(-32768, 44); data.writeInt16LE(32767, 46);
  const out = parseWav16Mono(data);
  assert.equal(out.length, 2);
  assert.ok(Math.abs(out[0] + 1) < 1e-4);
  assert.ok(Math.abs(out[1] - 1) < 1e-4);
});
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `node --test scripts/wavetable-dsp.test.mjs`
Expected: FAIL — `Cannot find module '.../scripts/wavetable-dsp.mjs'`

- [ ] **Step 3: Write the implementation**

Create `scripts/wavetable-dsp.mjs`:

```js
// Funzioni pure per la conversione delle onde AKWF in tavole .xwt.
// Nessuna rete, nessun filesystem: quelli stanno in fetch-wavetables.mjs.

/** Legge un WAV PCM 16 bit mono e restituisce i campioni in -1..1. */
export function parseWav16Mono(buffer) {
  if (buffer.length < 12 || buffer.toString("ascii", 0, 4) !== "RIFF" || buffer.toString("ascii", 8, 12) !== "WAVE")
    throw new Error("non è un file RIFF/WAVE");

  let offset = 12;
  let bitsPerSample = 0;
  let channels = 0;
  let data = null;

  // I chunk non sono in ordine garantito: si cammina finché non si trovano fmt e data.
  while (offset + 8 <= buffer.length) {
    const id = buffer.toString("ascii", offset, offset + 4);
    const size = buffer.readUInt32LE(offset + 4);
    const body = offset + 8;
    if (id === "fmt ") {
      channels = buffer.readUInt16LE(body + 2);
      bitsPerSample = buffer.readUInt16LE(body + 14);
    } else if (id === "data") {
      data = buffer.subarray(body, Math.min(body + size, buffer.length));
    }
    offset = body + size + (size % 2); // i chunk sono allineati a 2 byte
  }

  if (data === null) throw new Error("chunk data assente");
  if (bitsPerSample !== 16) throw new Error(`attesi 16 bit, trovati ${bitsPerSample}`);
  if (channels !== 1) throw new Error(`attesa una traccia mono, trovate ${channels}`);

  const count = Math.floor(data.length / 2);
  const out = new Float32Array(count);
  for (let i = 0; i < count; i++) out[i] = data.readInt16LE(i * 2) / 32768;
  return out;
}

/**
 * Ricampiona un singolo ciclo da input.length a outLength campioni passando
 * per la serie di Fourier. Per un segnale periodico è l'interpolazione esatta:
 * si calcolano i coefficienti sul ciclo di partenza e si risintetizza sulla
 * nuova lunghezza. L'interpolazione lineare, al confronto, aggiunge armoniche
 * che non ci sono.
 */
export function resampleCycle(input, outLength) {
  const n = input.length;
  const half = Math.floor(n / 2);
  const out = new Float32Array(outLength);

  for (let k = 0; k <= half; k++) {
    let re = 0;
    let im = 0;
    for (let i = 0; i < n; i++) {
      const a = (-2 * Math.PI * k * i) / n;
      re += input[i] * Math.cos(a);
      im += input[i] * Math.sin(a);
    }
    re /= n;
    im /= n;

    // Il bin 0 (continua) e, per n pari, il bin di Nyquist non hanno gemello negativo.
    const amp = k === 0 || (n % 2 === 0 && k === half) ? 1 : 2;

    for (let j = 0; j < outLength; j++) {
      const b = (2 * Math.PI * k * j) / outLength;
      out[j] += amp * (re * Math.cos(b) - im * Math.sin(b));
    }
  }

  return out;
}

/** Toglie la continua e porta il picco a 1. In place. */
export function removeDcAndNormalise(frame) {
  let sum = 0;
  for (let i = 0; i < frame.length; i++) sum += frame[i];
  const mean = sum / frame.length;

  let peak = 0;
  for (let i = 0; i < frame.length; i++) {
    frame[i] -= mean;
    peak = Math.max(peak, Math.abs(frame[i]));
  }

  if (peak > 0) for (let i = 0; i < frame.length; i++) frame[i] /= peak;
  return frame;
}

/**
 * Ricava `count` frame da una lista di onde, interpolando fra onde adiacenti.
 * Serve in due casi: famiglia più corta di count (le onde sono ancore di un
 * morph) e famiglia più lunga (si scorre tutta a passo costante). Un'unica
 * strada, perché anche nel secondo caso il blend fra vicine rende il morph
 * più liscio che saltare da un'onda all'altra.
 */
export function selectFrames(waves, count) {
  if (waves.length === 0) throw new Error("nessuna onda da cui ricavare i frame");

  const out = [];
  for (let i = 0; i < count; i++) {
    const pos = count === 1 ? 0 : ((waves.length - 1) * i) / (count - 1);
    const lo = Math.floor(pos);
    const hi = Math.min(lo + 1, waves.length - 1);
    const t = pos - lo;
    const a = waves[lo];
    const b = waves[hi];
    const frame = new Float32Array(a.length);
    for (let s = 0; s < a.length; s++) frame[s] = a[s] * (1 - t) + b[s] * t;
    out.push(frame);
  }
  return out;
}

/** Serializza i frame nel formato .xwt (little-endian, nessun padding). */
export function encodeXwt(frames, frameSize) {
  const buf = Buffer.alloc(12 + frames.length * frameSize * 4);
  buf.write("XWT1", 0, "ascii");
  buf.writeUInt32LE(frames.length, 4);
  buf.writeUInt32LE(frameSize, 8);

  let offset = 12;
  for (const frame of frames) {
    if (frame.length !== frameSize) throw new Error(`frame da ${frame.length}, attesi ${frameSize}`);
    for (let i = 0; i < frameSize; i++) {
      buf.writeFloatLE(frame[i], offset);
      offset += 4;
    }
  }
  return buf;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `node --test scripts/wavetable-dsp.test.mjs`
Expected: PASS, 7 tests.

- [ ] **Step 5: Write the download shell**

Create `scripts/fetch-wavetables.mjs`:

```js
#!/usr/bin/env node
// Scarica le onde AKWF (CC0) e le converte nelle tavole .xwt del plugin.
// Uso: node scripts/fetch-wavetables.mjs
// Gira raramente: i .xwt sono committati, questo script serve solo a rigenerarli.
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { encodeXwt, parseWav16Mono, removeDcAndNormalise, resampleCycle, selectFrames } from "./wavetable-dsp.mjs";

const REPO = "KristofferKarlAxelEkstrand/AKWF-FREE";
const BRANCH = "main";
const FRAMES = 64;
const FRAME_SIZE = 2048;

// L'ordine è quello delle opzioni di `wtIndex` in Source/parameters/parameters.json.
const TABLES = [
  { id: "basic", label: "Basic Shapes", family: "AKWF_bw_perfectwaves" },
  { id: "saws", label: "Analog Saws", family: "AKWF_bw_saw" },
  { id: "grit", label: "Digital Grit", family: "AKWF_bitreduced" },
  { id: "vocal", label: "Vocal Formant", family: "AKWF_hvoice" },
  { id: "bells", label: "Glass Bells", family: "AKWF_fmsynth" },
  { id: "pwm", label: "PWM Sweep", family: "AKWF_bw_squ" },
];

async function listFamily(family) {
  const url = `https://api.github.com/repos/${REPO}/contents/AKWF/${family}`;
  const res = await fetch(url, { headers: { accept: "application/vnd.github+json" } });
  if (!res.ok) throw new Error(`lista di ${family}: HTTP ${res.status}`);
  const entries = await res.json();
  return entries
    .filter((e) => e.type === "file" && e.name.toLowerCase().endsWith(".wav"))
    .sort((a, b) => a.name.localeCompare(b.name))
    .map((e) => e.download_url);
}

async function downloadWave(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return parseWav16Mono(Buffer.from(await res.arrayBuffer()));
}

async function buildTable(table, outDir) {
  const urls = await listFamily(table.family);
  // Scaricare tutta la famiglia quando ne servono 64 è spreco: si prendono
  // prima gli indici utili, poi si interpola fra quelli.
  const wanted = urls.length <= FRAMES ? urls : selectIndices(urls, FRAMES);
  const waves = [];
  for (const url of wanted) waves.push(await downloadWave(url));

  const frames = selectFrames(waves, FRAMES).map((frame) => removeDcAndNormalise(resampleCycle(frame, FRAME_SIZE)));
  writeFileSync(resolve(outDir, `${table.id}.xwt`), encodeXwt(frames, FRAME_SIZE));
  console.log(`${table.id}: ${waves.length} onde da ${table.family} → ${FRAMES} frame`);
}

function selectIndices(list, count) {
  return Array.from({ length: count }, (_, i) => list[Math.round(((list.length - 1) * i) / (count - 1))]);
}

async function main() {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
  const outDir = resolve(root, "Resources/wavetables");
  mkdirSync(outDir, { recursive: true });

  for (const table of TABLES) await buildTable(table, outDir);

  writeFileSync(
    resolve(outDir, "CREDITS.md"),
    [
      "# Wavetables",
      "",
      `Generate da \`scripts/fetch-wavetables.mjs\` il ${new Date().toISOString().slice(0, 10)}.`,
      "",
      "Fonte: **Adventure Kid Waveforms (AKWF)** — Kristoffer Ekstrand",
      `<https://github.com/${REPO}>`,
      "",
      "Licenza: **CC0-1.0** (pubblico dominio). L'attribuzione non è dovuta: è qui per tracciare la provenienza.",
      "",
      "| tavola | famiglia AKWF |",
      "|---|---|",
      ...TABLES.map((t) => `| ${t.label} (\`${t.id}\`) | \`${t.family}\` |`),
      "",
    ].join("\n"),
  );
}

await main();
```

- [ ] **Step 6: Generate the tables**

Run: `node scripts/fetch-wavetables.mjs`
Expected: six lines of output, then `ls -la Resources/wavetables/` shows six `.xwt` files of **524 300 bytes each** (12 + 64 × 2048 × 4) plus `CREDITS.md`.

Sanity check the output with:

```bash
for f in Resources/wavetables/*.xwt; do
  printf '%s ' "$f"; head -c 4 "$f"; printf ' %s bytes\n' "$(wc -c < "$f")"
done
```

Expected: each line reads `XWT1` and `524300 bytes`.

- [ ] **Step 7: Commit**

```bash
git add scripts/wavetable-dsp.mjs scripts/wavetable-dsp.test.mjs scripts/fetch-wavetables.mjs Resources/wavetables
git commit -m "Convert AKWF waveforms into embedded wavetables"
```

---

### Task 2: C++ test target and the `.xwt` parser

The first C++ tests in the project. The harness is folded in here because nothing downstream can be tested without it.

**Files:**
- Create: `Tests/main.cpp`, `Tests/WavetableTests.cpp`
- Create: `Source/dsp/WavetableBlob.h`, `Source/dsp/WavetableBlob.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the `.xwt` layout from Task 1.
- Produces: `dsp::BlobView { const float* samples; int frames; int frameSize; const float* frame(int) const; }` and `std::optional<dsp::BlobView> dsp::parseXwt (const void* data, size_t sizeInBytes) noexcept`. Task 3 builds mipmaps from a `BlobView`.

- [ ] **Step 1: Add the test target to CMake**

In `CMakeLists.txt`, after the `serum_style_synth_set_warnings(SerumStyleSynth)` line, add:

```cmake
# --- Unit tests -----------------------------------------------------------
# Console app: gira juce::UnitTestRunner su dsp/ ed engine/. Non tocca la UI
# né l'host, quindi niente juce_gui_extra e niente WebView.
juce_add_console_app(XerumTests PRODUCT_NAME "XerumTests")

target_sources(XerumTests
    PRIVATE
        Tests/main.cpp
        Tests/WavetableTests.cpp
        Source/dsp/WavetableBlob.cpp)

target_include_directories(XerumTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(XerumTests
    PRIVATE
        JUCE_UNIT_TESTS=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

serum_style_synth_set_warnings(XerumTests)

target_link_libraries(XerumTests
    PRIVATE
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
```

- [ ] **Step 2: Write the runner**

Create `Tests/main.cpp`:

```cpp
#include <juce_core/juce_core.h>

#include <iostream>

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TEST FAILURES") << std::endl;
    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 3: Write the failing tests**

Create `Tests/WavetableTests.cpp`:

```cpp
#include "dsp/WavetableBlob.h"

#include <juce_core/juce_core.h>

#include <cstring>
#include <vector>

namespace
{
/** Blob .xwt sintetico: `frames` frame di `frameSize` campioni, tutti a `value`. */
std::vector<char> makeBlob (uint32_t frames, uint32_t frameSize, float value = 0.5f)
{
    std::vector<char> bytes (12 + (size_t) frames * frameSize * sizeof (float));
    std::memcpy (bytes.data(), "XWT1", 4);
    std::memcpy (bytes.data() + 4, &frames, 4);
    std::memcpy (bytes.data() + 8, &frameSize, 4);

    for (size_t i = 0; i < (size_t) frames * frameSize; ++i)
        std::memcpy (bytes.data() + 12 + i * sizeof (float), &value, sizeof (float));

    return bytes;
}
} // namespace

struct WavetableBlobTests final : juce::UnitTest
{
    WavetableBlobTests() : juce::UnitTest ("WavetableBlob", "dsp") {}

    void runTest() override
    {
        beginTest ("un blob valido viene accettato e i frame sono raggiungibili");
        {
            const auto bytes = makeBlob (4, 8, 0.25f);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());
            expectEquals (view->frames, 4);
            expectEquals (view->frameSize, 8);
            expectWithinAbsoluteError (view->frame (3)[7], 0.25f, 1.0e-6f);
        }

        beginTest ("magic sbagliato: rifiutato");
        {
            auto bytes = makeBlob (2, 4);
            bytes[1] = 'X';
            expect (! dsp::parseXwt (bytes.data(), bytes.size()).has_value());
        }

        beginTest ("blob troncato: rifiutato");
        {
            const auto bytes = makeBlob (4, 8);
            expect (! dsp::parseXwt (bytes.data(), bytes.size() - 4).has_value());
        }

        beginTest ("frameSize non potenza di due: rifiutato");
        {
            const auto bytes = makeBlob (2, 6);
            expect (! dsp::parseXwt (bytes.data(), bytes.size()).has_value());
        }

        beginTest ("dimensioni assurde: rifiutate senza leggere fuori");
        {
            auto bytes = makeBlob (2, 4);
            const uint32_t huge = 1000000;
            std::memcpy (bytes.data() + 4, &huge, 4);
            expect (! dsp::parseXwt (bytes.data(), bytes.size()).has_value());
        }

        beginTest ("puntatore nullo o buffer minuscolo: rifiutati");
        {
            expect (! dsp::parseXwt (nullptr, 1024).has_value());
            const char tiny[4] = { 'X', 'W', 'T', '1' };
            expect (! dsp::parseXwt (tiny, sizeof (tiny)).has_value());
        }
    }
};

static WavetableBlobTests wavetableBlobTests;
```

- [ ] **Step 4: Run to verify the build fails**

Run: `cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests`
Expected: FAIL — `'dsp/WavetableBlob.h' file not found`.

- [ ] **Step 5: Write the parser**

Create `Source/dsp/WavetableBlob.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dsp
{
/** Vista di sola lettura su un blob .xwt già in memoria (BinaryData): non possiede niente. */
struct BlobView
{
    const float* samples { nullptr };
    int frames { 0 };
    int frameSize { 0 };

    const float* frame (int index) const noexcept
    {
        return samples + (size_t) index * (size_t) frameSize;
    }
};

/** Valida magic, dimensioni, allineamento e lunghezza. Nessuna allocazione, nessuna eccezione. */
std::optional<BlobView> parseXwt (const void* data, size_t sizeInBytes) noexcept;
} // namespace dsp
```

Create `Source/dsp/WavetableBlob.cpp`:

```cpp
#include "dsp/WavetableBlob.h"

#include <cstring>

namespace dsp
{
namespace
{
// Limiti di sanità: un blob che dichiara di più è corrotto, non ambizioso.
constexpr uint32_t kMaxFrames = 256;
constexpr uint32_t kMaxFrameSize = 4096;

uint32_t readLittleEndian32 (const uint8_t* p) noexcept
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}
} // namespace

std::optional<BlobView> parseXwt (const void* data, size_t sizeInBytes) noexcept
{
    if (data == nullptr || sizeInBytes < 12)
        return std::nullopt;

    const auto* bytes = static_cast<const uint8_t*> (data);

    if (std::memcmp (bytes, "XWT1", 4) != 0)
        return std::nullopt;

    const auto frames = readLittleEndian32 (bytes + 4);
    const auto frameSize = readLittleEndian32 (bytes + 8);

    if (frames == 0 || frames > kMaxFrames || frameSize == 0 || frameSize > kMaxFrameSize)
        return std::nullopt;

    // Potenza di due: la mipmap dimezza la lunghezza a ogni livello.
    if ((frameSize & (frameSize - 1)) != 0)
        return std::nullopt;

    const auto expected = (size_t) 12 + (size_t) frames * (size_t) frameSize * sizeof (float);

    if (sizeInBytes < expected)
        return std::nullopt;

    // I campioni vengono letti come float: se il blob non è allineato, meglio
    // rifiutarlo che fare una lettura non allineata.
    if ((reinterpret_cast<uintptr_t> (bytes + 12) % alignof (float)) != 0)
        return std::nullopt;

    BlobView view;
    view.samples = reinterpret_cast<const float*> (bytes + 12);
    view.frames = (int) frames;
    view.frameSize = (int) frameSize;
    return view;
}
} // namespace dsp
```

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests \
  && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```
Expected: `ALL TESTS PASSED`, exit code 0.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt Tests Source/dsp/WavetableBlob.h Source/dsp/WavetableBlob.cpp
git commit -m "Add a C++ unit test target and the .xwt parser"
```

---

### Task 3: `MipTable` and the band-limited mipmap

**Files:**
- Create: `Source/dsp/MipTable.h`, `Source/dsp/MipTable.cpp`
- Modify: `Source/dsp/WavetableStore.h`, `Source/dsp/WavetableStore.cpp` (new file — the header today is a comment-only placeholder)
- Modify: `Tests/WavetableTests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `dsp::parseXwt`, `dsp::BlobView` (Task 2).
- Produces:
  - `dsp::MipTable` with `static constexpr int kMaxLevel = 6`, `int getNumFrames() const noexcept`, `int sizeAtLevel (int level) const noexcept`, `const float* samples (int frame, int level) const noexcept`.
  - `std::unique_ptr<dsp::MipTable> dsp::buildMipTable (const BlobView&)`.
  - `dsp::WavetableStore` with `void setActive (int index)`, `const MipTable* active() const noexcept`, `int getNumTables() const noexcept`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/WavetableTests.cpp` (and add `#include "dsp/MipTable.h"` / `#include "dsp/WavetableStore.h"` / `#include <cmath>` at the top):

```cpp
namespace
{
/** Blob con un solo frame contenente la somma delle prime `harmonics` armoniche. */
std::vector<char> makeHarmonicBlob (uint32_t frameSize, int harmonics)
{
    const uint32_t frames = 1;
    std::vector<char> bytes (12 + (size_t) frames * frameSize * sizeof (float));
    std::memcpy (bytes.data(), "XWT1", 4);
    std::memcpy (bytes.data() + 4, &frames, 4);
    std::memcpy (bytes.data() + 8, &frameSize, 4);

    for (uint32_t i = 0; i < frameSize; ++i)
    {
        float v = 0.0f;
        for (int h = 1; h <= harmonics; ++h)
            v += std::sin (2.0f * juce::MathConstants<float>::pi * (float) h * (float) i / (float) frameSize) / (float) h;

        std::memcpy (bytes.data() + 12 + i * sizeof (float), &v, sizeof (float));
    }

    return bytes;
}

/** Ampiezza dell'armonica h in un ciclo lungo `size`. */
float harmonicAmplitude (const float* samples, int size, int h)
{
    float re = 0.0f;
    float im = 0.0f;

    for (int i = 0; i < size; ++i)
    {
        const auto a = 2.0f * juce::MathConstants<float>::pi * (float) h * (float) i / (float) size;
        re += samples[i] * std::cos (a);
        im -= samples[i] * std::sin (a);
    }

    return 2.0f * std::sqrt (re * re + im * im) / (float) size;
}
} // namespace

struct MipTableTests final : juce::UnitTest
{
    MipTableTests() : juce::UnitTest ("MipTable", "dsp") {}

    void runTest() override
    {
        beginTest ("ogni livello dimezza la lunghezza");
        {
            const auto bytes = makeHarmonicBlob (2048, 64);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());

            const auto table = dsp::buildMipTable (*view);
            expect (table != nullptr);
            expectEquals (table->getNumFrames(), 1);
            expectEquals (table->sizeAtLevel (0), 2048);
            expectEquals (table->sizeAtLevel (3), 256);
            expectEquals (table->sizeAtLevel (dsp::MipTable::kMaxLevel), 32);
        }

        beginTest ("il livello 0 conserva la forma d'onda originale");
        {
            const auto bytes = makeHarmonicBlob (2048, 64);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            float maxError = 0.0f;
            for (int i = 0; i < 2048; ++i)
                maxError = juce::jmax (maxError, std::abs (table->samples (0, 0)[i] - view->frame (0)[i]));

            expect (maxError < 1.0e-4f, "errore massimo " + juce::String (maxError));
        }

        beginTest ("i livelli alti tagliano le armoniche sopra la loro Nyquist");
        {
            const auto bytes = makeHarmonicBlob (2048, 200);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            // Livello 4 → 128 campioni → al massimo 64 armoniche.
            const auto* level4 = table->samples (0, 4);
            expect (harmonicAmplitude (level4, 128, 10) > 0.05f, "l'armonica 10 deve restare");
            expect (harmonicAmplitude (level4, 128, 63) < 0.05f, "l'armonica 63 deve essere attenuata o assente");
        }
    }
};

static MipTableTests mipTableTests;

struct WavetableStoreTests final : juce::UnitTest
{
    WavetableStoreTests() : juce::UnitTest ("WavetableStore", "dsp") {}

    void runTest() override
    {
        beginTest ("prima di setActive non c'è nessuna tavola attiva");
        {
            dsp::WavetableStore store;
            expect (store.active() == nullptr);
            expect (store.getNumTables() > 0);
        }

        beginTest ("setActive costruisce la tavola e la pubblica");
        {
            dsp::WavetableStore store;
            store.setActive (0);
            const auto* first = store.active();
            expect (first != nullptr);
            expectEquals (first->getNumFrames(), 64);
            expectEquals (first->sizeAtLevel (0), 2048);
        }

        beginTest ("tornare su una tavola già costruita restituisce lo stesso puntatore");
        {
            dsp::WavetableStore store;
            store.setActive (0);
            const auto* first = store.active();
            store.setActive (1);
            store.setActive (0);
            expect (store.active() == first, "le tavole costruite non vengono mai liberate");
        }

        beginTest ("un indice fuori range non cambia la tavola attiva");
        {
            dsp::WavetableStore store;
            store.setActive (0);
            const auto* first = store.active();
            store.setActive (99);
            store.setActive (-1);
            expect (store.active() == first);
        }
    }
};

static WavetableStoreTests wavetableStoreTests;
```

- [ ] **Step 2: Run to verify the build fails**

Run: `cmake --build --preset macos-debug --target XerumTests`
Expected: FAIL — `'dsp/MipTable.h' file not found`.

- [ ] **Step 3: Write `MipTable`**

Create `Source/dsp/MipTable.h`:

```cpp
#pragma once

#include <vector>

namespace dsp
{
/**
 * Una tavola pronta per il thread audio: `frames` frame, ognuno con la piramide
 * dei livelli band-limited. Il livello k ha `frameSize >> k` campioni, cioè metà
 * armoniche del livello precedente. Memoria totale per frame ≈ 2 × frameSize,
 * non (kMaxLevel + 1) × frameSize.
 *
 * Costruita sul message thread, letta dal thread audio: dopo la costruzione è
 * immutabile e non viene mai distrutta finché vive il plugin.
 */
class MipTable
{
public:
    static constexpr int kMaxLevel = 6;

    MipTable (int frames, int frameSize);

    int getNumFrames() const noexcept { return frames_; }
    int getFrameSize() const noexcept { return frameSize_; }
    int sizeAtLevel (int level) const noexcept { return frameSize_ >> level; }

    /** Solo durante la costruzione. */
    float* writePointer (int frame, int level) noexcept;

    /** Thread audio. */
    const float* samples (int frame, int level) const noexcept;

private:
    int indexOf (int frame, int level) const noexcept;

    int frames_;
    int frameSize_;
    int stridePerFrame_ { 0 };
    std::vector<int> levelOffsets_;
    std::vector<float> data_;
};
} // namespace dsp
```

Create `Source/dsp/MipTable.cpp`:

```cpp
#include "dsp/MipTable.h"

namespace dsp
{
MipTable::MipTable (int frames, int frameSize)
    : frames_ (frames), frameSize_ (frameSize)
{
    levelOffsets_.resize (kMaxLevel + 1);

    for (int level = 0; level <= kMaxLevel; ++level)
    {
        levelOffsets_[(size_t) level] = stridePerFrame_;
        stridePerFrame_ += frameSize_ >> level;
    }

    data_.assign ((size_t) frames_ * (size_t) stridePerFrame_, 0.0f);
}

int MipTable::indexOf (int frame, int level) const noexcept
{
    return frame * stridePerFrame_ + levelOffsets_[(size_t) level];
}

float* MipTable::writePointer (int frame, int level) noexcept
{
    return data_.data() + indexOf (frame, level);
}

const float* MipTable::samples (int frame, int level) const noexcept
{
    return data_.data() + indexOf (frame, level);
}
} // namespace dsp
```

- [ ] **Step 4: Write the store and the mipmap builder**

Replace `Source/dsp/WavetableStore.h` with:

```cpp
#pragma once

#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"

#include <atomic>
#include <memory>
#include <vector>

namespace dsp
{
/** Costruisce la piramide band-limited di un blob. Alloca: mai dal thread audio. */
std::unique_ptr<MipTable> buildMipTable (const BlobView& blob);

/**
 * Possiede le tavole del plugin e ne pubblica una al thread audio.
 *
 * Le tavole costruite non vengono mai distrutte: è questo che rende sicuro il
 * puntatore atomico. Il thread audio può leggere `active()` mentre il message
 * thread ne costruisce un'altra, e nessuno gli toglie la memoria da sotto.
 * Costo massimo ≈ 1 MB per tavola.
 */
class WavetableStore
{
public:
    WavetableStore();

    /** Quante tavole conosce (una per opzione di `wtIndex`). */
    int getNumTables() const noexcept { return (int) tables_.size(); }

    /** Message thread / prepareToPlay: costruisce se serve e pubblica. Indice fuori range: nessun effetto. */
    void setActive (int index);

    /** Thread audio: la tavola pronta, o nullptr se non ce n'è ancora nessuna. */
    const MipTable* active() const noexcept { return active_.load (std::memory_order_acquire); }

private:
    std::vector<std::unique_ptr<MipTable>> tables_;
    std::atomic<const MipTable*> active_ { nullptr };
};
} // namespace dsp
```

Create `Source/dsp/WavetableStore.cpp`:

```cpp
#include "dsp/WavetableStore.h"

#include <juce_dsp/juce_dsp.h>

#include "BinaryData.h" // generato da juce_add_binary_data, namespace WavetableAssets

namespace dsp
{
namespace
{
constexpr int kFftOrder = 11; // 2048

/** I file .xwt nell'ordine delle opzioni di `wtIndex` in parameters.json. */
const char* const kTableFiles[] = { "basic.xwt", "saws.xwt", "grit.xwt", "vocal.xwt", "bells.xwt", "pwm.xwt" };

std::optional<BlobView> lookupBlob (const char* fileName)
{
    for (int i = 0; i < WavetableAssets::namedResourceListSize; ++i)
    {
        if (juce::String (WavetableAssets::originalFilenames[i]) != fileName)
            continue;

        int size = 0;
        const auto* data = WavetableAssets::getNamedResource (WavetableAssets::namedResourceList[i], size);

        if (data == nullptr || size <= 0)
            return std::nullopt;

        return parseXwt (data, (size_t) size);
    }

    return std::nullopt;
}
} // namespace

std::unique_ptr<MipTable> buildMipTable (const BlobView& blob)
{
    auto table = std::make_unique<MipTable> (blob.frames, blob.frameSize);

    juce::dsp::FFT forward { kFftOrder };
    std::vector<juce::dsp::Complex<float>> timeDomain ((size_t) blob.frameSize);
    std::vector<juce::dsp::Complex<float>> spectrum ((size_t) blob.frameSize);

    for (int frame = 0; frame < blob.frames; ++frame)
    {
        const auto* source = blob.frame (frame);

        for (int i = 0; i < blob.frameSize; ++i)
            timeDomain[(size_t) i] = { source[i], 0.0f };

        forward.perform (timeDomain.data(), spectrum.data(), false);

        for (int level = 0; level <= MipTable::kMaxLevel; ++level)
        {
            const int size = blob.frameSize >> level;
            const int harmonics = size / 2;

            juce::dsp::FFT inverse { kFftOrder - level };
            std::vector<juce::dsp::Complex<float>> shortSpectrum ((size_t) size, { 0.0f, 0.0f });
            std::vector<juce::dsp::Complex<float>> shortTime ((size_t) size);

            // Si tengono le armoniche 0..harmonics-1 e i loro gemelli negativi:
            // tutto quello che sta sopra produrrebbe aliasing a questa lunghezza.
            for (int bin = 0; bin < harmonics; ++bin)
                shortSpectrum[(size_t) bin] = spectrum[(size_t) bin];

            for (int bin = 1; bin < harmonics; ++bin)
                shortSpectrum[(size_t) (size - bin)] = spectrum[(size_t) (blob.frameSize - bin)];

            inverse.perform (shortSpectrum.data(), shortTime.data(), true);

            // L'antitrasformata divide per `size`, ma i bin vengono da una FFT
            // lunga `frameSize`: il fattore di scala rimette le cose a posto.
            const auto scale = (float) size / (float) blob.frameSize;
            auto* destination = table->writePointer (frame, level);

            for (int i = 0; i < size; ++i)
                destination[i] = shortTime[(size_t) i].real() * scale;
        }
    }

    return table;
}

WavetableStore::WavetableStore()
{
    tables_.resize (std::size (kTableFiles));
}

void WavetableStore::setActive (int index)
{
    if (index < 0 || index >= getNumTables())
        return;

    if (tables_[(size_t) index] == nullptr)
    {
        const auto blob = lookupBlob (kTableFiles[index]);

        if (! blob.has_value())
            return; // blob assente o corrotto: si resta sulla tavola precedente

        tables_[(size_t) index] = buildMipTable (*blob);
    }

    active_.store (tables_[(size_t) index].get(), std::memory_order_release);
}
} // namespace dsp
```

- [ ] **Step 5: Wire the wavetable blobs and the new sources into CMake**

In `CMakeLists.txt`:

1. after the `juce_add_plugin(...)` block, add the binary data target:

```cmake
# --- Wavetables ------------------------------------------------------------
# Sempre incorporate (a differenza del bundle web, che è solo in Release):
# senza tavole il sintetizzatore non ha niente da suonare.
file(GLOB XERUM_WAVETABLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/wavetables/*.xwt)
if(NOT XERUM_WAVETABLES)
    message(FATAL_ERROR "Nessuna tavola in Resources/wavetables. Generale con: node scripts/fetch-wavetables.mjs")
endif()
juce_add_binary_data(WavetableAssets SOURCES ${XERUM_WAVETABLES} NAMESPACE WavetableAssets)
```

2. add to `target_sources(SerumStyleSynth …)`: `Source/dsp/WavetableBlob.cpp`, `Source/dsp/MipTable.cpp`, `Source/dsp/WavetableStore.cpp`
3. add `WavetableAssets` to the plugin's `target_link_libraries(... PRIVATE ...)`
4. add to `target_sources(XerumTests …)`: `Source/dsp/MipTable.cpp`, `Source/dsp/WavetableStore.cpp`
5. add `WavetableAssets` to the test target's `target_link_libraries(... PRIVATE ...)`

`juce_add_binary_data` generates one `BinaryData.h` per target; both the plugin and the tests link `WavetableAssets`, so both see the same header.

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests \
  && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```
Expected: `ALL TESTS PASSED`. If "l'armonica 63 deve essere attenuata" fails, the bin copy in `buildMipTable` is wrong — check the negative-frequency loop.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt Source/dsp Tests
git commit -m "Build band-limited wavetable mipmaps and publish them to the audio thread"
```

---

### Task 4: The oscillator — first sound

**Files:**
- Modify: `Source/dsp/WavetableOscillator.h`, `Source/dsp/WavetableOscillator.cpp`
- Modify: `Source/engine/SynthVoice.h`, `Source/engine/SynthVoice.cpp`, `Source/engine/VoiceManager.h`, `Source/engine/VoiceManager.cpp`, `Source/engine/SynthEngine.h`, `Source/engine/SynthEngine.cpp`
- Modify: `Source/plugin/PluginProcessor.h`, `Source/plugin/PluginProcessor.cpp`
- Modify: `Source/util/RealtimeHelpers.h`, `Tests/WavetableTests.cpp`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `dsp::MipTable`, `dsp::WavetableStore` (Task 3).
- Produces:
  - `int dsp::levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept`
  - `dsp::WavetableOscillator` with `void prepare (double)`, `void reset()`, `void setTable (const MipTable*)`, `void setFrequencyHz (float)`, `void setFramePosition (float normalised)`, `float getSample()`
  - `engine::SynthEngine::setWavetable (const dsp::MipTable*)` and `engine::VoiceManager::setWavetable (const dsp::MipTable*)`

- [ ] **Step 1: Write the failing tests**

Append to `Tests/WavetableTests.cpp` (add `#include "dsp/WavetableOscillator.h"`):

```cpp
struct OscillatorTests final : juce::UnitTest
{
    OscillatorTests() : juce::UnitTest ("WavetableOscillator", "dsp") {}

    void runTest() override
    {
        beginTest ("il livello scelto non contiene armoniche sopra Nyquist");
        {
            // A 44.1 kHz, un La4 (440 Hz) ammette ~50 armoniche → serve un livello
            // da 128 campioni (64 armoniche) o più corto.
            expectEquals (dsp::levelForFrequency (440.0f, 44100.0, 2048), 5);
            // Un La1 (55 Hz) ammette ~400 armoniche → livello 2 (512 campioni, 256 armoniche).
            expectEquals (dsp::levelForFrequency (55.0f, 44100.0, 2048), 2);
            // Frequenze assurde non devono uscire dai limiti.
            expectEquals (dsp::levelForFrequency (0.0f, 44100.0, 2048), dsp::MipTable::kMaxLevel);
            expectEquals (dsp::levelForFrequency (20000.0f, 44100.0, 2048), dsp::MipTable::kMaxLevel);
        }

        beginTest ("senza tavola l'oscillatore tace invece di dereferenziare");
        {
            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (nullptr);
            osc.setFrequencyHz (440.0f);
            for (int i = 0; i < 64; ++i)
                expectWithinAbsoluteError (osc.getSample(), 0.0f, 0.0f);
        }

        beginTest ("un seno in tavola esce come un seno");
        {
            const auto bytes = makeHarmonicBlob (2048, 1); // una sola armonica
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (table.get());
            osc.setFramePosition (0.0f);
            osc.setFrequencyHz (441.0f); // 100 campioni per ciclo esatti

            std::vector<float> rendered (100);
            for (auto& s : rendered)
                s = osc.getSample();

            float maxError = 0.0f;
            for (int i = 0; i < 100; ++i)
                maxError = juce::jmax (maxError, std::abs (rendered[(size_t) i]
                                                           - std::sin (2.0f * juce::MathConstants<float>::pi * (float) i / 100.0f)));

            expect (maxError < 0.02f, "errore massimo " + juce::String (maxError));
        }

        beginTest ("la posizione fra due frame interpola invece di saltare");
        {
            // Due frame: costante -1 e costante +1 (l'interpolazione è leggibile a occhio).
            const uint32_t frames = 2, frameSize = 64;
            std::vector<char> bytes (12 + (size_t) frames * frameSize * sizeof (float));
            std::memcpy (bytes.data(), "XWT1", 4);
            std::memcpy (bytes.data() + 4, &frames, 4);
            std::memcpy (bytes.data() + 8, &frameSize, 4);
            for (uint32_t i = 0; i < frameSize; ++i)
            {
                const float lo = -1.0f, hi = 1.0f;
                std::memcpy (bytes.data() + 12 + i * sizeof (float), &lo, sizeof (float));
                std::memcpy (bytes.data() + 12 + (frameSize + i) * sizeof (float), &hi, sizeof (float));
            }

            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (table.get());
            osc.setFrequencyHz (100.0f);
            osc.setFramePosition (0.5f);

            expectWithinAbsoluteError (osc.getSample(), 0.0f, 0.05f);

            osc.setFramePosition (0.0f);
            expectWithinAbsoluteError (osc.getSample(), -1.0f, 0.05f);
        }
    }
};

static OscillatorTests oscillatorTests;
```

- [ ] **Step 2: Run to verify the tests fail**

Run: `cmake --build --preset macos-debug --target XerumTests`
Expected: FAIL — `no member named 'setTable'` / `levelForFrequency`.

- [ ] **Step 3: Write the oscillator**

Replace `Source/dsp/WavetableOscillator.h` with:

```cpp
#pragma once

#include "dsp/MipTable.h"

namespace dsp
{
/** Il livello più corto le cui armoniche stanno tutte sotto Nyquist a questa frequenza. */
int levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept;

/**
 * Oscillatore wavetable con doppia interpolazione: lineare fra campioni adiacenti
 * dentro il frame, lineare fra i due frame adiacenti alla posizione. È la seconda
 * che produce il morph: senza, muovere Position dà scatti.
 */
class WavetableOscillator
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Tavola attiva; nullptr significa silenzio, non crash. */
    void setTable (const MipTable* table) noexcept;

    void setFrequencyHz (float hz) noexcept;

    /** Posizione nel morph, 0..1 sull'intero set di frame. */
    void setFramePosition (float normalised) noexcept;

    float getSample() noexcept;

private:
    void updateLevel() noexcept;

    double sampleRate_ { 44100.0 };
    double phase_ { 0.0 };
    double phaseIncrement_ { 0.0 };
    float frequencyHz_ { 0.0f };

    const MipTable* table_ { nullptr };
    int level_ { 0 };
    int frameLo_ { 0 };
    int frameHi_ { 0 };
    float frameMix_ { 0.0f };
};
} // namespace dsp
```

Replace `Source/dsp/WavetableOscillator.cpp` with:

```cpp
#include "dsp/WavetableOscillator.h"

namespace dsp
{
namespace
{
/** Interpolazione lineare dentro un frame, con wrap sull'ultimo campione. */
float sampleAt (const float* frame, int size, int index, float fraction) noexcept
{
    const float a = frame[index];
    const float b = frame[index + 1 < size ? index + 1 : 0];
    return a + (b - a) * fraction;
}
} // namespace

int levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept
{
    if (frequencyHz <= 0.0f || sampleRate <= 0.0)
        return MipTable::kMaxLevel;

    // Quante armoniche stanno sotto Nyquist a questa frequenza.
    const double maxHarmonics = sampleRate / (2.0 * (double) frequencyHz);

    int level = 0;
    while (level < MipTable::kMaxLevel && (double) ((frameSize >> level) / 2) > maxHarmonics)
        ++level;

    return level;
}

void WavetableOscillator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
    updateLevel();
}

void WavetableOscillator::reset() noexcept
{
    phase_ = 0.0;
}

void WavetableOscillator::setTable (const MipTable* table) noexcept
{
    table_ = table;
    frameLo_ = frameHi_ = 0;
    frameMix_ = 0.0f;
    updateLevel();
}

void WavetableOscillator::setFrequencyHz (float hz) noexcept
{
    frequencyHz_ = hz;
    phaseIncrement_ = (double) hz / sampleRate_;
    updateLevel();
}

void WavetableOscillator::setFramePosition (float normalised) noexcept
{
    if (table_ == nullptr)
        return;

    const int lastFrame = table_->getNumFrames() - 1;
    const float position = juce::jlimit (0.0f, 1.0f, normalised) * (float) lastFrame;

    frameLo_ = (int) position;
    frameHi_ = frameLo_ < lastFrame ? frameLo_ + 1 : lastFrame;
    frameMix_ = position - (float) frameLo_;
}

void WavetableOscillator::updateLevel() noexcept
{
    level_ = table_ == nullptr ? 0 : levelForFrequency (frequencyHz_, sampleRate_, table_->getFrameSize());
}

float WavetableOscillator::getSample() noexcept
{
    if (table_ == nullptr)
        return 0.0f;

    const int size = table_->sizeAtLevel (level_);
    const double position = phase_ * (double) size;
    const int index = juce::jlimit (0, size - 1, (int) position);
    const auto fraction = (float) (position - (double) index);

    const float lo = sampleAt (table_->samples (frameLo_, level_), size, index, fraction);
    const float hi = sampleAt (table_->samples (frameHi_, level_), size, index, fraction);

    phase_ += phaseIncrement_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;

    return lo + (hi - lo) * frameMix_;
}
} // namespace dsp
```

Add `#include <juce_core/juce_core.h>` to `WavetableOscillator.h` for `juce::jlimit`.

- [ ] **Step 4: Run the oscillator tests**

Run: `cmake --build --preset macos-debug --target XerumTests && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 5: Commit the oscillator**

```bash
git add Source/dsp/WavetableOscillator.h Source/dsp/WavetableOscillator.cpp Tests/WavetableTests.cpp
git commit -m "Interpolate the wavetable across samples and frames"
```

- [ ] **Step 6: Route a table into the voices**

In `Source/engine/SynthVoice.h`, add to the public interface and remove nothing else yet:

```cpp
    /** La tavola attiva, o nullptr. Chiamata dal message thread tramite VoiceManager. */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /** Posizione del morph, 0..1. */
    void setFramePosition (float normalised) noexcept;
```

In `Source/engine/SynthVoice.cpp`:

```cpp
void SynthVoice::setWavetable (const dsp::MipTable* table) noexcept
{
    oscillator_.setTable (table);
}

void SynthVoice::setFramePosition (float normalised) noexcept
{
    oscillator_.setFramePosition (normalised);
}
```

and in `SynthVoice::render`, delete the `if constexpr (util::kEnableTestTone)` branch and its `else`, leaving:

```cpp
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = oscillator_.getSample() * amp * envelope_.getNextSample();
        sample = filter_.processSample (sample);

        outL[i] += sample;
        outR[i] += sample;
    }
```

Delete `phase_` and `frequencyHz_`'s sine bookkeeping if they become unused (`frequencyHz_` stays: `start()` uses it). Remove the now-unused `#include "util/RealtimeHelpers.h"` only if nothing else in the file needs it.

Add matching pass-throughs in `VoiceManager` (`void setWavetable (const dsp::MipTable*) noexcept;` and `void setFramePosition (float) noexcept;`, both looping over the voice pool) and in `SynthEngine` (`void setWavetable (const dsp::MipTable*) noexcept;` forwarding to `voices_`).

- [ ] **Step 7: Build the table in the processor**

In `Source/plugin/PluginProcessor.h`, add the member and the listener:

```cpp
    dsp::WavetableStore wavetables_;
    std::atomic<float>* wtIndexParam_ { nullptr };
    std::atomic<float>* wtposParam_ { nullptr };
    int lastWavetableIndex_ { -1 };
```

(`#include "dsp/WavetableStore.h"`.)

In the constructor, after the existing `getRawParameterValue` calls:

```cpp
    wtIndexParam_ = apvts_.getRawParameterValue ("wtIndex");
    wtposParam_ = apvts_.getRawParameterValue ("wtpos");
```

In `prepareToPlay`, before `engine_->prepare (spec)`:

```cpp
    // Costruire la tavola alloca: qui è lecito, in processBlock no.
    const auto index = wavetableIndexFromParam();
    wavetables_.setActive (index);
    lastWavetableIndex_ = index;
    engine_->setWavetable (wavetables_.active());
```

with the helper (private, in the .cpp):

```cpp
int SerumStyleSynthAudioProcessor::wavetableIndexFromParam() const noexcept
{
    if (wtIndexParam_ == nullptr)
        return 0;

    // `wtIndex` è un AudioParameterChoice: il valore grezzo è già l'indice.
    return juce::jlimit (0, wavetables_.getNumTables() - 1,
                         (int) wtIndexParam_->load (std::memory_order_relaxed));
}
```

In `processBlock`, before `engine_->process(...)`:

```cpp
    if (wtposParam_ != nullptr)
        engine_->setFramePosition (wtposParam_->load (std::memory_order_relaxed));
```

Wiring the `wtIndex` change to a message-thread rebuild comes in Task 7 with the rest of the parameters; until then the table is whatever `prepareToPlay` built.

- [ ] **Step 8: Build the plugin and listen**

Run:
```bash
cmake --preset macos-debug && cmake --build --preset macos-debug --target SerumStyleSynth_Standalone
open build/macos-debug/SerumStyleSynth_artefacts/Debug/Standalone/SerumStyleSynth.app
```
Expected: clicking the on-screen keyboard now **produces sound** — a raw, unfiltered, un-enveloped tone that clicks on note-off (the envelope is still a hard gate). That click is expected and Task 5 fixes it.

- [ ] **Step 9: Commit**

```bash
git add Source/engine Source/plugin Source/util/RealtimeHelpers.h
git commit -m "Feed the wavetable through the voices so the synth makes sound"
```

---

### Task 5: The ADSR envelope

**Files:**
- Modify: `Source/dsp/ADSREnvelope.h`, `Source/dsp/ADSREnvelope.cpp`
- Create: `Tests/EnvelopeFilterTests.cpp`
- Modify: `CMakeLists.txt` (add the test source and `Source/dsp/ADSREnvelope.cpp` to `XerumTests`)

**Interfaces:**
- Produces: `dsp::ADSREnvelope` with `void prepare (double)`, `void reset()`, `void setAttackSeconds (float)`, `void setDecaySeconds (float)`, `void setSustainLevel (float)`, `void setReleaseSeconds (float)`, `void noteOn (float velocityAmount)`, `void noteOff()`, `bool isActive() const`, `float getNextSample()`.

**Timing contract** (the tests encode it, so it must be stated): each stage is a one-pole moving toward a target, with its coefficient chosen so the *nominal* time is reached, not approached forever.
- attack: target 1, reaches ≥ 0.99 at `att`
- decay: target `sustain`, covers 99 % of the distance in `dec`
- release: target 0, falls below -80 dB (1e-4) at `rel`, and the envelope then reports inactive

- [ ] **Step 1: Write the failing tests**

Create `Tests/EnvelopeFilterTests.cpp`:

```cpp
#include "dsp/ADSREnvelope.h"

#include <juce_core/juce_core.h>

struct ADSRTests final : juce::UnitTest
{
    ADSRTests() : juce::UnitTest ("ADSREnvelope", "dsp") {}

    static float runFor (dsp::ADSREnvelope& env, int samples)
    {
        float last = 0.0f;
        for (int i = 0; i < samples; ++i)
            last = env.getNextSample();
        return last;
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("l'attacco arriva a 1 entro il tempo dichiarato, non prima");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            const auto halfway = runFor (env, 2400);  // 50 ms
            expect (halfway < 0.99f, "a metà attacco non deve essere già finito");

            const auto atEnd = runFor (env, 2400);    // 100 ms in tutto
            expect (atEnd >= 0.99f, "livello a fine attacco " + juce::String (atEnd));
        }

        beginTest ("il decay scende al sustain e ci resta");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.4f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            runFor (env, 48);                          // attacco
            const auto afterDecay = runFor (env, 2400); // 50 ms
            expectWithinAbsoluteError (afterDecay, 0.4f, 0.02f);

            const auto later = runFor (env, 48000);     // un secondo di sustain
            expectWithinAbsoluteError (later, 0.4f, 0.001f);
        }

        beginTest ("il release scende sotto -80 dB e l'inviluppo si spegne");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.001f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.05f);
            env.noteOn (1.0f);
            runFor (env, 480);

            env.noteOff();
            expect (env.isActive(), "durante il release la voce è ancora viva");

            const auto tail = runFor (env, 2400); // 50 ms
            expect (tail < 1.0e-4f, "coda " + juce::String (tail));
            expect (! env.isActive(), "a fine release la voce va liberata");
        }

        beginTest ("la velocity scala il picco quando envVel è attivo");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (0.5f);

            const auto peak = runFor (env, 480);
            expectWithinAbsoluteError (peak, 0.5f, 0.02f);
        }

        beginTest ("reset azzera tutto");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (0.1f);
            env.setSustainLevel (0.5f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);
            runFor (env, 480);
            env.reset();
            expect (! env.isActive());
            expectWithinAbsoluteError (env.getNextSample(), 0.0f, 0.0f);
        }
    }
};

static ADSRTests adsrTests;
```

Add `Tests/EnvelopeFilterTests.cpp` and `Source/dsp/ADSREnvelope.cpp` to the `XerumTests` sources in `CMakeLists.txt`.

- [ ] **Step 2: Run to verify the tests fail**

Run: `cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests`
Expected: FAIL — `no member named 'noteOn'` taking a float / the assertions fail with the hard-gate implementation.

- [ ] **Step 3: Write the envelope**

Replace `Source/dsp/ADSREnvelope.h` with:

```cpp
#pragma once

namespace dsp
{
/**
 * Inviluppo esponenziale stile analogico: ogni stadio è un polo singolo che
 * insegue un target. I coefficienti si ricalcolano solo quando cambia un
 * parametro o a note-on, mai per campione — nel loop audio non entra `exp`.
 *
 * I tempi sono nominali e vengono rispettati: l'attacco arriva a 0.99 in `att`,
 * il decay copre il 99 % della distanza in `dec`, il release scende sotto
 * -80 dB in `rel` e lì l'inviluppo si dichiara spento.
 */
class ADSREnvelope
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setAttackSeconds (float seconds) noexcept;
    void setDecaySeconds (float seconds) noexcept;
    void setSustainLevel (float level) noexcept;
    void setReleaseSeconds (float seconds) noexcept;

    /** `peak` è il livello massimo di questa nota (velocity già applicata). */
    void noteOn (float peak) noexcept;
    void noteOff() noexcept;

    bool isActive() const noexcept { return stage_ != Stage::idle; }
    float getNextSample() noexcept;

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void updateCoefficients() noexcept;

    double sampleRate_ { 44100.0 };
    Stage stage_ { Stage::idle };

    float attackSeconds_ { 0.01f };
    float decaySeconds_ { 0.1f };
    float sustain_ { 1.0f };
    float releaseSeconds_ { 0.1f };

    float attackCoeff_ { 1.0f };
    float decayCoeff_ { 1.0f };
    float releaseCoeff_ { 1.0f };

    float peak_ { 1.0f };
    float level_ { 0.0f };
};
} // namespace dsp
```

Replace `Source/dsp/ADSREnvelope.cpp` with:

```cpp
#include "dsp/ADSREnvelope.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
/** Soglia di spegnimento: -80 dB. Sotto, la coda è inudibile e la voce va liberata. */
constexpr float kSilence = 1.0e-4f;

/** Quante costanti di tempo servono per considerare finito ogni stadio. */
constexpr float kAttackTau = 4.6f;   // 1 - e^-4.6 ≈ 0.99
constexpr float kDecayTau = 4.6f;    // 99 % della distanza
constexpr float kReleaseTau = 9.21f; // e^-9.21 ≈ 1e-4, cioè -80 dB

float coefficientFor (float seconds, float constants, double sampleRate) noexcept
{
    const auto samples = std::max (1.0, (double) seconds * sampleRate);
    return 1.0f - (float) std::exp (-(double) constants / samples);
}
} // namespace

void ADSREnvelope::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients();
    reset();
}

void ADSREnvelope::reset() noexcept
{
    stage_ = Stage::idle;
    level_ = 0.0f;
}

void ADSREnvelope::setAttackSeconds (float seconds) noexcept
{
    attackSeconds_ = std::max (0.0f, seconds);
    updateCoefficients();
}

void ADSREnvelope::setDecaySeconds (float seconds) noexcept
{
    decaySeconds_ = std::max (0.0f, seconds);
    updateCoefficients();
}

void ADSREnvelope::setSustainLevel (float level) noexcept
{
    sustain_ = std::clamp (level, 0.0f, 1.0f);
}

void ADSREnvelope::setReleaseSeconds (float seconds) noexcept
{
    releaseSeconds_ = std::max (0.0f, seconds);
    updateCoefficients();
}

void ADSREnvelope::updateCoefficients() noexcept
{
    attackCoeff_ = coefficientFor (attackSeconds_, kAttackTau, sampleRate_);
    decayCoeff_ = coefficientFor (decaySeconds_, kDecayTau, sampleRate_);
    releaseCoeff_ = coefficientFor (releaseSeconds_, kReleaseTau, sampleRate_);
}

void ADSREnvelope::noteOn (float peak) noexcept
{
    peak_ = std::clamp (peak, 0.0f, 1.0f);
    stage_ = Stage::attack;
}

void ADSREnvelope::noteOff() noexcept
{
    if (stage_ != Stage::idle)
        stage_ = Stage::release;
}

float ADSREnvelope::getNextSample() noexcept
{
    switch (stage_)
    {
        case Stage::idle:
            return 0.0f;

        case Stage::attack:
            level_ += attackCoeff_ * (peak_ - level_);
            if (level_ >= peak_ * 0.99f)
            {
                level_ = peak_;
                stage_ = Stage::decay;
            }
            break;

        case Stage::decay:
        {
            const auto target = peak_ * sustain_;
            level_ += decayCoeff_ * (target - level_);
            if (std::abs (level_ - target) <= 0.01f * std::max (0.01f, peak_))
            {
                level_ = target;
                stage_ = Stage::sustain;
            }
            break;
        }

        case Stage::sustain:
            level_ = peak_ * sustain_;
            break;

        case Stage::release:
            level_ -= releaseCoeff_ * level_;
            if (level_ < kSilence)
            {
                level_ = 0.0f;
                stage_ = Stage::idle;
            }
            break;
    }

    return level_;
}
} // namespace dsp
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset macos-debug --target XerumTests && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 5: Use the envelope in the voice**

In `Source/engine/SynthVoice.cpp`, `start()` now passes the peak:

```cpp
void SynthVoice::start (int midiNote, float velocity) noexcept
{
    midiNote_ = midiNote;
    velocity_ = velocity;
    frequencyHz_ = midiNoteToHz (midiNote);
    oscillator_.setFrequencyHz (frequencyHz_);
    envelope_.noteOn (velocity);
    active_ = true;
}
```

and `render()` drops the separate `velocity_` multiplication, because the envelope already carries it:

```cpp
    const float amp = 0.2f;
```

(the `0.2f` headroom stays until Task 7 replaces it with the `level` parameter.)

- [ ] **Step 6: Build and listen**

Run: `cmake --build --preset macos-debug --target SerumStyleSynth_Standalone` and play.
Expected: notes now fade in and out instead of clicking.

- [ ] **Step 7: Commit**

```bash
git add Source/dsp/ADSREnvelope.h Source/dsp/ADSREnvelope.cpp Source/engine/SynthVoice.cpp Tests CMakeLists.txt
git commit -m "Give the envelope real attack, decay, sustain and release stages"
```

---

### Task 6: The state variable filter

**Files:**
- Modify: `Source/dsp/StateVariableFilter.h`, `Source/dsp/StateVariableFilter.cpp`
- Modify: `Tests/EnvelopeFilterTests.cpp`, `CMakeLists.txt` (add `Source/dsp/StateVariableFilter.cpp` to `XerumTests`)

**Interfaces:**
- Produces: `dsp::StateVariableFilter` with `enum class Type { lowPass, highPass, bandPass }`, `void prepare (double)`, `void reset()`, `void setType (Type)`, `void setCutoffHz (float)`, `void setResonance (float q)`, `void setNumStages (int)`, `float processSample (float)`.

- [ ] **Step 1: Write the failing tests**

Append to `Tests/EnvelopeFilterTests.cpp` (add `#include "dsp/StateVariableFilter.h"` and `#include <cmath>`):

```cpp
namespace
{
/** Ampiezza in uscita dal filtro a una data frequenza, misurata a regime. */
float filterGainAt (dsp::StateVariableFilter& filter, float frequencyHz, double sampleRate)
{
    filter.reset();

    const auto increment = 2.0 * juce::MathConstants<double>::pi * (double) frequencyHz / sampleRate;
    const int settle = (int) (sampleRate * 0.2);
    const int measure = (int) (sampleRate * 0.1);

    double phase = 0.0;
    for (int i = 0; i < settle; ++i, phase += increment)
        filter.processSample ((float) std::sin (phase));

    double sumSquares = 0.0;
    for (int i = 0; i < measure; ++i, phase += increment)
    {
        const auto out = filter.processSample ((float) std::sin (phase));
        sumSquares += (double) out * (double) out;
    }

    // RMS in uscita diviso l'RMS di un seno di ampiezza 1 (cioè 1/√2).
    return (float) (std::sqrt (sumSquares / measure) * std::sqrt (2.0));
}
} // namespace

struct StateVariableFilterTests final : juce::UnitTest
{
    StateVariableFilterTests() : juce::UnitTest ("StateVariableFilter", "dsp") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("passa-basso Butterworth: -3 dB al cutoff");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (1);
            filter.setResonance (juce::MathConstants<float>::sqrt2 / 2.0f); // Q = 0.707
            filter.setCutoffHz (1000.0f);

            expectWithinAbsoluteError (filterGainAt (filter, 1000.0f, sampleRate), 0.707f, 0.03f);
            expect (filterGainAt (filter, 100.0f, sampleRate) > 0.95f, "in banda passante deve passare");
            expect (filterGainAt (filter, 8000.0f, sampleRate) < 0.1f, "tre ottave sopra deve essere spento");
        }

        beginTest ("passa-alto: specchio del passa-basso");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::highPass);
            filter.setNumStages (1);
            filter.setResonance (juce::MathConstants<float>::sqrt2 / 2.0f);
            filter.setCutoffHz (1000.0f);

            expect (filterGainAt (filter, 100.0f, sampleRate) < 0.1f);
            expect (filterGainAt (filter, 8000.0f, sampleRate) > 0.95f);
        }

        beginTest ("24 dB taglia più ripido di 12 dB");
        {
            dsp::StateVariableFilter gentle, steep;
            for (auto* f : { &gentle, &steep })
            {
                f->prepare (sampleRate);
                f->setType (dsp::StateVariableFilter::Type::lowPass);
                f->setResonance (juce::MathConstants<float>::sqrt2 / 2.0f);
                f->setCutoffHz (1000.0f);
            }
            gentle.setNumStages (1);
            steep.setNumStages (2);

            expect (filterGainAt (steep, 4000.0f, sampleRate) < filterGainAt (gentle, 4000.0f, sampleRate) * 0.5f);
        }

        beginTest ("risonanza alta su tutto il range: nessun NaN, nessuna esplosione");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (2);
            filter.setResonance (20.0f);

            for (float cutoff : { 20.0f, 200.0f, 2000.0f, 19000.0f, 40000.0f })
            {
                filter.setCutoffHz (cutoff);
                filter.reset();

                float peak = 0.0f;
                for (int i = 0; i < 48000; ++i)
                {
                    const auto out = filter.processSample (i == 0 ? 1.0f : 0.0f);
                    expect (std::isfinite (out), "uscita non finita a cutoff " + juce::String (cutoff));
                    peak = juce::jmax (peak, std::abs (out));
                }

                expect (peak < 100.0f, "picco " + juce::String (peak) + " a cutoff " + juce::String (cutoff));
            }
        }
    }
};

static StateVariableFilterTests stateVariableFilterTests;
```

- [ ] **Step 2: Run to verify the tests fail**

Run: `cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests`
Expected: FAIL — `no member named 'setNumStages'`, and `Type` has different enumerators.

- [ ] **Step 3: Write the filter**

Replace `Source/dsp/StateVariableFilter.h` with:

```cpp
#pragma once

namespace dsp
{
/**
 * SVF topology-preserving (Zavalishin): resta stabile anche quando il cutoff
 * viene modulato in fretta, che è la ragione per cui non usiamo una biquad.
 *
 * `g = tan(π · fc / sr)` è l'unico `tan` del percorso e si ricalcola solo in
 * setCutoffHz(), che il chiamante invoca una volta per blocco.
 */
class StateVariableFilter
{
public:
    enum class Type { lowPass, highPass, bandPass };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setType (Type type) noexcept { type_ = type; }

    /** Il cutoff viene comunque limitato a 0.49 · sampleRate: oltre, tan() esplode. */
    void setCutoffHz (float hz) noexcept;

    /** Fattore di qualità: 0.707 = Butterworth, valori alti = risonanza. */
    void setResonance (float q) noexcept;

    /** 1 stadio = 12 dB/ottava, 2 = 24. */
    void setNumStages (int stages) noexcept;

    float processSample (float input) noexcept;

private:
    struct Stage
    {
        float s1 { 0.0f };
        float s2 { 0.0f };
    };

    float processStage (Stage& stage, float input) const noexcept;
    void updateCoefficients() noexcept;

    double sampleRate_ { 44100.0 };
    Type type_ { Type::lowPass };
    float cutoffHz_ { 1000.0f };
    float resonance_ { 0.707f };
    int numStages_ { 1 };

    float g_ { 0.0f };
    float twoR_ { 1.414f };
    float denominator_ { 1.0f };

    Stage stages_[2];
};
} // namespace dsp
```

Replace `Source/dsp/StateVariableFilter.cpp` with:

```cpp
#include "dsp/StateVariableFilter.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
} // namespace

void StateVariableFilter::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients();
    reset();
}

void StateVariableFilter::reset() noexcept
{
    for (auto& stage : stages_)
        stage = {};
}

void StateVariableFilter::setCutoffHz (float hz) noexcept
{
    cutoffHz_ = hz;
    updateCoefficients();
}

void StateVariableFilter::setResonance (float q) noexcept
{
    resonance_ = std::max (0.1f, q);
    updateCoefficients();
}

void StateVariableFilter::setNumStages (int stages) noexcept
{
    numStages_ = std::clamp (stages, 1, 2);
}

void StateVariableFilter::updateCoefficients() noexcept
{
    // Oltre 0.49 · sr la prewarp manda tan() all'infinito e il filtro diverge.
    const auto limit = (float) (sampleRate_ * 0.49);
    const auto cutoff = std::clamp (cutoffHz_, 10.0f, limit);

    g_ = std::tan (kPi * cutoff / (float) sampleRate_);
    twoR_ = 1.0f / resonance_;
    denominator_ = 1.0f + twoR_ * g_ + g_ * g_;
}

float StateVariableFilter::processStage (Stage& stage, float input) const noexcept
{
    const auto highPass = (input - (twoR_ + g_) * stage.s1 - stage.s2) / denominator_;
    const auto bandPass = g_ * highPass + stage.s1;
    stage.s1 = g_ * highPass + bandPass;
    const auto lowPass = g_ * bandPass + stage.s2;
    stage.s2 = g_ * bandPass + lowPass;

    switch (type_)
    {
        case Type::highPass: return highPass;
        case Type::bandPass: return bandPass;
        case Type::lowPass:  break;
    }

    return lowPass;
}

float StateVariableFilter::processSample (float input) noexcept
{
    auto out = processStage (stages_[0], input);

    if (numStages_ > 1)
        out = processStage (stages_[1], out);

    return out;
}
} // namespace dsp
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset macos-debug --target XerumTests && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests`
Expected: `ALL TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add Source/dsp/StateVariableFilter.h Source/dsp/StateVariableFilter.cpp Tests/EnvelopeFilterTests.cpp CMakeLists.txt
git commit -m "Implement the TPT state variable filter"
```

---

### Task 7: Wire the 22 parameters

**Files:**
- Create: `Source/engine/EngineParams.h`
- Modify: `Source/engine/SynthEngine.h/.cpp`, `Source/engine/VoiceManager.h/.cpp`, `Source/engine/SynthVoice.h/.cpp`
- Modify: `Source/plugin/PluginProcessor.h/.cpp`
- Modify: `Source/util/RealtimeHelpers.h` (remove the test tone)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 3–6, plus `params::denormalise` and `params::kParameterTable` from `Source/parameters/`.
- Produces: `engine::EngineParams` (a plain struct of denormalised values) and `engine::SynthEngine::setParams (const EngineParams&) noexcept`.

**Switch semantics** (from the spec, §7): `oscOn == false` silences the oscillator while voices keep running; `filtOn == false` bypasses the filter; `bypass == true` outputs silence and kills all voices — this is a synth, there is no input to pass through.

- [ ] **Step 1: Write the params struct**

Create `Source/engine/EngineParams.h`:

```cpp
#pragma once

#include "dsp/StateVariableFilter.h"

namespace engine
{
/**
 * I parametri già denormalizzati, riempiti una volta per blocco dal processore.
 * Le voci leggono questa struct: nessun atomico e nessuna mappatura per campione.
 */
struct EngineParams
{
    bool oscOn { true };
    float framePosition { 0.0f };    // 0..1 sul set di frame
    int octave { 0 };
    int semitones { 0 };
    float fineCents { 0.0f };
    float level { 1.0f };            // guadagno lineare

    bool filterOn { true };
    dsp::StateVariableFilter::Type filterType { dsp::StateVariableFilter::Type::lowPass };
    int filterStages { 2 };
    float cutoffHz { 1000.0f };
    float resonanceQ { 0.707f };
    float driveGain { 1.0f };        // guadagno lineare pre-filtro
    float keyTrack { 0.0f };         // 0..1

    float attackSeconds { 0.01f };
    float decaySeconds { 0.1f };
    float sustain { 1.0f };
    float releaseSeconds { 0.1f };
    float velocityAmount { 0.0f };   // 0..1: quanto la velocity scala il picco

    float pan { 0.0f };              // -1..1
    bool bypass { false };
};
} // namespace engine
```

- [ ] **Step 2: Apply the params in the voice**

In `Source/engine/SynthVoice.h`, add `void setParams (const EngineParams& p) noexcept;` and store `const EngineParams* params_ { nullptr };` — or, simpler and allocation-free, copy the fields the voice needs in `setParams`. Use the copy: a dangling pointer here is a crash in the audio thread.

In `Source/engine/SynthVoice.cpp`:

```cpp
void SynthVoice::setParams (const EngineParams& p) noexcept
{
    oscOn_ = p.oscOn;
    filterOn_ = p.filterOn;
    level_ = p.level;
    driveGain_ = p.driveGain;
    pan_ = p.pan;
    tuningSemitones_ = (float) (12 * p.octave + p.semitones) + p.fineCents * 0.01f;

    oscillator_.setFramePosition (p.framePosition);

    envelope_.setAttackSeconds (p.attackSeconds);
    envelope_.setDecaySeconds (p.decaySeconds);
    envelope_.setSustainLevel (p.sustain);
    envelope_.setReleaseSeconds (p.releaseSeconds);
    velocityAmount_ = p.velocityAmount;

    filter_.setType (p.filterType);
    filter_.setNumStages (p.filterStages);
    filter_.setResonance (p.resonanceQ);

    // Key tracking: il cutoff segue la nota. Calcolato qui, non per campione.
    baseCutoffHz_ = p.cutoffHz;
    keyTrack_ = p.keyTrack;
    updateCutoff();
}

void SynthVoice::updateCutoff() noexcept
{
    // A keyTrack 1 il cutoff raddoppia per ottava sopra il DO centrale.
    const auto offsetSemitones = keyTrack_ * (float) (midiNote_ - 60);
    filter_.setCutoffHz (baseCutoffHz_ * std::exp2 (offsetSemitones / 12.0f));
}
```

`updateCutoff()` calls `std::exp2` — it runs from `setParams` (once per block) and from `start()` (note-on), never per sample.

The tuning is applied in `start()`:

```cpp
void SynthVoice::start (int midiNote, float velocity) noexcept
{
    midiNote_ = midiNote;
    velocity_ = velocity;
    frequencyHz_ = midiNoteToHz (midiNote, tuningSemitones_);
    oscillator_.setFrequencyHz (frequencyHz_);
    updateCutoff();

    // envVel a 0 % = inviluppo sempre a piena ampiezza; a 100 % = proporzionale alla velocity.
    const auto peak = 1.0f - velocityAmount_ * (1.0f - velocity);
    envelope_.noteOn (peak);
    active_ = true;
}
```

with the helper updated:

```cpp
float midiNoteToHz (int note, float offsetSemitones) noexcept
{
    // std::pow gira solo a note-on, mai per campione.
    return 440.0f * std::pow (2.0f, ((float) note + offsetSemitones - 69.0f) / 12.0f);
}
```

and `render()`:

```cpp
void SynthVoice::render (float* outL, float* outR, int numSamples) noexcept
{
    if (! isActive() || outL == nullptr || outR == nullptr || numSamples <= 0)
        return;

    // Pan a potenza costante, calcolato per blocco.
    const auto angle = (pan_ * 0.5f + 0.5f) * 1.5707963f;
    const auto gainL = std::cos (angle) * level_;
    const auto gainR = std::sin (angle) * level_;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = oscOn_ ? oscillator_.getSample() : 0.0f;

        if (driveGain_ > 1.0f)
            sample = saturate (sample * driveGain_);

        if (filterOn_)
            sample = filter_.processSample (sample);

        sample *= envelope_.getNextSample();

        outL[i] += sample * gainL;
        outR[i] += sample * gainR;
    }

    if (! envelope_.isActive())
        active_ = false;
}
```

with, in the anonymous namespace of the file:

```cpp
/** Saturazione polinomiale: unitaria a fondo scala, liscia, senza tanh() per campione. */
float saturate (float x) noexcept
{
    const auto clamped = std::clamp (x, -1.0f, 1.0f);
    return 1.5f * (clamped - (clamped * clamped * clamped) / 3.0f);
}
```

Add the new members to `SynthVoice.h`: `bool oscOn_`, `bool filterOn_`, `float level_`, `float driveGain_`, `float pan_`, `float tuningSemitones_`, `float velocityAmount_`, `float baseCutoffHz_`, `float keyTrack_`, and the private `void updateCutoff() noexcept;`.

- [ ] **Step 3: Pass the params down through the engine**

`VoiceManager::setParams (const EngineParams&)` loops over the pool; `SynthEngine::setParams (const EngineParams& p)` stores a copy and forwards. In `SynthEngine::process`, honour bypass before anything else:

```cpp
    if (params_.bypass)
    {
        voices_.allSoundOff();
        buffer.clear();
        return;
    }
```

- [ ] **Step 4: Fill the struct in the processor**

In `Source/plugin/PluginProcessor.h`, replace the two individual `std::atomic<float>*` members with a small array plus an accessor, or keep named pointers — named pointers are clearer here. Add one per wired parameter:

```cpp
    std::atomic<float>* paramOscOn_ { nullptr };
    std::atomic<float>* paramWtIndex_ { nullptr };
    std::atomic<float>* paramWtpos_ { nullptr };
    std::atomic<float>* paramOct_ { nullptr };
    std::atomic<float>* paramSemi_ { nullptr };
    std::atomic<float>* paramFine_ { nullptr };
    std::atomic<float>* paramLevel_ { nullptr };
    std::atomic<float>* paramFiltOn_ { nullptr };
    std::atomic<float>* paramFtype_ { nullptr };
    std::atomic<float>* paramSlope_ { nullptr };
    std::atomic<float>* paramCutoff_ { nullptr };
    std::atomic<float>* paramRes_ { nullptr };
    std::atomic<float>* paramDrive_ { nullptr };
    std::atomic<float>* paramKeytrk_ { nullptr };
    std::atomic<float>* paramAtt_ { nullptr };
    std::atomic<float>* paramDec_ { nullptr };
    std::atomic<float>* paramSus_ { nullptr };
    std::atomic<float>* paramRel_ { nullptr };
    std::atomic<float>* paramEnvVel_ { nullptr };
    std::atomic<float>* paramVolume_ { nullptr };
    std::atomic<float>* paramPan_ { nullptr };
    std::atomic<float>* paramBypass_ { nullptr };

    engine::EngineParams collectParams() const noexcept;
```

In `PluginProcessor.cpp`, the constructor grabs them all (`paramOscOn_ = apvts_.getRawParameterValue ("oscOn");` and so on), and:

```cpp
namespace
{
/** Valore denormalizzato di un parametro, usando la spec di parameters.json. */
float realValue (const char* id, const std::atomic<float>* raw) noexcept
{
    if (raw == nullptr)
        return 0.0f;

    const auto& spec = params::specFor (id);
    return params::denormalise (spec, raw->load (std::memory_order_relaxed));
}
} // namespace

engine::EngineParams SerumStyleSynthAudioProcessor::collectParams() const noexcept
{
    engine::EngineParams p;

    p.oscOn = paramOscOn_ != nullptr && paramOscOn_->load (std::memory_order_relaxed) >= 0.5f;
    p.framePosition = paramWtpos_ != nullptr ? paramWtpos_->load (std::memory_order_relaxed) : 0.0f;
    p.octave = (int) realValue ("oct", paramOct_);
    p.semitones = (int) realValue ("semi", paramSemi_);
    p.fineCents = realValue ("fine", paramFine_);

    // `level` e `volume` hanno mappa Db: il valore grezzo è già il guadagno lineare.
    p.level = paramLevel_ != nullptr ? paramLevel_->load (std::memory_order_relaxed) : 1.0f;

    p.filterOn = paramFiltOn_ != nullptr && paramFiltOn_->load (std::memory_order_relaxed) >= 0.5f;
    p.filterType = filterTypeFromChoice (paramFtype_);
    p.filterStages = paramSlope_ != nullptr && paramSlope_->load (std::memory_order_relaxed) >= 0.5f ? 2 : 1;
    p.cutoffHz = realValue ("cutoff", paramCutoff_);

    // res 0..100 % → Q 0.707 (Butterworth) … 20 (autoscillante quasi).
    p.resonanceQ = juce::jmap (realValue ("res", paramRes_) * 0.01f, 0.707f, 20.0f);

    // drive 0..24 dB → guadagno lineare pre-saturazione.
    p.driveGain = juce::Decibels::decibelsToGain (realValue ("drive", paramDrive_));
    p.keyTrack = realValue ("keytrk", paramKeytrk_) * 0.01f;

    p.attackSeconds = realValue ("att", paramAtt_) * 0.001f;   // la mappa è in ms
    p.decaySeconds = realValue ("dec", paramDec_) * 0.001f;
    p.sustain = realValue ("sus", paramSus_) * 0.01f;
    p.releaseSeconds = realValue ("rel", paramRel_) * 0.001f;
    p.velocityAmount = realValue ("envVel", paramEnvVel_) * 0.01f;

    p.pan = realValue ("pan", paramPan_) * 0.02f;              // -50..50 → -1..1
    p.bypass = paramBypass_ != nullptr && paramBypass_->load (std::memory_order_relaxed) >= 0.5f;

    return p;
}
```

`params::specFor(id)` does not exist yet: add it to `Source/parameters/ParameterMapping.h` as a linear search over `params::kParameterTable` returning a `const Spec&`, with a `jassertfalse` and a reference to the first entry for an unknown id. The search runs once per parameter per block over 48 entries — measure before optimising; if it shows up, cache the `Spec*` next to each atomic pointer in the constructor.

`filterTypeFromChoice` maps the choice index 0/1/2 to `lowPass`/`highPass`/`bandPass`.

`processBlock` becomes:

```cpp
    const auto params = collectParams();
    engine_->setParams (params);

    if (volumeParam_ != nullptr)
    {
        const float v = volumeParam_->load (std::memory_order_relaxed);
        engine_->setMasterGainLinear (v <= 0.0f ? 0.0f : v * kHeadroomGain);
    }
```

- [ ] **Step 5: Rebuild the table when `wtIndex` changes**

In `PluginProcessor.h`, derive from `juce::AudioProcessorValueTreeState::Listener` and add `void parameterChanged (const juce::String& id, float value) override;` plus `juce::AsyncUpdater`-style deferral — the listener can fire on the audio thread, so it must not build anything itself:

```cpp
void SerumStyleSynthAudioProcessor::parameterChanged (const juce::String& id, float)
{
    // Può arrivare dal thread audio (automazione host): qui si marca soltanto.
    if (id == "wtIndex")
        wavetableDirty_.store (true, std::memory_order_release);
}
```

and a `juce::Timer` (or the existing message-thread path) on the processor that, on the message thread, checks the flag:

```cpp
void SerumStyleSynthAudioProcessor::timerCallback()
{
    if (! wavetableDirty_.exchange (false, std::memory_order_acquire))
        return;

    const auto index = wavetableIndexFromParam();

    if (index == lastWavetableIndex_)
        return;

    wavetables_.setActive (index);           // alloca: message thread
    lastWavetableIndex_ = index;
    engine_->setWavetable (wavetables_.active());
}
```

Start the timer at 25 Hz in the constructor, stop it in the destructor. Register the listener with `apvts_.addParameterListener ("wtIndex", this)` and remove it in the destructor.

- [ ] **Step 6: Delete the test tone**

In `Source/util/RealtimeHelpers.h`, remove `kTestToneHz` and `kEnableTestTone` and the sentence describing them. Grep to confirm nothing references them: `rg kEnableTestTone` must come back empty.

- [ ] **Step 7: Build and verify by ear**

Run:
```bash
cmake --preset macos-debug && cmake --build --preset macos-debug --target SerumStyleSynth_Standalone XerumTests \
  && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```
Then open the standalone and check, one at a time: Cutoff sweeps and is audible; Resonance peaks; LP/HP/BP differ; 12/24 differ; Attack/Release change the shape; Position morphs the timbre; Octave/Semi/Fine retune; Level and Volume change loudness; Pan moves the image; Bypass silences; the Oscillator and Filter toggles do what §7 says.

- [ ] **Step 8: Commit**

```bash
git add Source CMakeLists.txt
git commit -m "Connect the oscillator, filter, envelope and master parameters to the engine"
```

---

### Task 8: Robustness — parameter sweep and voice behaviour

**Files:**
- Create: `Tests/EngineTests.cpp`
- Modify: `CMakeLists.txt` (add the test source plus `Source/engine/*.cpp` to `XerumTests`)

**Interfaces:**
- Consumes: `engine::SynthEngine`, `engine::EngineParams`, `dsp::WavetableStore`.

- [ ] **Step 1: Write the failing tests**

Create `Tests/EngineTests.cpp`:

```cpp
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace
{
engine::EngineParams defaultParams()
{
    engine::EngineParams p;
    p.cutoffHz = 8000.0f;
    p.attackSeconds = 0.005f;
    p.decaySeconds = 0.1f;
    p.sustain = 0.8f;
    p.releaseSeconds = 0.05f;
    p.level = 0.8f;
    return p;
}

/** Rende `blocks` blocchi da 128 campioni e restituisce il picco assoluto. */
float renderPeak (engine::SynthEngine& synth, int blocks, juce::UnitTest& test)
{
    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;
    float peak = 0.0f;

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        synth.process (buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto v = buffer.getSample (ch, i);
                test.expect (std::isfinite (v), "campione non finito");
                peak = juce::jmax (peak, std::abs (v));
            }
    }

    return peak;
}
} // namespace

struct EngineTests final : juce::UnitTest
{
    EngineTests() : juce::UnitTest ("SynthEngine", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (0);

        beginTest ("una nota produce suono e il silenzio torna dopo il release");
        {
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = 48000.0;
            spec.maximumBlockSize = 128;
            spec.numChannels = 2;
            synth.prepare (spec);
            synth.setWavetable (store.active());
            synth.setParams (defaultParams());

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);

            expect (buffer.getMagnitude (0, 0, 128) > 0.01f, "la nota deve suonare");

            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buffer.clear();
            synth.process (buffer, midi);

            const auto tail = renderPeak (synth, 100, *this); // ~266 ms
            expect (tail < 1.0e-3f, "dopo il release deve tornare il silenzio, picco " + juce::String (tail));
        }

        beginTest ("sedici note insieme non fanno clipping né voci appese");
        {
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = 48000.0;
            spec.maximumBlockSize = 128;
            spec.numChannels = 2;
            synth.prepare (spec);
            synth.setWavetable (store.active());
            synth.setParams (defaultParams());

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            for (int note = 48; note < 64; ++note)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);

            buffer.clear();
            synth.process (buffer, midi);
            const auto peak = renderPeak (synth, 50, *this);
            expect (peak < 1.0f, "picco " + juce::String (peak) + ": la somma delle voci deve restare sotto 0 dBFS");

            midi.clear();
            for (int note = 48; note < 64; ++note)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            buffer.clear();
            synth.process (buffer, midi);

            expect (renderPeak (synth, 100, *this) < 1.0e-3f, "nessuna voce deve restare appesa");
        }

        beginTest ("tutti i parametri agli estremi: nessun NaN, nessuna esplosione");
        {
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = 44100.0;
            spec.maximumBlockSize = 128;
            spec.numChannels = 2;
            synth.prepare (spec);
            synth.setWavetable (store.active());

            for (int variant = 0; variant < 8; ++variant)
            {
                engine::EngineParams p = defaultParams();
                const bool high = (variant & 1) != 0;
                p.cutoffHz = high ? 20000.0f : 20.0f;
                p.resonanceQ = high ? 20.0f : 0.707f;
                p.driveGain = (variant & 2) != 0 ? juce::Decibels::decibelsToGain (24.0f) : 1.0f;
                p.filterStages = (variant & 4) != 0 ? 2 : 1;
                p.framePosition = high ? 1.0f : 0.0f;
                p.attackSeconds = 0.0f;
                p.releaseSeconds = 0.0f;
                p.level = 1.0f;
                synth.setParams (p);

                juce::AudioBuffer<float> buffer (2, 128);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 36 + variant * 8, 1.0f), 0);
                buffer.clear();
                synth.process (buffer, midi);

                const auto peak = renderPeak (synth, 40, *this);
                expect (peak < 4.0f, "variante " + juce::String (variant) + ": picco " + juce::String (peak));

                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOff (1, 36 + variant * 8), 0);
                buffer.clear();
                synth.process (buffer, midi);
                renderPeak (synth, 40, *this);
            }
        }

        beginTest ("bypass: silenzio assoluto e voci spente");
        {
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = 48000.0;
            spec.maximumBlockSize = 128;
            spec.numChannels = 2;
            synth.prepare (spec);
            synth.setWavetable (store.active());

            auto p = defaultParams();
            synth.setParams (p);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);

            p.bypass = true;
            synth.setParams (p);
            expectWithinAbsoluteError (renderPeak (synth, 10, *this), 0.0f, 0.0f);
        }
    }
};

static EngineTests engineTests;
```

- [ ] **Step 2: Run to verify the tests fail or pass honestly**

Run: `cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests`

Expected: the target fails to link until `Source/engine/*.cpp` are in the test sources. Once it links, **a failure here is a real bug in Tasks 4–7** — fix the engine, not the test. The most likely one is the 16-voice peak: if it exceeds 1.0, the per-voice headroom in `SynthVoice::render` is too generous.

- [ ] **Step 3: Commit**

```bash
git add Tests/EngineTests.cpp CMakeLists.txt
git commit -m "Cover the engine with sweep, polyphony and bypass tests"
```

---

### Task 9: Factory presets

**Files:**
- Create: `Source/parameters/presets.json`
- Modify: `scripts/gen-params.mjs`
- Create (generated): `Source/parameters/PresetTable.h`, `WebUI/src/synth/presets.generated.ts`
- Modify: `Source/bridge/StateChannel.h/.cpp`, `WebUI/src/juce/backend.ts`, `WebUI/src/juce/juce-backend.ts`, `WebUI/src/juce/fake-backend.ts`, `WebUI/src/synth/presets.ts`, `WebUI/src/synth/useSynth.ts`
- Modify: `WebUI/src/synth/presets.test.ts`

**Interfaces:**
- Consumes: the working engine from Tasks 4–7.
- Produces: `params::kPresetTable` (C++), `PRESETS` from `presets.generated.ts` (TS), and `Backend.loadPreset(index: number): Promise<void>`.

- [ ] **Step 1: Author the presets**

Create `Source/parameters/presets.json` with 12 entries. Values are **normalised 0..1**, exactly like the APVTS; omitted parameters keep the spec default. Author them by dialling each sound in the standalone and reading the values back — the numbers below are the starting skeleton, not the finished sounds:

```json
{
  "version": 1,
  "presets": [
    { "name": "Init", "cat": "User", "values": {} },
    { "name": "Sub Pulse", "cat": "Bass", "values": { "wtIndex": 1.0, "wtpos": 0.1, "cutoff": 0.35, "res": 0.15, "att": 0.0, "dec": 0.3, "sus": 0.6, "rel": 0.2 } },
    { "name": "Reese Wide", "cat": "Bass", "values": { "wtIndex": 0.2, "wtpos": 0.45, "cutoff": 0.4, "res": 0.35, "drive": 0.4 } },
    { "name": "Acid Line", "cat": "Bass", "values": { "wtIndex": 0.2, "cutoff": 0.3, "res": 0.7, "keytrk": 0.8, "dec": 0.25, "sus": 0.1 } },
    { "name": "Neon Lead", "cat": "Lead", "values": { "wtIndex": 0.4, "wtpos": 0.6, "cutoff": 0.7, "res": 0.25, "att": 0.05 } },
    { "name": "Solid Saw", "cat": "Lead", "values": { "wtIndex": 0.2, "wtpos": 0.0, "cutoff": 0.75, "res": 0.1 } },
    { "name": "Glass Pad", "cat": "Pad", "values": { "wtIndex": 0.8, "wtpos": 0.5, "cutoff": 0.55, "att": 0.45, "rel": 0.6, "sus": 0.8 } },
    { "name": "Dust Choir", "cat": "Pad", "values": { "wtIndex": 0.6, "wtpos": 0.35, "cutoff": 0.5, "att": 0.5, "rel": 0.65 } },
    { "name": "Velvet Keys", "cat": "Keys", "values": { "wtIndex": 0.8, "wtpos": 0.2, "cutoff": 0.6, "dec": 0.5, "sus": 0.35, "rel": 0.3 } },
    { "name": "Bell Tower", "cat": "Keys", "values": { "wtIndex": 0.8, "wtpos": 0.8, "cutoff": 0.8, "dec": 0.6, "sus": 0.1, "rel": 0.5 } },
    { "name": "Wire Pluck", "cat": "Pluck", "values": { "wtIndex": 0.4, "cutoff": 0.65, "res": 0.3, "att": 0.0, "dec": 0.2, "sus": 0.0, "rel": 0.15 } },
    { "name": "Cold Sweep", "cat": "FX", "values": { "wtIndex": 1.0, "wtpos": 0.9, "cutoff": 0.45, "res": 0.5, "att": 0.6, "rel": 0.7 } }
  ]
}
```

- [ ] **Step 2: Write the failing generator test**

Add to `WebUI/src/synth/presets.test.ts`:

```ts
import { PRESETS } from "./presets.generated";
import { PARAM_SPECS, type ParamId } from "./params.generated";

it("ogni preset generato ha un nome, una categoria e solo parametri esistenti", () => {
  expect(PRESETS.length).toBeGreaterThanOrEqual(12);
  for (const preset of PRESETS) {
    expect(preset.name).toBeTruthy();
    expect(preset.cat).toBeTruthy();
    for (const [id, value] of Object.entries(preset.values)) {
      expect(PARAM_SPECS[id as ParamId], `parametro sconosciuto: ${id}`).toBeDefined();
      expect(value).toBeGreaterThanOrEqual(0);
      expect(value).toBeLessThanOrEqual(1);
    }
  }
});
```

- [ ] **Step 3: Run to verify it fails**

Run: `cd WebUI && pnpm vitest run src/synth/presets.test.ts`
Expected: FAIL — `Cannot find module './presets.generated'`.

- [ ] **Step 4: Extend the generator**

In `scripts/gen-params.mjs`, after the existing parameter generation, add a `generatePresets(json)` that emits both files, and write them in `main()`. The C++ header:

```cpp
#pragma once

// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.
// Non modificare a mano.

namespace params
{
struct PresetValue
{
    const char* id;
    float value;
};

struct Preset
{
    const char* name;
    const char* category;
    const PresetValue* values;
    int numValues;
};

inline constexpr PresetValue kPreset0Values[] = { { "wtIndex", 1.0f } /* … */ };
// … una per preset …

inline constexpr Preset kPresetTable[] = {
    { "Init", "User", nullptr, 0 },
    // …
};

inline constexpr int kNumPresets = (int) (sizeof (kPresetTable) / sizeof (kPresetTable[0]));
} // namespace params
```

and the TypeScript:

```ts
// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.
// Non modificare a mano.
import type { ParamId } from "./params.generated";

export type Preset = { name: string; cat: string; values: Partial<Record<ParamId, number>> };

export const PRESETS: Preset[] = [
  { name: "Init", cat: "User", values: {} },
  // …
];
```

The generator must **fail loudly** on an unknown parameter id: `throw new Error(\`preset "${p.name}": parametro sconosciuto ${id}\`)`. A silently ignored typo is a preset that half-loads.

- [ ] **Step 5: Run the generator and the test**

Run: `node scripts/gen-params.mjs && cd WebUI && pnpm vitest run src/synth/presets.test.ts`
Expected: PASS.

- [ ] **Step 6: Point `presets.ts` at the generated list**

In `WebUI/src/synth/presets.ts`, delete the hardcoded `PRESETS` array and the local `Preset` type, and re-export from the generated module, keeping `CATEGORIES`, `filterPresets` and `step` as they are:

```ts
export type { Preset } from "./presets.generated";
export { PRESETS } from "./presets.generated";
```

Run the whole web suite to catch fallout: `cd WebUI && pnpm typecheck && pnpm test`.

- [ ] **Step 7: Add `loadPreset` to the bridge**

In `Source/bridge/StateChannel.cpp`, in `applyTo`, alongside the existing native functions:

```cpp
        .withNativeFunction ("loadPreset", [this] (const juce::Array<juce::var>& args,
                                                   juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            // Siamo sul message thread: scrivere i parametri e notificare è sicuro.
            if (args.size() >= 1)
                applyPreset ((int) args[0]);

            done (juce::var {});
        })
```

and the method:

```cpp
void StateChannel::applyPreset (int index)
{
    if (index < 0 || index >= params::kNumPresets)
        return;

    const auto& preset = params::kPresetTable[index];

    for (const auto& spec : params::kParameterTable)
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
```

Declare `void applyPreset (int index);` in `StateChannel.h` and include `"parameters/PresetTable.h"`.

- [ ] **Step 8: Call it from the UI**

Add to `Backend` in `WebUI/src/juce/backend.ts`:

```ts
  loadPreset(index: number): Promise<void>;
```

`JuceBackend`: `loadPreset: (index: number) => call("loadPreset")(index).then(() => {})`.
`FakeBackend`: apply the preset values to its own parameter store so the browser demo behaves the same.

In `WebUI/src/synth/useSynth.ts`, `pick` takes the index and calls the backend:

```ts
  const pick = useCallback(
    (pr: Preset) => {
      setPreset(pr);
      setDirty(false);
      setBrowse(false);
      void backend.loadPreset(PRESETS.indexOf(pr)).catch((e) => console.error("[preset] load fallito", e));
    },
    [backend],
  );
```

`useSynth` needs the backend: take it from the existing bridge hook (`useBridge()` / whatever `WebUI/src/juce/provider.tsx` exposes) rather than threading it through props.

- [ ] **Step 9: Run every suite**

Run:
```bash
cd WebUI && pnpm typecheck && pnpm test && cd ..
cmake --preset macos-debug && cmake --build --preset macos-debug --target XerumTests SerumStyleSynth_Standalone \
  && ./build/macos-debug/XerumTests_artefacts/Debug/XerumTests
```
Expected: all green. Then open the standalone, click through the preset browser and confirm each preset changes the sound and the knobs move.

- [ ] **Step 10: Commit**

```bash
git add Source/parameters Source/bridge scripts/gen-params.mjs WebUI/src
git commit -m "Generate factory presets on both sides and load them from C++"
```

---

### Task 10: Documentation

**Files:**
- Modify: `docs/architecture.md`, `docs/build.md`

- [ ] **Step 1: Update `docs/architecture.md`**

- In "Phase 1 behaviour", replace "Output is **silence**" with what the engine now does: wavetable oscillator with band-limited mipmaps, exponential ADSR, TPT SVF, 22 parameters connected, and the list of parameters still inert.
- In "Roadmap", mark phases 2 (`WavetableStore`) and 3 (`WavetableOscillator`) done.
- Add a "Wavetables" section: where the `.xwt` files come from, the format, how to regenerate them (`node scripts/fetch-wavetables.mjs`), the mipmap strategy, and the rule that built tables are never freed.
- Add a line to the plugin-window section recording the known divergence: `WaveDisplay` draws a procedural curve, not the real table.

- [ ] **Step 2: Update `docs/build.md`**

- Add the C++ test target: how to build and run it, and that it must pass before a commit that touches `Source/dsp` or `Source/engine`.
- Note that `Resources/wavetables/*.xwt` must exist before configuring — CMake fails with a pointer to the script otherwise.
- Extend the DAW checklist with: preset switching, a cutoff sweep with automation, and a save/reload that preserves the selected wavetable.

- [ ] **Step 3: Commit**

```bash
git add docs
git commit -m "Document the DSP engine, the wavetable pipeline and the test target"
```

---

## Self-Review

**Spec coverage**

| spec section | task |
|---|---|
| §4 wavetables from AKWF, `.xwt` format, CREDITS | 1 |
| §4.4 CMake binary data | 3 (step 5) |
| §5 `WavetableStore`, mipmap, never freed, lifecycle | 3 |
| §6.1 oscillator, level choice, dual interpolation | 4 |
| §6.2 ADSR | 5 |
| §6.3 SVF, slope, keytrack, drive | 6 (filter), 7 (keytrack, drive) |
| §6.4 smoothing | **gap — see below** |
| §6.5 chain and summing | 4, 7 |
| §7 parameters, EngineParams, switches, test tone removal | 7 |
| §8 presets | 9 |
| §9 edge cases (corrupt blob, missing table, high notes, high sample rate, resonance limit) | 2, 3, 6, 8 |
| §10 tests | 1, 2, 3, 4, 5, 6, 8 |
| §11 file map | File Structure |

**Gap found and closed:** §6.4 (`juce::SmoothedValue` on cutoff, wtpos, level, volume, pan) had no task. It belongs in Task 7, where those parameters first reach the voice. Add these steps to Task 7 between steps 4 and 5:

- [ ] **Task 7, Step 4b: Smooth the continuous parameters**

In `Source/engine/SynthVoice.h` add:

```cpp
    juce::SmoothedValue<float> smoothedCutoff_;
    juce::SmoothedValue<float> smoothedFramePosition_;
    juce::SmoothedValue<float> smoothedLevel_;
    juce::SmoothedValue<float> smoothedPan_;
```

In `prepare()`, give each a 20 ms ramp: `smoothedCutoff_.reset (sampleRate, 0.02);` and so on. `setParams` sets the targets (`smoothedCutoff_.setTargetValue (...)`) instead of writing the values directly, and `render()` reads `getNextValue()` per sample for level and pan, and once per block for cutoff and frame position (both feed `set…` calls that are too expensive per sample: `setCutoffHz` recomputes `tan`).

Concretely, in `render()`:

```cpp
    // Cutoff e posizione si aggiornano una volta per blocco: ricalcolano tan() e gli indici di frame.
    filter_.setCutoffHz (smoothedCutoff_.skip (numSamples));
    oscillator_.setFramePosition (smoothedFramePosition_.skip (numSamples));
```

and inside the sample loop, replace the fixed `gainL`/`gainR` with per-sample values derived from `smoothedLevel_.getNextValue()` and `smoothedPan_.getNextValue()`.

A test for it, appended to `Tests/EngineTests.cpp`:

```cpp
        beginTest ("un salto di cutoff non produce un gradino nel segnale");
        {
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = 48000.0;
            spec.maximumBlockSize = 128;
            spec.numChannels = 2;
            synth.prepare (spec);
            synth.setWavetable (store.active());

            auto p = defaultParams();
            p.cutoffHz = 200.0f;
            synth.setParams (p);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);
            renderPeak (synth, 20, *this);

            p.cutoffHz = 12000.0f;
            synth.setParams (p);

            buffer.clear();
            midi.clear();
            synth.process (buffer, midi);

            float maxJump = 0.0f;
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxJump = juce::jmax (maxJump, std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

            expect (maxJump < 0.5f, "salto massimo fra campioni " + juce::String (maxJump));
        }
```

**Placeholder scan:** no "TBD"/"TODO"/"similar to task N"/"add error handling" remain. Two places deliberately defer a judgement to the implementer and say so explicitly: the preset values in Task 9 Step 1 (a skeleton to be dialled in by ear, which is what the spec asks for) and the `params::specFor` linear search in Task 7 Step 4 (measure before optimising).

**Type consistency:** `setWavetable` is used with the same name in `SynthVoice`, `VoiceManager`, `SynthEngine` and `PluginProcessor`. `setFramePosition` is the same name in `WavetableOscillator`, `SynthVoice`, `VoiceManager` and `SynthEngine`. `dsp::MipTable::kMaxLevel` is the single source for the level count in `levelForFrequency`, the builder and the tests. `dsp::BlobView::frame(int)` is used identically in the tests and the builder. `EngineParams` field names match between `EngineParams.h`, `SynthVoice::setParams` and `collectParams`.
