# Serum Wavetable Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Portare in Xerum le sette wavetable contenute nei preset Serum del pack *Retro Synthwave Pack 2*, e far disegnare a `WaveDisplay` la tavola davvero selezionata invece della forma sintetica di oggi.

**Architecture:** Tutto l'import è offline, in Node. Due script nuovi: uno estrae le tavole dai `.fxp` e scrive `.xwt`, l'altro legge i `.xwt` e genera le anteprime per la WebUI. Il plugin non guadagna nessun I/O su file: le tavole si compilano nel binario come le sei attuali, raccolte dal `file(GLOB CONFIGURE_DEPENDS)` già presente. Prima di allargare `wtIndex` da 6 a 13 opzioni, `presets.json` passa a nominare i propri choice invece di normalizzarli, altrimenti ogni valore già scritto punterebbe a un'altra tavola.

**Tech Stack:** Node 24 (ESM, `node:zlib`, `node:test`), C++23 + JUCE 9, CMake, React 19 + Vitest, pnpm.

**Spec:** `docs/superpowers/specs/2026-09-20-serum-wavetable-import-design.md`

## Global Constraints

- **Sorgente del pack:** `/Users/andrea/Downloads/RetroSynthwavePack2` (171 file `.fxp`). Non è nel repo e non ci va.
- **Formato `.xwt`:** `"XWT1"` + `frames` u32 LE + `frameSize` u32 LE + `frames * frameSize` float32 LE. `parseXwt` rifiuta `frames > 256`, `frameSize > 4096` e `frameSize` non potenza di due.
- **Tavole Xerum:** sempre **64 frame × 2048 campioni**. Non cambiare questi due numeri.
- **Soglia di import:** si scartano gli stream sotto **8 frame** sorgente (sono onde singole, non tavole di morph).
- **Le sette tavole** (sha1 dello stream, frame sorgente, slug, etichetta):
  | sha1 | frame | slug | etichetta |
  |---|---|---|---|
  | `56780dc5` | 256 | `retro-racing` | Retro Racing |
  | `40bbc013` | 256 | `retro-spindizzy` | Retro Spindizzy |
  | `d15054da` | 256 | `retro-ggsisters` | Retro GG Sisters |
  | `8490d094` | 256 | `retro-commando` | Retro Commando |
  | `b5167ba8` | 278 | `retro-uridium-pad` | Retro Uridium Pad |
  | `fa38ca0c` | 22 | `retro-leaderboard` | Retro Leaderboard |
  | `34b742bb` | 22 | `retro-uridium` | Retro Uridium |
- **Niente riallineamento di fase, niente equalizzazione dell'RMS per frame.** Servono alle AKWF, non a una tavola Serum già coerente lungo il morph. Si applica solo `normaliseTable`.
- **L'ordine è vincolato:** le opzioni di `wtIndex` in `Source/parameters/parameters.json` e le voci di `kWavetableFiles` in `Source/dsp/WavetableStore.h` devono restare allineate indice per indice; lo `static_assert` in `Source/engine/ParamCollect.h:42-45` lo verifica a compile time.
- **Licenza:** le tavole vengono da un pack commerciale di terzi (autore `zak235`). `Resources/wavetables/CREDITS.md` registra la provenienza. Non sono ridistribuibili in un Xerum pubblicato senza permesso: non aggiungere righe che lo diano per scontato.
- **Lingua:** commenti e messaggi d'errore in italiano, come il resto del repo. Messaggi di commit in inglese.

---

## File Structure

| file | responsabilità |
|---|---|
| `scripts/serum-fxp.mjs` | **nuovo** — leggere un `.fxp` Serum: header, chunk, split degli stream zlib, stream → frame. Nessun I/O su disco, nessuna CLI. |
| `scripts/serum-fxp.test.mjs` | **nuovo** — test di `serum-fxp.mjs` su buffer costruiti in memoria. |
| `scripts/import-serum-wavetables.mjs` | **nuovo** — CLI: cartella di `.fxp` → `.xwt` in `Resources/wavetables` + righe in `CREDITS.md`. |
| `scripts/build-wavetable-previews.mjs` | **nuovo** — CLI: tutti i `.xwt` → `WebUI/src/synth/wavetables.generated.ts`. |
| `scripts/gen-params.mjs` | choice dei preset risolti per nome; errore sui nomi ignoti. |
| `Source/parameters/presets.json` | i choice diventano stringhe. |
| `Source/parameters/parameters.json` | 7 opzioni in coda a `wtIndex`. |
| `Source/dsp/WavetableStore.h` | 7 voci in coda a `kWavetableFiles`. |
| `Resources/wavetables/*.xwt` | 7 file nuovi, generati. |
| `Resources/wavetables/CREDITS.md` | provenienza e nota di licenza. |
| `Tests/WavetableTests.cpp` | un test che scorre `kWavetableFiles` invece delle sei tavole note. |
| `Tests/PresetValueTests.cpp` | un test che verifica che ogni choice di ogni preset stia nel numero di opzioni. |
| `WebUI/src/synth/wavetables.generated.ts` | **nuovo**, generato — anteprime 9 × 256 per tavola. |
| `WebUI/src/synth/curves.ts` + `curves.test.ts` | `sampleWave` esce, entra `tableSample`; `spectrum` prende la tavola. |
| `WebUI/src/synth/ui/WaveDisplay.tsx` | disegna la tavola vera. |
| `WebUI/src/synth/presets.ts` | `presetWave` restituisce anche l'indice di tavola. |
| `WebUI/src/synth/ui/PresetOverlay.tsx` | miniatura con la tavola del preset. |
| `WebUI/package.json` | `test:scripts` su tutti i `*.test.mjs`. |
| `docs/architecture.md` | sezione wavetable. |

---

## Task 1: Lettore di preset Serum

**Files:**
- Create: `scripts/serum-fxp.mjs`
- Create: `scripts/serum-fxp.test.mjs`
- Modify: `WebUI/package.json:16`

**Interfaces:**
- Consumes: niente (primo task).
- Produces:
  - `parseFxp(buffer: Buffer): { name: string, chunk: Buffer }` — lancia su header non valido.
  - `splitZlibStreams(chunk: Buffer): Buffer[]` — gli stream inflati, in ordine.
  - `framesFromStream(stream: Buffer, frameSize: number): Float32Array[] | null` — `null` se lo stream è vuoto o la lunghezza non è multiplo di `frameSize * 4`.
  - `SERUM_PLUGIN_ID = "XfsX"`, `SERUM_FRAME_SIZE = 2048`.

- [ ] **Step 1: Scrivi il test che fallisce**

Crea `scripts/serum-fxp.test.mjs`:

