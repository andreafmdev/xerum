import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { buildPreview } from "./build-wavetable-previews.mjs";

const root = resolve(fileURLToPath(import.meta.url), "../..");

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

test("outPoints maggiore dei campioni per frame lancia invece di produrre NaN", () => {
  // stride = 4 / 8 = 0.5 < 1: senza guardia `from` e `to` combaciano dopo l'arrotondamento
  // e la media diventa 0/0.
  const frames = [new Float32Array(4).fill(1)];
  assert.throws(() => buildPreview(frames, 1, 8), /outPoints/);
});

test("frame di lunghezza diversa lanciano invece di leggere fuori dai limiti", () => {
  const frames = [new Float32Array(8).fill(1), new Float32Array(4).fill(1)];
  assert.throws(() => buildPreview(frames, 2, 4), /lunghezza/);
});

/**
 * Estrae i `value` delle voci di WAVETABLES leggendo wavetables.generated.ts come testo,
 * invece di importarlo come modulo: un `import()` di un file `.ts` funziona solo da Node
 * 22.18 in su (dove lo strip dei tipi e' attivo di default), e la CI pinna solo "22" — un
 * giorno che risolvesse a un 22.x precedente il test fallirebbe con
 * ERR_UNKNOWN_FILE_EXTENSION, un errore che non c'entra nulla con cio' che il test verifica.
 * Il file e' generato da noi con una forma nota e stabile (vedi build-wavetable-previews.mjs:
 * ogni voce e' `{ value: "<slug>", frames: [...] }`), quindi l'estrazione testuale e'
 * affidabile quanto un import, senza il vincolo di versione.
 */
function extractWavetableValues(source) {
  const startMarker = "export const WAVETABLES: WavetablePreview[] = [";
  const start = source.indexOf(startMarker);
  assert.notEqual(start, -1, "wavetables.generated.ts: non trovo l'array WAVETABLES nella forma attesa");

  const afterStart = source.slice(start + startMarker.length);
  const end = afterStart.indexOf("\n];");
  assert.notEqual(end, -1, "wavetables.generated.ts: non trovo la chiusura dell'array WAVETABLES");

  const arrayBody = afterStart.slice(0, end);
  const entryPattern = /\{\s*value:\s*("(?:[^"\\]|\\.)*")\s*,\s*frames:\s*\[/g;
  const values = [];
  let match;
  while ((match = entryPattern.exec(arrayBody)) !== null) {
    values.push(JSON.parse(match[1]));
  }

  // Fondamentale: se la forma del file generato cambiasse (es. l'ordine dei campi, o il nome
  // della chiave), il pattern sopra non troverebbe piu' nulla. Un test che a quel punto
  // confrontasse due array vuoti passerebbe a vuoto — peggio di nessun test. Qui invece
  // falliamo esplicitamente non appena l'estrazione non trova neanche una voce.
  assert.ok(
    values.length > 0,
    "wavetables.generated.ts: zero voci estratte dall'array WAVETABLES — la forma del file e' " +
      "cambiata, aggiorna il pattern di estrazione in questo test",
  );

  return values;
}

// Il test di consistenza che la spec chiedeva: wavetables.generated.ts deve avere una voce
// per ogni opzione di wtIndex, nello stesso ordine e con gli stessi `value`. Senza questo test
// una .xwt aggiunta a parameters.json e dimenticata nella rigenerazione delle anteprime passa
// inosservata — ed e' esattamente il buco che presetWave() sfruttava (vedi presets.ts).
test("wavetables.generated.ts ha una voce per ogni opzione di wtIndex, stesso ordine e stessi value", () => {
  const params = JSON.parse(readFileSync(resolve(root, "Source/parameters/parameters.json"), "utf8"));
  const wtIndex = params.params.find((p) => p.id === "wtIndex");
  assert.ok(wtIndex, "parameters.json non ha il parametro wtIndex");

  const source = readFileSync(resolve(root, "WebUI/src/synth/wavetables.generated.ts"), "utf8");
  const values = extractWavetableValues(source);

  assert.equal(values.length, wtIndex.options.length);
  assert.deepEqual(values, wtIndex.options.map((o) => o.value));
});
