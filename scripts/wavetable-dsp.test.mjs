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