```js
import test from "node:test";
import assert from "node:assert/strict";
import { deflateSync } from "node:zlib";
import { parseFxp, splitZlibStreams, framesFromStream, SERUM_FRAME_SIZE } from "./serum-fxp.mjs";

/** Un .fxp FPCh come lo scrive Serum: header big-endian, poi il chunk opaco. */
function makeFxp({ pluginId = "XfsX", name = "TEST", chunk = Buffer.alloc(0), declaredLen = null } = {}) {
  const head = Buffer.alloc(60);
  head.write("CcnK", 0, "ascii");
  head.writeUInt32BE(52 + chunk.length, 4);
  head.write("FPCh", 8, "ascii");
  head.writeUInt32BE(1, 12);
  head.write(pluginId, 16, "ascii");
  head.writeUInt32BE(1, 20);
  head.writeUInt32BE(1, 24);
  head.write(name, 28, "ascii");
  head.writeUInt32BE(declaredLen ?? chunk.length, 56);
  return Buffer.concat([head, chunk]);
}

/** Uno stream di `frames` frame: ogni campione vale l'indice del frame. */
function rampStream(frames, frameSize = SERUM_FRAME_SIZE) {
  const data = new Float32Array(frames * frameSize);
  for (let f = 0; f < frames; f++) data.fill(f, f * frameSize, (f + 1) * frameSize);
  return Buffer.from(data.buffer);
}

test("parseFxp legge nome e chunk di un fxp Serum", () => {
  const chunk = Buffer.from("payload");
  const { name, chunk: got } = parseFxp(makeFxp({ name: "BS-airwolf", chunk }));
  assert.equal(name, "BS-airwolf");
  assert.deepEqual(got, chunk);
});

test("parseFxp rifiuta un plugin che non e' Serum", () => {
  assert.throws(() => parseFxp(makeFxp({ pluginId: "Xfer" })), /XfsX/);
});

test("parseFxp rifiuta un magic sbagliato", () => {
  const bad = makeFxp({});
  bad.write("XXXX", 0, "ascii");
  assert.throws(() => parseFxp(bad), /CcnK/);
});

test("parseFxp rifiuta un chunk piu' lungo del file", () => {
  assert.throws(() => parseFxp(makeFxp({ chunk: Buffer.from("ab"), declaredLen: 9999 })), /troncato/);
});

test("splitZlibStreams separa gli stream concatenati e ignora la coda", () => {
  const a = deflateSync(Buffer.from("stato"));
  const b = deflateSync(rampStream(2));
  const chunk = Buffer.concat([a, b, Buffer.from([0x15, 0x0b, 0x00, 0x00])]);
  const streams = splitZlibStreams(chunk);
  assert.equal(streams.length, 2);
  assert.equal(streams[0].toString(), "stato");
  assert.equal(streams[1].length, 2 * SERUM_FRAME_SIZE * 4);
});

test("framesFromStream ricava i frame e ne conserva i valori", () => {
  const frames = framesFromStream(rampStream(3), SERUM_FRAME_SIZE);
  assert.equal(frames.length, 3);
  assert.equal(frames[0][0], 0);
  assert.equal(frames[2][SERUM_FRAME_SIZE - 1], 2);
});

test("framesFromStream restituisce null su stream vuoto o non allineato", () => {
  assert.equal(framesFromStream(Buffer.alloc(0), SERUM_FRAME_SIZE), null);
  assert.equal(framesFromStream(Buffer.alloc(13), SERUM_FRAME_SIZE), null);
});
```

- [ ] **Step 2: Lancia il test e verifica che fallisca**

```bash
node --test scripts/serum-fxp.test.mjs
```

Atteso: FAIL con `Cannot find module .../scripts/serum-fxp.mjs`.

- [ ] **Step 3: Scrivi l'implementazione minima**

Crea `scripts/serum-fxp.mjs`:

```js
// Lettura dei preset .fxp di Xfer Serum: solo il formato, nessun I/O e nessuna CLI.
// Un .fxp e' un FPCh VST2 (header big-endian) il cui chunk opaco contiene uno o piu'
// stream zlib concatenati: il primo e' lo stato del synth, quelli dopo — quando non
// sono vuoti — sono le wavetable custom in float32 grezzi.
import { inflateSync } from "node:zlib";

export const SERUM_PLUGIN_ID = "XfsX";
export const SERUM_FRAME_SIZE = 2048;

/** Header FPCh: magic 0, fxMagic 8, fxID 16, nome 28..56, lunghezza del chunk 56. */
export function parseFxp(buffer) {
  if (buffer.length < 60) throw new Error("fxp troncato: meno di 60 byte di header");
  if (buffer.toString("ascii", 0, 4) !== "CcnK") throw new Error('non e\' un fxp: manca il magic "CcnK"');
  if (buffer.toString("ascii", 8, 12) !== "FPCh") throw new Error('fxp senza chunk opaco: atteso "FPCh"');

  const pluginId = buffer.toString("ascii", 16, 20);
  if (pluginId !== SERUM_PLUGIN_ID) throw new Error(`plugin "${pluginId}", atteso "${SERUM_PLUGIN_ID}" (Serum)`);

  const chunkLength = buffer.readUInt32BE(56);
  if (chunkLength <= 0 || 60 + chunkLength > buffer.length)
    throw new Error(`chunk dichiarato ${chunkLength} byte ma il file e' troncato (${buffer.length - 60} disponibili)`);

  return {
    name: buffer.toString("ascii", 28, 56).replace(/\0[\s\S]*$/, ""),
    chunk: buffer.subarray(60, 60 + chunkLength),
  };
}

/**
 * Inflate ripetuto finche' i byte successivi aprono uno stream zlib (`0x78`).
 * `inflateSync(..., { info: true })` dice quanti byte di ingresso ha consumato: e' l'unico
 * modo di trovare l'inizio dello stream dopo. La coda di 4 byte che Serum lascia in fondo
 * non e' uno stream e viene ignorata.
 */
export function splitZlibStreams(chunk) {
  const streams = [];
  let rest = chunk;

  while (rest.length >= 2 && rest[0] === 0x78) {
    const { buffer, engine } = inflateSync(rest, { info: true });
    if (engine.bytesWritten <= 0) break;
    streams.push(buffer);
    rest = rest.subarray(engine.bytesWritten);
  }

  return streams;
}

/** Lo stream come frame da `frameSize` campioni, o null se non e' una tavola. */
export function framesFromStream(stream, frameSize) {
  const bytesPerFrame = frameSize * 4;
  if (stream.length === 0 || stream.length % bytesPerFrame !== 0) return null;

  const frames = [];
  for (let offset = 0; offset < stream.length; offset += bytesPerFrame) {
    const frame = new Float32Array(frameSize);
    for (let i = 0; i < frameSize; i++) frame[i] = stream.readFloatLE(offset + i * 4);
    frames.push(frame);
  }
  return frames;
}
```

- [ ] **Step 4: Lancia il test e verifica che passi**

```bash
node --test scripts/serum-fxp.test.mjs
```

Atteso: PASS, 7 test.

- [ ] **Step 5: Fai girare i test degli script tutti insieme**

In `WebUI/package.json`, riga 16, sostituisci

```json
    "test:scripts": "node --test ../scripts/wavetable-dsp.test.mjs",
```

con

```json
    "test:scripts": "node --test ../scripts/*.test.mjs",
```

Poi:

```bash
cd WebUI && pnpm test:scripts
```

Atteso: PASS, i test di `wavetable-dsp` più i 7 nuovi.

- [ ] **Step 6: Commit**

```bash
git add scripts/serum-fxp.mjs scripts/serum-fxp.test.mjs WebUI/package.json
git commit -m "Read Serum .fxp presets well enough to find their wavetables

A .fxp is a VST2 FPCh whose opaque chunk holds one or more concatenated
zlib streams. The first is Serum's own state; any stream after it, when
it is not empty, is a custom wavetable as raw float32. Finding where one
stream ends needs inflateSync's info mode, which reports how much input
it consumed.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: CLI di import

**Files:**
- Create: `scripts/import-serum-wavetables.mjs`
- Test: `scripts/serum-fxp.test.mjs` (aggiunta di un caso su `collectWavetables`)

**Interfaces:**
- Consumes: `parseFxp`, `splitZlibStreams`, `framesFromStream`, `SERUM_FRAME_SIZE` da `./serum-fxp.mjs`; `selectFrames`, `normaliseTable`, `encodeXwt` da `./wavetable-dsp.mjs`.
- Produces: `collectWavetables(files: {name: string, buffer: Buffer}[], minFrames: number): Map<string, { sha1: string, frames: Float32Array[], sources: string[] }>` — chiave sha1 abbreviata, esportata per il test.

**Nota sul riuso:** `selectFrames(waves, count)` in `scripts/wavetable-dsp.mjs:114` fa già esattamente la riduzione lungo l'asse dei frame che serve (`pos = (N-1) * i / (count-1)`, blend lineare fra i due frame adiacenti). Non scrivere una funzione nuova. `normaliseTable(frames)` a riga 93 toglie la continua e applica un solo fattore di scala globale: è il punto 6 della spec.

- [ ] **Step 1: Scrivi il test che fallisce**

Aggiungi in fondo a `scripts/serum-fxp.test.mjs`:

