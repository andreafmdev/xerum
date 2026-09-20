import test from "node:test";
import assert from "node:assert/strict";
import {
  classifyTable,
  KNOWN_DUPLICATE_TABLES,
  KNOWN_TABLES,
  realignSerumTable,
  REALIGN_TABLES,
} from "./import-serum-wavetables.mjs";
import { adjacentCorrelations, midMorphRmsLosses } from "./wavetable-dsp.mjs";

test("classifyTable riconosce una tavola nota", () => {
  const sha1 = Object.keys(KNOWN_TABLES)[0];
  const got = classifyTable(sha1);
  assert.equal(got.kind, "known");
  assert.equal(got.slug, KNOWN_TABLES[sha1]);
});

test("classifyTable segnala un duplicato noto, non lo confonde con uno slug sconosciuto", () => {
  for (const [sha1, expected] of Object.entries(KNOWN_DUPLICATE_TABLES)) {
    const got = classifyTable(sha1);
    assert.equal(got.kind, "duplicate");
    assert.equal(got.slug, expected.slug);
    assert.equal(got.duplicateOf, expected.duplicateOf);
    assert.equal(got.ofSha1, expected.ofSha1);
    assert.equal(got.maxDiff, expected.maxDiff);
  }
});

test("classifyTable non spedisce i due duplicati come tavole note", () => {
  for (const sha1 of Object.keys(KNOWN_DUPLICATE_TABLES)) assert.equal(sha1 in KNOWN_TABLES, false);
});

test("classifyTable restituisce unknown per uno sha1 mai visto", () => {
  const got = classifyTable("deadbeef");
  assert.equal(got.kind, "unknown");
});

test("REALIGN_TABLES contiene solo le due tavole fuori soglia, non retro-racing", () => {
  assert.deepEqual([...REALIGN_TABLES].sort(), ["retro-commando", "retro-uridium-pad"]);
  assert.equal(REALIGN_TABLES.has("retro-racing"), false);
});

/** Due cicli in antifase (seno e -seno): correlazione -1, perdita totale a meta' morph. */
function antiphaseFrames(count, frameSize = 2048) {
  const cycle = (sign) => {
    const f = new Float32Array(frameSize);
    for (let i = 0; i < frameSize; i++) f[i] = sign * Math.sin((2 * Math.PI * i) / frameSize);
    return f;
  };
  const frames = [];
  for (let i = 0; i < count; i++) frames.push(cycle(i % 2 === 0 ? 1 : -1));
  return frames;
}

test("realignSerumTable migliora la correlazione fra frame adiacenti su un caso in antifase", () => {
  const input = antiphaseFrames(16);
  const before = Math.min(...adjacentCorrelations(input));
  assert.ok(before < 0, "il caso di test deve partire scorrelato/in antifase");

  const { frames: after } = realignSerumTable(input);
  const afterCorr = Math.min(...adjacentCorrelations(after));
  assert.ok(afterCorr > before, `la correlazione minima deve migliorare (${before} → ${afterCorr})`);
  assert.equal(after.length, 64, "la tavola riallineata ha comunque i 64 frame attesi");
});

test("realignSerumTable non altera lo spettro di ampiezza quando basta il solo allineamento di fase", () => {
  // Un ciclo puro ruotato di mezzo periodo ha lo stesso spettro di ampiezza dell'originale:
  // il passo A (solo rotazione) deve azzerare la perdita a meta' morph senza il passo B.
  const frameSize = 2048;
  const base = new Float32Array(frameSize);
  for (let i = 0; i < frameSize; i++) base[i] = Math.sin((2 * Math.PI * i) / frameSize) + 0.3 * Math.sin((6 * Math.PI * i) / frameSize);
  const half = frameSize / 2;
  const rotated = new Float32Array(frameSize);
  for (let i = 0; i < frameSize; i++) rotated[i] = base[(i + half) % frameSize];

  const { frames: after, usedStepB } = realignSerumTable([base, rotated, base, rotated]);
  assert.equal(usedStepB, false);
  assert.ok(Math.min(...midMorphRmsLosses(after)) > -0.1, "dopo l'allineamento la perdita a meta' morph deve essere quasi nulla");
});
