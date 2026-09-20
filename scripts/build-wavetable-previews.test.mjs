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