```js
import { collectWavetables } from "./import-serum-wavetables.mjs";

test("collectWavetables deduplica per sha1 e scarta sotto la soglia", () => {
  const big = deflateSync(rampStream(16));
  const small = deflateSync(rampStream(4));
  const state = deflateSync(Buffer.alloc(64));
  const files = [
    { name: "A.fxp", buffer: makeFxp({ name: "A", chunk: Buffer.concat([state, big]) }) },
    { name: "B.fxp", buffer: makeFxp({ name: "B", chunk: Buffer.concat([state, big]) }) },
    { name: "C.fxp", buffer: makeFxp({ name: "C", chunk: Buffer.concat([state, small]) }) },
    { name: "D.fxp", buffer: makeFxp({ name: "D", chunk: state }) },
  ];
  const found = collectWavetables(files, 8);
  assert.equal(found.size, 1);
  const only = [...found.values()][0];
  assert.equal(only.frames.length, 16);
  assert.deepEqual(only.sources, ["A.fxp", "B.fxp"]);
});
```

- [ ] **Step 2: Lancia il test e verifica che fallisca**

```bash
node --test scripts/serum-fxp.test.mjs
```

Atteso: FAIL con `Cannot find module .../scripts/import-serum-wavetables.mjs`.

- [ ] **Step 3: Scrivi l'implementazione**

Crea `scripts/import-serum-wavetables.mjs`:

```js
#!/usr/bin/env node
// Estrae le wavetable custom incorporate nei preset .fxp di Serum e le scrive come .xwt.
// Uso: node scripts/import-serum-wavetables.mjs <cartella-fxp> [--out Resources/wavetables]
//                                               [--min-frames 8] [--dry-run]
//
// Non tocca i parametri del preset: lo stato Serum e' un altro strumento, con altri
// oscillatori e un'altra matrice, e tradurlo non e' questo lavoro.
import { createHash } from "node:crypto";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { framesFromStream, parseFxp, splitZlibStreams, SERUM_FRAME_SIZE } from "./serum-fxp.mjs";
import { encodeXwt, normaliseTable, selectFrames } from "./wavetable-dsp.mjs";

/** I 64 frame che Xerum si aspetta in ogni .xwt. */
const TARGET_FRAMES = 64;

/**
 * Le sette tavole del pack, riconosciute per sha1 dello stream grezzo. Lo slug e'
 * anche il `value` dell'opzione di wtIndex e il nome del file .xwt: cambiarlo qui
 * senza cambiarlo in parameters.json fa fallire lo static_assert in ParamCollect.h.
 */
export const KNOWN_TABLES = {
  "56780dc5": "retro-racing",
  "40bbc013": "retro-spindizzy",
  d15054da: "retro-ggsisters",
  "8490d094": "retro-commando",
  b5167ba8: "retro-uridium-pad",
  fa38ca0c: "retro-leaderboard",
  "34b742bb": "retro-uridium",
};

/**
 * Raccoglie le tavole distinte fra i file dati. Lo stream 0 e' sempre lo stato Serum
 * e non viene guardato; quelli dopo sono tavole quando la lunghezza torna e i frame
 * bastano. Sotto `minFrames` si tratta di onde singole, non di morph: si scartano.
 */
export function collectWavetables(files, minFrames) {
  const found = new Map();

  for (const { name, buffer } of files) {
    const { chunk } = parseFxp(buffer);

    for (const stream of splitZlibStreams(chunk).slice(1)) {
      const frames = framesFromStream(stream, SERUM_FRAME_SIZE);
      if (frames === null || frames.length < minFrames) continue;

      const sha1 = createHash("sha1").update(stream).digest("hex").slice(0, 8);
      const entry = found.get(sha1);
      if (entry) entry.sources.push(name);
      else found.set(sha1, { sha1, frames, sources: [name] });
    }
  }

  return found;
}

function parseArgs(argv) {
  const positional = [];
  const opts = { out: "Resources/wavetables", minFrames: 8, dryRun: false };

  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--out") opts.out = argv[++i];
    else if (argv[i] === "--min-frames") opts.minFrames = Number(argv[++i]);
    else if (argv[i] === "--dry-run") opts.dryRun = true;
    else positional.push(argv[i]);
  }

  if (positional.length !== 1) throw new Error("uso: import-serum-wavetables.mjs <cartella-fxp> [--out DIR] [--min-frames N] [--dry-run]");
  if (!Number.isFinite(opts.minFrames) || opts.minFrames < 1) throw new Error(`--min-frames non valido: ${opts.minFrames}`);
  return { dir: positional[0], ...opts };
}

const peakOf = (frames) => frames.reduce((p, f) => f.reduce((q, s) => Math.max(q, Math.abs(s)), p), 0);
const rmsOf = (frames) => {
  let sum = 0;
  let n = 0;
  for (const f of frames) for (const s of f) { sum += s * s; n++; }
  return Math.sqrt(sum / n);
};

function main(argv) {
  const { dir, out, minFrames, dryRun } = parseArgs(argv);
  const root = resolve(dirname_(), "..");
  const outDir = resolve(root, out);

  const names = readdirSync(dir).filter((n) => n.toLowerCase().endsWith(".fxp")).sort();
  if (names.length === 0) throw new Error(`nessun .fxp in ${dir}`);

  const files = names.map((n) => ({ name: n, buffer: readFileSync(resolve(dir, n)) }));
  const found = collectWavetables(files, minFrames);
  console.log(`${names.length} preset letti, ${found.size} tavole distinte sopra i ${minFrames} frame`);

  const written = [];
  for (const { sha1, frames, sources } of found.values()) {
    const slug = KNOWN_TABLES[sha1];
    if (!slug) {
      console.warn(`  ${sha1}: ${frames.length} frame, nessuno slug noto — saltata (da ${sources[0]})`);
      continue;
    }

    const before = { frames: frames.length, peak: peakOf(frames), rms: rmsOf(frames) };
    const reduced = normaliseTable(selectFrames(frames, TARGET_FRAMES));
    const after = { frames: reduced.length, peak: peakOf(reduced), rms: rmsOf(reduced) };

    console.log(
      `  ${slug} (${sha1}): ${before.frames} → ${after.frames} frame, ` +
        `picco ${before.peak.toFixed(3)} → ${after.peak.toFixed(3)}, ` +
        `rms ${before.rms.toFixed(3)} → ${after.rms.toFixed(3)}, ` +
        `da ${sources.length} preset (${sources[0]})`,
    );

    if (!dryRun) writeFileSync(resolve(outDir, `${slug}.xwt`), encodeXwt(reduced, SERUM_FRAME_SIZE));
    written.push({ slug, sha1, sources, sourceFrames: before.frames });
  }

  const missing = Object.entries(KNOWN_TABLES).filter(([sha1]) => !found.has(sha1));
  if (missing.length > 0) throw new Error(`tavole attese e non trovate: ${missing.map(([s, n]) => `${n} (${s})`).join(", ")}`);

  console.log(dryRun ? "--dry-run: nessun file scritto" : `${written.length} file .xwt scritti in ${outDir}`);
  console.log("\nRighe per CREDITS.md:\n");
  for (const w of written)
    console.log(`| \`${w.slug}\` | ${w.sourceFrames} frame | ${w.sources.length} preset, es. \`${basename(w.sources[0], ".fxp")}\` |`);
}

function dirname_() {
  return resolve(fileURLToPath(import.meta.url), "..");
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`import-serum-wavetables: ${error.message}`);
    process.exit(1);
  }
}
```

- [ ] **Step 4: Lancia il test e verifica che passi**

```bash
node --test scripts/serum-fxp.test.mjs
```

Atteso: PASS, 8 test.

- [ ] **Step 5: Prova a vuoto sul pack vero**

```bash
node scripts/import-serum-wavetables.mjs /Users/andrea/Downloads/RetroSynthwavePack2 --dry-run
```

Atteso: `171 preset letti, 7 tavole distinte sopra i 8 frame`, sette righe con gli slug della tabella nei Global Constraints, e `--dry-run: nessun file scritto`. Se una tavola attesa manca, lo script esce con errore: fermati e riporta, non aggirare.

- [ ] **Step 6: Commit**

```bash
git add scripts/import-serum-wavetables.mjs scripts/serum-fxp.test.mjs
git commit -m "Turn the embedded Serum tables into .xwt files

Seven distinct morph tables hide inside the pack's 171 presets, most of
them repeated across several patches, so the importer keys them by the
sha1 of the raw stream and writes each one once. Streams under eight
frames are single cycles rather than tables and are dropped.

Reducing 256 or 278 source frames to Xerum's 64 is what selectFrames
already does for the AKWF families, and normaliseTable supplies the one
global scale factor. Nothing else is applied: a Serum table is already
coherent along the morph axis, so the phase realignment those tables
need would only rotate this one for nothing.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: I preset nominano i propri choice

Questo task viene **prima** di aggiungere le tavole: allargare `wtIndex` da 6 a 13 opzioni ripunterebbe ogni valore normalizzato già scritto (`"wtIndex": 1.0` oggi è `pwm`, dopo sarebbe `retro-uridium`).

**Files:**
- Modify: `scripts/gen-params.mjs:211-222` (`generatePresets`)
- Modify: `Source/parameters/presets.json` (tutti i preset, più `_note`)
- Create: `scripts/gen-params.test.mjs`
- Modify: `Source/parameters/PresetTable.h`, `WebUI/src/synth/presets.generated.ts` (rigenerati, non a mano)

**Interfaces:**
- Consumes: niente dai task precedenti.
- Produces: `generatePresets(presetsJson, paramsJson)` accetta per i parametri `kind: "choice"` **solo stringhe** (il `value` dell'opzione) e per gli altri solo numeri 0..1. L'output (`PresetTable.h`, `presets.generated.ts`) non cambia forma: resta `{ const char* id; float value; }` e `Partial<Record<ParamId, number>>`.

- [ ] **Step 1: Scrivi il test che fallisce**

Crea `scripts/gen-params.test.mjs`:

```js
import test from "node:test";
import assert from "node:assert/strict";
import { generatePresets } from "./gen-params.mjs";

const params = {
  params: [
    { id: "wtIndex", name: "Wavetable", group: "osc", kind: "choice", slot: false, default: 0,
      options: [{ value: "basic", label: "Basic" }, { value: "saws", label: "Saws" }, { value: "grit", label: "Grit" }] },
    { id: "cutoff", name: "Cutoff", group: "filter", kind: "float", slot: true, map: { type: "log", min: 20, max: 20000 }, default: 0.5 },
  ],
};

test("un choice nominato diventa il normalizzato del suo indice", () => {
  const { header } = generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "grit", cutoff: 0.5 } }] }, params);
  assert.match(header, /\{ "wtIndex", 1\.0f \}/);
  assert.match(header, /\{ "cutoff", 0\.5f \}/);
});

test("la prima opzione di un choice vale 0", () => {
  const { header } = generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "basic" } }] }, params);
  assert.match(header, /\{ "wtIndex", 0\.0f \}/);
});

test("un nome di opzione sconosciuto fa fallire la generazione", () => {
  assert.throws(
    () => generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "retro-nope" } }] }, params),
    /retro-nope/,
  );
});

test("un choice ancora numerico fa fallire la generazione", () => {
  assert.throws(
    () => generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: 1 } }] }, params),
    /wtIndex/,
  );
});

test("un float non puo' essere una stringa", () => {
  assert.throws(
    () => generatePresets({ presets: [{ name: "P", cat: "Bass", values: { cutoff: "mezzo" } }] }, params),
    /cutoff/,
  );
});

test("la TypeScript generata porta il numero, non il nome", () => {
  const { ts } = generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "saws" } }] }, params);
  assert.match(ts, /"wtIndex":0\.5/);
});
```

- [ ] **Step 2: Lancia il test e verifica che fallisca**

```bash
node --test scripts/gen-params.test.mjs
```

Atteso: FAIL — il primo test riporta che `header` non contiene `{ "wtIndex", 1.0f }` (oggi il valore viene copiato tale e quale e la stringa fa esplodere `f()`), e il test "choice ancora numerico" fallisce perché non viene lanciato niente.

- [ ] **Step 3: Scrivi l'implementazione**

In `scripts/gen-params.mjs`, sostituisci il corpo di `generatePresets` dalla riga `const knownIds = new Set(...)` fino alla chiusura del ciclo di validazione (righe 212-222) con:

```js
export function generatePresets(presetsJson, paramsJson) {
  const specById = new Map(paramsJson.params.map((p) => [p.id, p]));
  const presets = presetsJson.presets;

  // I choice si scrivono per nome dell'opzione, non per valore normalizzato: `indice /
  // (numOpzioni - 1)` cambia sotto i piedi ogni volta che si aggiunge un'opzione, e un
  // preset scritto ieri punterebbe in silenzio a un'altra wavetable. Il nome no.
  const resolve_ = (presetName, id, value) => {
    const spec = specById.get(id);
    if (!spec) throw new Error(`preset "${presetName}": parametro sconosciuto ${id}`);

    if (spec.kind === "choice") {
      if (typeof value !== "string")
        throw new Error(`preset "${presetName}": ${id} e' un choice e va scritto col nome dell'opzione, non con ${JSON.stringify(value)}`);
      const index = spec.options.findIndex((o) => o.value === value);
      if (index < 0)
        throw new Error(`preset "${presetName}": ${id} non ha l'opzione "${value}" (ci sono: ${spec.options.map((o) => o.value).join(", ")})`);
      return spec.options.length === 1 ? 0 : index / (spec.options.length - 1);
    }

    if (typeof value !== "number" || value < 0 || value > 1)
      throw new Error(`preset "${presetName}": valore fuori range 0..1 per ${id}: ${JSON.stringify(value)}`);
    return value;
  };

  const resolved = presets.map((p) => ({
    ...p,
    values: Object.fromEntries(Object.entries(p.values ?? {}).map(([id, v]) => [id, resolve_(p.name, id, v)])),
  }));
```

Poi, nel resto della funzione, sostituisci ogni uso di `presets` con `resolved` — sono i tre `...presets.map(...)` dentro `header` e il `...presets.map(...)` dentro `ts`, più `kNumPresets = ${presets.length}`. Lascia `presets` solo dove serve il conteggio; più semplice: rinomina tutte e quattro le occorrenze in `resolved`, e `kNumPresets = ${resolved.length}`.

- [ ] **Step 4: Lancia il test e verifica che passi**

```bash
node --test scripts/gen-params.test.mjs
```

Atteso: PASS, 6 test.

- [ ] **Step 5: Converti `presets.json`**

Apri `Source/parameters/presets.json`. Per ogni preset, sostituisci i valori numerici di `wtIndex`, `ftype` e `slope` col `value` dell'opzione corrispondente, usando le liste in `parameters.json`:

- `wtIndex`: `basic`, `saws`, `grit`, `vocal`, `bells`, `pwm` — normalizzato `indice / 5`, quindi `0.0`→`basic`, `0.2`→`saws`, `0.4`→`grit`, `0.6`→`vocal`, `0.8`→`bells`, `1.0`→`pwm`.
- `ftype`: `LP`, `HP`, `BP` — `0.0`→`LP`, `0.5`→`HP`, `1.0`→`BP`.
- `slope`: `12`, `24` — `0.0`→`"12"`, `1.0`→`"24"` (stringhe, anche se sembrano numeri).

Se un valore non cade esattamente su un'opzione, **fermati e riporta**: significa che il preset puntava a metà strada fra due tavole, ed è un bug da decidere, non da arrotondare.

Riscrivi anche `_note` in testa al file: togli la frase sui choice (`wtIndex/ftype/slope sono choice, x = indice/(numOpzioni-1)`) e mettici

```
"I choice (wtIndex, ftype, slope) si scrivono col nome dell'opzione come sta in parameters.json; gli altri parametri con il valore NORMALIZZATO 0..1, la stessa convenzione dell'APVTS."
```

lasciando intatto il resto della nota (le formule di cutoff, att/dec/rel, res, drive e la frase su level/drive/volume).

- [ ] **Step 6: Rigenera e verifica che nulla sia cambiato nei valori**

```bash
node scripts/gen-params.mjs
git diff --stat Source/parameters/PresetTable.h WebUI/src/synth/presets.generated.ts
```

Atteso: `gen-params: N preset → PresetTable.h, presets.generated.ts` e **nessuna differenza** nei due file generati. La conversione è per definizione a valore costante: se il diff non è vuoto, un nome è sbagliato. Fermati e correggi.

- [ ] **Step 7: Lancia i test C++ e WebUI**

```bash
cmake --build --preset macos-debug --target XerumTests && ctest --preset macos-debug
cd WebUI && pnpm test
```

Atteso: tutto verde (nessun comportamento è cambiato).

- [ ] **Step 8: Commit**

```bash
git add scripts/gen-params.mjs scripts/gen-params.test.mjs Source/parameters/presets.json
git commit -m "Let presets name their choices instead of numbering them

A choice parameter normalises as index / (options - 1), so every stored
value shifts the moment an option is added. wtIndex is about to grow
from six entries to thirteen, which would quietly repoint every factory
preset at a different wavetable, and the static_assert that guards the
option order compares file names, not preset values.

Presets now write the option's own name and the generator resolves it,
failing the build on a name no parameter offers. The generated table is
byte for byte what it was.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Le sette tavole nel binario

**Files:**
- Create: `Resources/wavetables/retro-{racing,spindizzy,ggsisters,commando,uridium-pad,leaderboard,uridium}.xwt` (generati)
- Modify: `Resources/wavetables/CREDITS.md`
- Modify: `Source/parameters/parameters.json` (opzioni di `wtIndex`)
- Modify: `Source/dsp/WavetableStore.h:24` (`kWavetableFiles`)
- Modify: `Tests/WavetableTests.cpp`

**Interfaces:**
- Consumes: la CLI del Task 2.
- Produces: `wtIndex` con 13 opzioni, `kWavetableFiles` con 13 voci nello stesso ordine.

- [ ] **Step 1: Scrivi il test C++ che fallisce**

In `Tests/WavetableTests.cpp`, aggiungi un caso che scorre **tutte** le tavole registrate invece delle sei note. Mettilo accanto agli altri `beginTest`, dentro la stessa `UnitTest` che già usa `dsp::parseXwt`:

```cpp
beginTest ("ogni tavola registrata e' un blob valido e costruisce la mipmap");
{
    dsp::WavetableStore store;
    expectEquals (store.getNumTables(), (int) std::size (dsp::kWavetableFiles));

    for (int i = 0; i < store.getNumTables(); ++i)
    {
        store.setActive (i);
        expect (store.active() != nullptr,
                juce::String ("nessuna mipmap per ") + dsp::kWavetableFiles[i]);
    }
}
```

Se `WavetableTests.cpp` non include già `dsp/WavetableStore.h`, aggiungilo in testa.

- [ ] **Step 2: Lancia il test e verifica che passi con sei tavole**

```bash
cmake --build --preset macos-debug --target XerumTests && ctest --preset macos-debug
```

Atteso: PASS. È la rete che deve essere in piedi **prima** di aggiungere le tavole: da qui in poi una tavola registrata ma corrotta fa fallire i test invece di restare in silenzio.

- [ ] **Step 3: Genera i sette `.xwt`**

```bash
node scripts/import-serum-wavetables.mjs /Users/andrea/Downloads/RetroSynthwavePack2
ls -la Resources/wavetables/
```

Atteso: sette file nuovi da 524 300 byte l'uno. Conserva l'output dello script: le righe finali servono al passo 5.

- [ ] **Step 4: Registra le opzioni**

In `Source/parameters/parameters.json`, in coda alle opzioni di `wtIndex` (dopo `pwm`):

```json
        { "value": "retro-racing", "label": "Retro Racing" },
        { "value": "retro-spindizzy", "label": "Retro Spindizzy" },
        { "value": "retro-ggsisters", "label": "Retro GG Sisters" },
        { "value": "retro-commando", "label": "Retro Commando" },
        { "value": "retro-uridium-pad", "label": "Retro Uridium Pad" },
        { "value": "retro-leaderboard", "label": "Retro Leaderboard" },
        { "value": "retro-uridium", "label": "Retro Uridium" }
```

In `Source/dsp/WavetableStore.h`, riga 24, allunga `kWavetableFiles` **nello stesso ordine**:

```cpp
inline constexpr const char* const kWavetableFiles[] = { "basic.xwt", "saws.xwt", "grit.xwt", "vocal.xwt", "bells.xwt", "pwm.xwt",
                                                         "retro-racing.xwt", "retro-spindizzy.xwt", "retro-ggsisters.xwt", "retro-commando.xwt",
                                                         "retro-uridium-pad.xwt", "retro-leaderboard.xwt", "retro-uridium.xwt" };
```

- [ ] **Step 5: Aggiorna `CREDITS.md`**

In fondo a `Resources/wavetables/CREDITS.md`, aggiungi:

```markdown
## Tavole importate da Serum

Estratte da *Retro Synthwave Pack 2* (autore `zak235`) con
`scripts/import-serum-wavetables.mjs` il 2026-09-20. I preset `.fxp` del pack sono stato di
Xfer Serum; alcuni portano una wavetable custom in float32 grezzi dentro un secondo stream
zlib del chunk. Sette tavole distinte, ridotte da 256/278/22 frame ai 64 di Xerum
interpolando lungo l'asse del morph, poi normalizzate con un solo fattore globale.

**Licenza: non risolta.** Il pack è un prodotto commerciale di terzi. Queste sette tavole
stanno qui per la build personale; **non possono essere ridistribuite in un Xerum pubblicato**
senza permesso scritto dell'autore. Se il permesso non arriva, vanno rimosse insieme alle loro
opzioni in `parameters.json` e alle voci in `kWavetableFiles`.

| tavola | frame sorgente | origine |
|---|---|---|
```

seguita dalle sette righe stampate dallo script al passo 3.

- [ ] **Step 6: Riconfigura, ricompila, lancia i test**

```bash
node scripts/gen-params.mjs
cmake --preset macos-debug
cmake --build --preset macos-debug --target XerumTests && ctest --preset macos-debug
```

La riconfigurazione serve: `file(GLOB ... CONFIGURE_DEPENDS)` in `CMakeLists.txt:34` deve rivedere la cartella.

Atteso: compila (lo `static_assert` in `Source/engine/ParamCollect.h:42-45` passa solo se le due liste combaciano) e il test del passo 1 ora scorre tredici tavole.

- [ ] **Step 7: Lancia i test della WebUI**

```bash
cd WebUI && pnpm test
```

Atteso: verde. Se un test conta le opzioni di `wtIndex`, aggiornalo al numero nuovo; non toccare altro.

- [ ] **Step 8: Commit**

```bash
git add Resources/wavetables Source/parameters/parameters.json Source/dsp/WavetableStore.h Source/parameters/ParameterTable.h WebUI/src/synth/params.generated.ts Tests/WavetableTests.cpp
git commit -m "Add the seven Retro Synthwave tables

wtIndex goes from six options to thirteen. The new files take the same
path the AKWF tables already take: dropped into Resources/wavetables,
picked up by the CMake glob, and held in step with parameters.json by
the static_assert that compares option values to file names.

The test that walks the tables now walks all of them through the store
rather than naming the six it knew, so a registered table that fails to
parse or to build its mipmap fails the suite instead of falling back in
silence.

CREDITS.md records where these came from and that their licence is
unresolved: they are fine in a personal build and cannot ship.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Anteprime per la WebUI

**Files:**
- Create: `scripts/build-wavetable-previews.mjs`
- Create: `WebUI/src/synth/wavetables.generated.ts` (generato)
- Create: `scripts/build-wavetable-previews.test.mjs`
- Modify: `WebUI/package.json` (script `gen:wavetables`)

**Interfaces:**
- Consumes: `decodeXwt` da `./wavetable-dsp.mjs` (riga 151); l'ordine delle opzioni di `wtIndex` da `Source/parameters/parameters.json`.
- Produces: `buildPreview(frames: Float32Array[], outFrames: number, outPoints: number): number[][]`, esportata per il test.
- Produces (TypeScript): in `WebUI/src/synth/wavetables.generated.ts`
  ```ts
  export type WavetablePreview = { value: string; frames: number[][] };
  export const WAVETABLE_FRAMES = 9;
  export const WAVETABLE_POINTS = 256;
  export const WAVETABLES: WavetablePreview[];
  ```
  `WAVETABLES` è nell'ordine esatto delle opzioni di `wtIndex`; ogni `frames` ha `WAVETABLE_FRAMES` righe da `WAVETABLE_POINTS` numeri.

- [ ] **Step 1: Scrivi il test che fallisce**

Crea `scripts/build-wavetable-previews.test.mjs`:

```js
import test from "node:test";
import assert from "node:assert/strict";
import { buildPreview } from "./build-wavetable-previews.mjs";

/** 64 frame: il frame f vale costantemente f / 63. */
function rampTable(frameCount = 64, frameSize = 2048) {
  return Array.from({ length: frameCount }, (_, f) => {
    const frame = new Float32Array(frameSize);
    frame.fill(f / (frameCount - 1));
    return frame;
  });
}

test("l'anteprima ha la forma chiesta", () => {
  const preview = buildPreview(rampTable(), 9, 256);
  assert.equal(preview.length, 9);
  assert.equal(preview[0].length, 256);
});

test("il primo e l'ultimo frame sono quelli sorgente", () => {
  const preview = buildPreview(rampTable(), 9, 256);
  assert.equal(preview[0][0], 0);
  assert.equal(preview[8][0], 1);
});

test("i punti sono la media della finestra, non un campione saltato", () => {
  const frames = [Float32Array.from({ length: 8 }, (_, i) => i)];
  const preview = buildPreview(frames, 1, 4);
  assert.deepEqual(preview[0], [0.5, 2.5, 4.5, 6.5]);
});

test("i valori sono arrotondati a quattro decimali", () => {
  const frames = [new Float32Array(8).fill(1 / 3)];
  assert.deepEqual(buildPreview(frames, 1, 4), [[0.3333, 0.3333, 0.3333, 0.3333]]);
});
```

- [ ] **Step 2: Lancia il test e verifica che fallisca**

```bash
node --test scripts/build-wavetable-previews.test.mjs
```

Atteso: FAIL con `Cannot find module .../scripts/build-wavetable-previews.mjs`.

- [ ] **Step 3: Scrivi l'implementazione**

Crea `scripts/build-wavetable-previews.mjs`:

```js
#!/usr/bin/env node
// Genera WebUI/src/synth/wavetables.generated.ts dalle tavole .xwt che finiscono nel binario.
// Uso: node scripts/build-wavetable-previews.mjs   (oppure: cd WebUI && pnpm gen:wavetables)
//
// Girando sugli stessi file che carica il motore, l'onda a schermo non puo' divergere dal
// suono: se una tavola cambia, l'anteprima cambia con lei alla prossima generazione.
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { decodeXwt } from "./wavetable-dsp.mjs";

/** Nove frame perche' e' gia' la pila che WaveDisplay disegna; 256 punti perche' il canvas
    non ne mostra di piu' e 2048 numeri per frame farebbero un file da megabyte. */
const OUT_FRAMES = 9;
const OUT_POINTS = 256;

/**
 * Riduce una tavola a `outFrames` x `outPoints`. Lungo i frame si prende il piu' vicino
 * (i frame estremi restano quelli veri); lungo il ciclo si fa la media della finestra, non
 * si salta un campione ogni otto: una tavola brillante decimata a secco disegnerebbe
 * un'onda che non e' la sua.
 */
export function buildPreview(frames, outFrames, outPoints) {
  const stride = frames[0].length / outPoints;
  const out = [];

  for (let f = 0; f < outFrames; f++) {
    const source = frames[outFrames === 1 ? 0 : Math.round((f * (frames.length - 1)) / (outFrames - 1))];
    const row = [];
    for (let p = 0; p < outPoints; p++) {
      let sum = 0;
      const from = Math.round(p * stride);
      const to = Math.round((p + 1) * stride);
      for (let i = from; i < to; i++) sum += source[i];
      row.push(Math.round((sum / (to - from)) * 1e4) / 1e4);
    }
    out.push(row);
  }

  return out;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = resolve(fileURLToPath(import.meta.url), "../..");
  const params = JSON.parse(readFileSync(resolve(root, "Source/parameters/parameters.json"), "utf8"));
  const wtIndex = params.params.find((p) => p.id === "wtIndex");
  if (!wtIndex) throw new Error("parameters.json non ha il parametro wtIndex");

  const entries = wtIndex.options.map((option) => {
    const file = resolve(root, `Resources/wavetables/${option.value}.xwt`);
    const { frames } = decodeXwt(readFileSync(file));
    return `  { value: ${JSON.stringify(option.value)}, frames: [\n${buildPreview(frames, OUT_FRAMES, OUT_POINTS)
      .map((row) => `    [${row.join(",")}]`)
      .join(",\n")}\n  ] },`;
  });

  const ts = [
    "// GENERATO da scripts/build-wavetable-previews.mjs a partire da Resources/wavetables/*.xwt.",
    "// Non modificare a mano. Rigenera con: node scripts/build-wavetable-previews.mjs",
    "",
    "export type WavetablePreview = { value: string; frames: number[][] };",
    "",
    `export const WAVETABLE_FRAMES = ${OUT_FRAMES};`,
    `export const WAVETABLE_POINTS = ${OUT_POINTS};`,
    "",
    "/** Nell'ordine delle opzioni di wtIndex in parameters.json: l'indice del choice e' l'indice qui. */",
    "export const WAVETABLES: WavetablePreview[] = [",
    ...entries,
    "];",
    "",
  ].join("\n");

  writeFileSync(resolve(root, "WebUI/src/synth/wavetables.generated.ts"), ts);
  console.log(`build-wavetable-previews: ${entries.length} tavole → wavetables.generated.ts`);
}
```

- [ ] **Step 4: Lancia il test e verifica che passi**

```bash
node --test scripts/build-wavetable-previews.test.mjs
```

Atteso: PASS, 4 test.

- [ ] **Step 5: Genera il file e registralo in package.json**

```bash
node scripts/build-wavetable-previews.mjs
```

Atteso: `build-wavetable-previews: 13 tavole → wavetables.generated.ts`.

In `WebUI/package.json`, accanto a `"gen:params"`, aggiungi:

```json
    "gen:wavetables": "node ../scripts/build-wavetable-previews.mjs",
```

- [ ] **Step 6: Verifica che la TypeScript generata compili**

```bash
cd WebUI && pnpm typecheck && pnpm test:scripts
```

Atteso: entrambi verdi.

- [ ] **Step 7: Commit**

```bash
git add scripts/build-wavetable-previews.mjs scripts/build-wavetable-previews.test.mjs WebUI/src/synth/wavetables.generated.ts WebUI/package.json
git commit -m "Generate wave previews from the tables the engine loads

Nine frames of 256 points per table, read from the same .xwt files that
get compiled into the binary, so the picture cannot drift from the
sound. Along the cycle each point is the mean of its window rather than
every eighth sample: decimating a bright table outright would draw a
wave that is not its own.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: `WaveDisplay` disegna la tavola vera

**Files:**
- Modify: `WebUI/src/synth/curves.ts:3-14` (via `sampleWave`, entra `tableSample`), `:/export function spectrum/`
- Modify: `WebUI/src/synth/curves.test.ts:1-20, 52-64`
- Modify: `WebUI/src/synth/ui/WaveDisplay.tsx`
- Modify: `WebUI/src/synth/ui/SynthWindow.test.tsx` se cita `sampleWave`

**Interfaces:**
- Consumes: `WAVETABLES`, `WAVETABLE_FRAMES` da `./wavetables.generated`.
- Produces:
  - `tableSample(frames: number[][], pos: number, t: number, warp: number): number` — `pos` 0..1 lungo i frame, `t` 0..1 di fase, `warp` moltiplica la fase come faceva `sampleWave`.
  - `spectrum(frames: number[][], pos: number, warp: number, level: number, N: number): number[]` — stessa firma di prima con `frames` davanti.

- [ ] **Step 1: Scrivi il test che fallisce**

In `WebUI/src/synth/curves.test.ts`, sostituisci il blocco `describe("sampleWave", ...)` con:

```ts
import { tableSample, filterPath, envPath, lfoPath, spectrum } from "./curves";

/** Due frame piatti a -1 e +1: ogni valore letto dice da solo dove si trova. */
const flat: number[][] = [
  new Array(8).fill(-1),
  new Array(8).fill(1),
];

/** Un frame con un dente: serve a vedere che la fase conta. */
const ramp: number[][] = [Array.from({ length: 8 }, (_, i) => i / 4 - 1)];

describe("tableSample", () => {
  it("legge il primo e l'ultimo frame agli estremi di pos", () => {
    expect(tableSample(flat, 0, 0, 0)).toBeCloseTo(-1);
    expect(tableSample(flat, 1, 0, 0)).toBeCloseTo(1);
  });

  it("interpola fra due frame adiacenti", () => {
    expect(tableSample(flat, 0.5, 0, 0)).toBeCloseTo(0);
  });

  it("segue la fase dentro il frame", () => {
    expect(tableSample(ramp, 0, 0, 0)).toBeCloseTo(-1);
    expect(tableSample(ramp, 0, 0.5, 0)).toBeCloseTo(0);
  });

  it("il warp accelera la fase", () => {
    expect(tableSample(ramp, 0, 0.25, 1)).toBeCloseTo(tableSample(ramp, 0, 1, 0));
  });

  it("resta nei limiti per qualsiasi pos e t", () => {
    for (let p = 0; p <= 1; p += 0.1)
      for (let t = 0; t <= 1; t += 0.1) {
        const s = tableSample(flat, p, t, 0.5);
        expect(s).toBeGreaterThanOrEqual(-1.001);
        expect(s).toBeLessThanOrEqual(1.001);
      }
  });
});
```

E nel blocco `describe("spectrum", ...)` passa la tavola come primo argomento:

```ts
describe("spectrum", () => {
  it("restituisce N magnitudini non negative", () => {
    const s = spectrum(ramp, 0, 0, 1, 16);
    expect(s).toHaveLength(16);
    s.forEach((m) => expect(m).toBeGreaterThanOrEqual(0));
  });

  it("scala col level", () => {
    expect(spectrum(ramp, 0.3, 0.2, 0.5, 8)[0]).toBeCloseTo(spectrum(ramp, 0.3, 0.2, 1, 8)[0]! / 2);
  });

  it("una tavola piatta non ha armoniche", () => {
    expect(spectrum(flat, 0, 0, 1, 8).every((m) => m < 1e-6)).toBe(true);
  });
});
```

Aggiungi in `WebUI/src/synth/ui/SynthWindow.test.tsx` (accanto agli altri test dello schermo):

```tsx
it("disegna tavole diverse per wtIndex diversi", async () => {
  const { WAVETABLES } = await import("../wavetables.generated");
  const { tableSample } = await import("../curves");
  const a = WAVETABLES.find((w) => w.value === "basic")!.frames;
  const b = WAVETABLES.find((w) => w.value === "retro-racing")!.frames;
  const differs = Array.from({ length: 64 }, (_, i) => Math.abs(tableSample(a, 0.5, i / 64, 0) - tableSample(b, 0.5, i / 64, 0)));
  expect(Math.max(...differs)).toBeGreaterThan(0.05);
});
```

- [ ] **Step 2: Lancia i test e verifica che falliscano**

```bash
cd WebUI && pnpm test
```

Atteso: FAIL — `tableSample` non è esportata da `./curves`.

- [ ] **Step 3: Scrivi l'implementazione in `curves.ts`**

Sostituisci `sampleWave` (righe 3-14) con:

```ts
/**
 * Un campione della tavola: `pos` 0..1 sceglie fra i frame dell'anteprima interpolando fra i
 * due adiacenti, `t` 0..1 e' la fase dentro il ciclo, `warp` la accelera come fa l'oscillator
 * sync nel motore (qui e' approssimato: il vero warp vive in SynthVoice).
 */
export function tableSample(frames: number[][], pos: number, t: number, warp: number): number {
  const last = frames.length - 1;
  const fpos = Math.min(last, Math.max(0, pos * last));
  const lo = frames[Math.floor(fpos)]!;
  const hi = frames[Math.min(last, Math.floor(fpos) + 1)]!;
  const fk = fpos - Math.floor(fpos);

  const n = lo.length;
  const ph = (((t * (1 + warp * 3)) % 1) + 1) % 1;
  const x = ph * n;
  const i0 = Math.floor(x) % n;
  const i1 = (i0 + 1) % n;
  const k = x - Math.floor(x);

  const a = lo[i0]! * (1 - k) + lo[i1]! * k;
  const b = hi[i0]! * (1 - k) + hi[i1]! * k;
  return a * (1 - fk) + b * fk;
}
```

E in `spectrum`, aggiungi `frames` come primo parametro e leggi da lì:

```ts
export function spectrum(frames: number[][], pos: number, warp: number, level: number, N: number): number[] {
  const M = 128;
  const frame = Array.from({ length: M }, (_, i) => tableSample(frames, pos, i / M, warp));
  const out: number[] = [];
  for (let h = 1; h <= N; h++) {
    let re = 0;
    let im = 0;
    for (let i = 0; i < M; i++) {
      const s = frame[i]!;
      re += s * Math.cos((2 * Math.PI * h * i) / M);
      im += s * Math.sin((2 * Math.PI * h * i) / M);
    }
    out.push(Math.min(1, (Math.hypot(re, im) / M) * 3) * level);
  }
  return out;
}
```

- [ ] **Step 4: Aggiorna `WaveDisplay.tsx`**

Aggiungi l'import:

```tsx
import { tableSample, spectrum } from "../curves";
import { WAVETABLES } from "../wavetables.generated";
```

(togliendo `sampleWave` dall'import esistente di `../curves`).

Dopo `const name = ...`, aggiungi:

```tsx
  // L'anteprima della tavola scelta: stesso ordine delle opzioni, quindi l'indice del choice
  // e' l'indice qui. Il fallback sulla prima copre solo il caso in cui il file generato sia
  // piu' vecchio di parameters.json.
  const table = (WAVETABLES.find((w) => w.value === wt.value) ?? WAVETABLES[0]!).frames;
```

Poi, nel `draw`:

- sostituisci `const mags = spectrum(cur, warp, level, HARMONICS);` con `const mags = spectrum(table, cur, warp, level, HARMONICS);`
- sostituisci `sampleWave(pos, ((i / 160) * cyc + scroll) % 1, warp)` con `tableSample(table, pos, ((i / 160) * cyc + scroll) % 1, warp)`
- aggiungi `table` alle dipendenze di `useCallback`: `}, [table, position, warp, level, scale]);`

- [ ] **Step 5: Lancia i test e verifica che passino**

```bash
cd WebUI && pnpm test && pnpm typecheck
```

Atteso: PASS. Se `pnpm test` segnala che `sampleWave` è ancora importata da qualche parte, è `PresetOverlay.tsx`: lasciala rotta fino al Task 7 **solo** se il typecheck lo permette; altrimenti applica ora il passo 3 del Task 7 e committa i due insieme.

- [ ] **Step 6: Guarda lo strumento**

```bash
./scripts/dev.sh
```

Cambia `wtIndex` fra `Basic Shapes`, `Analog Saws` e `Retro Racing` e verifica che la pila di nove onde cambi davvero forma, e che le barre dello spettro a destra la seguano. Prima di questo commit erano identiche per tutte e sei le tavole.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/curves.ts WebUI/src/synth/curves.test.ts WebUI/src/synth/ui/WaveDisplay.tsx WebUI/src/synth/ui/SynthWindow.test.tsx
git commit -m "Draw the wavetable that is actually selected

The main screen has always drawn a synthetic morph from sine to saw to
square to pulse. It never read wtIndex — the parameter reached it only
as the label in the corner — so all six tables looked the same, and the
spectrum beside them was the spectrum of the fake.

Both now read the generated preview of the selected table.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: Miniature dei preset e documentazione

**Files:**
- Modify: `WebUI/src/synth/presets.ts:24-31` (`presetWave`)
- Modify: `WebUI/src/synth/ui/PresetOverlay.tsx:5, 31, 63, 144`
- Modify: `WebUI/src/synth/presets.test.ts`
- Modify: `docs/architecture.md`

**Interfaces:**
- Consumes: `tableSample` da `./curves`, `WAVETABLES` da `./wavetables.generated`.
- Produces: `presetWave(p: Preset): { pos: number; warp: number; frames: number[][] }`.

- [ ] **Step 1: Scrivi il test che fallisce**

In `WebUI/src/synth/presets.test.ts`, aggiungi:

```ts
import { WAVETABLES } from "./wavetables.generated";

it("presetWave porta la tavola del preset", () => {
  const withTable = PRESETS.find((p) => p.values.wtIndex !== undefined)!;
  const { frames } = presetWave(withTable);
  expect(frames).toBe(WAVETABLES[Math.round(withTable.values.wtIndex! * (WAVETABLES.length - 1))]!.frames);
});

it("un preset che non tocca wtIndex prende la tavola di default", () => {
  const init = PRESETS.find((p) => p.name === "Init")!;
  expect(presetWave(init).frames).toBe(WAVETABLES[0]!.frames);
});
```

- [ ] **Step 2: Lancia il test e verifica che fallisca**

```bash
cd WebUI && pnpm test presets.test
```

Atteso: FAIL — `presetWave(...).frames` è `undefined`.

- [ ] **Step 3: Scrivi l'implementazione**

In `WebUI/src/synth/presets.ts`, sostituisci `presetWave`:

```ts
/** Posizione, warp e tavola con cui disegnare la miniatura dell'onda: i valori del preset, o i
    default di spec per cio' che il preset non tocca (Init non tocca niente). */
export function presetWave(p: Preset): { pos: number; warp: number; frames: number[][] } {
  const wt = p.values.wtIndex ?? defaultNormalised(PARAM_SPECS.wtIndex);
  return {
    pos: p.values.wtpos ?? defaultNormalised(PARAM_SPECS.wtpos),
    warp: p.values.warp ?? defaultNormalised(PARAM_SPECS.warp),
    frames: (WAVETABLES[Math.round(wt * (WAVETABLES.length - 1))] ?? WAVETABLES[0]!).frames,
  };
}
```

con l'import `import { WAVETABLES } from "./wavetables.generated";` in testa.

In `WebUI/src/synth/ui/PresetOverlay.tsx`:

- riga 5: `import { sampleWave } from "../curves";` → `import { tableSample } from "../curves";`
- riga 31: `const selWave = presetWave(sel);` resta, ma dove si destruttura `{ pos, warp }` aggiungi `frames`
- riga 63: idem per `const w = presetWave(p);`
- riga 144: `sampleWave(p, i / 96, warp)` → `tableSample(frames, p, i / 96, warp)`, dove `frames` viene dallo stesso `presetWave` da cui arriva già `warp`

- [ ] **Step 4: Lancia i test e verifica che passino**

```bash
cd WebUI && pnpm test && pnpm typecheck
```

Atteso: PASS.

- [ ] **Step 5: Documenta**

In `docs/architecture.md`, nella sezione che descrive le wavetable, aggiungi:

```markdown
### Da dove vengono le tavole, e come se ne aggiunge una

Due sorgenti, una sola strada. Le sei tavole originali nascono dalle famiglie AKWF con
`scripts/fetch-wavetables.mjs`; le sette `retro-*` vengono dalle wavetable custom incorporate
nei preset Serum del pack *Retro Synthwave Pack 2*, estratte da
`scripts/import-serum-wavetables.mjs`. Entrambi gli script scrivono un `.xwt` — 64 frame da
2048 campioni, float32 — in `Resources/wavetables`, che `file(GLOB CONFIGURE_DEPENDS)`
raccoglie e `juce_add_binary_data` compila nel target `WavetableAssets`.

Aggiungere una tavola vuol dire quattro cose, e la quarta è la sola che si dimentica:

1. scrivere il `.xwt` in `Resources/wavetables`;
2. aggiungere l'opzione in coda a `wtIndex` in `Source/parameters/parameters.json`, col
   `value` uguale al nome del file senza estensione;
3. aggiungere la voce in coda a `kWavetableFiles` in `Source/dsp/WavetableStore.h`, nella
   stessa posizione — lo `static_assert` in `Source/engine/ParamCollect.h` non compila se le due
   liste divergono;
4. rigenerare le anteprime della WebUI con `node scripts/build-wavetable-previews.mjs`,
   altrimenti lo schermo continua a disegnare la tavola vecchia a quell'indice.

Nessuna tavola si carica da disco a runtime: `WavetableStore` legge solo da `WavetableAssets`.

I preset di fabbrica non contengono l'indice della tavola ma il suo nome
(`"wtIndex": "retro-racing"` in `Source/parameters/presets.json`): un choice si normalizza
`indice / (numOpzioni - 1)`, e senza il nome ogni aggiunta di tavola ripunterebbe in silenzio
tutti i preset già scritti. `scripts/gen-params.mjs` risolve il nome e fallisce la
generazione se non esiste.
```

Se la sezione wavetable in `docs/architecture.md` non esiste, mettila dopo quella
sull'oscillatore.

- [ ] **Step 6: Lancia tutta la suite**

```bash
cd WebUI && pnpm test && pnpm typecheck && pnpm test:scripts
cd .. && cmake --build --preset macos-debug --target XerumTests && ctest --preset macos-debug
```

Atteso: tutto verde.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/presets.ts WebUI/src/synth/presets.test.ts WebUI/src/synth/ui/PresetOverlay.tsx docs/architecture.md
git commit -m "Give each preset thumbnail its own table, and write down how to add one

presetWave already returned the position and warp a preset asks for; it
now returns the table too, so the browser's thumbnails stop all drawing
the same wave.

The architecture notes gain the four steps an added table needs. The
fourth — regenerating the WebUI previews — is the one with no compiler
behind it, so the screen would keep drawing the old table at that index
without a word.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Self-Review

**Copertura della spec:**

| sezione della spec | task |
|---|---|
| Pipeline di import, punti 1-4 (header, stream, dedup, soglia) | Task 1 + 2 |
| Punti 5-6 (riduzione a 64 frame, normalizzazione globale) | Task 2, via `selectFrames` + `normaliseTable` |
| Punto 7 (`.xwt`) | Task 2, via `encodeXwt` |
| Punto 8 (metriche + CREDITS) | Task 2 step 3, Task 4 step 5 |
| Niente riallineamento di fase | Task 2, esplicito nel commento e nel messaggio di commit |
| Registrazione delle tavole | Task 4 |
| Preset per nome | Task 3 |
| Anteprime generate | Task 5 |
| Onda vera in `WaveDisplay` | Task 6 |
| Spettro (deriva da `sampleWave`) | Task 6 — cambiando `spectrum` segue da sé |
| Miniature di `PresetOverlay` | Task 7 |
| Errori dell'importatore | Task 1 (test sugli header), Task 2 (`parseArgs`, tavole mancanti) |
| Test C++ su tutte le tavole | Task 4 step 1 |
| Test WebUI su tavole diverse | Task 6 step 1 |
| `docs/architecture.md` | Task 7 step 5 |
| Nota di licenza | Global Constraints + Task 4 step 5 |

**Fuori dal piano, per scelta:** la spec elencava fra i file toccati `Source/parameters/PresetValue.h` e `PresetTable.h` «se il tipo generato cambia forma». Non cambia: i choice si risolvono a numero nel generatore, e l'header resta `{ const char* id; float value; }`. Nessun task li tocca.

**Aggiunta rispetto alla spec:** il test C++ del Task 4 step 1 verifica anche `buildMipTable` su ogni tavola, non solo `parseXwt`. Una tavola può essere un blob valido e non costruire la piramide (frame troppo corto): con tredici tavole il caso vale la riga in più.

**Il test C++ su ogni preset** citato nella spec («ogni preset di `kPresetTable` risolve i propri choice a un indice dentro il numero di opzioni») non ha un task dedicato: il Task 3 lo sposta a compile time nel generatore, che fallisce la build su un nome ignoto. Un test runtime su un dato ormai costante non aggiunge niente. Se in review lo si vuole comunque, va in `Tests/PresetValueTests.cpp`.

---

## Esecuzione

Piano salvato in `docs/superpowers/plans/2026-09-20-serum-wavetable-import.md`.

I Task 1-2 e il Task 3 non si toccano (script nuovi contro generatore + `presets.json`) e possono andare in parallelo. Dal Task 4 in poi l'ordine è stretto: il 4 richiede il 2 e il 3, il 5 richiede il 4, il 6 il 5, il 7 il 6.
