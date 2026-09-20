import test from "node:test";
import assert from "node:assert/strict";
import { deflateSync } from "node:zlib";
import { parseFxp, splitZlibStreams, framesFromStream, SERUM_FRAME_SIZE } from "./serum-fxp.mjs";
import { collectWavetables } from "./import-serum-wavetables.mjs";

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

test("parseFxp rifiuta un chunk dichiarato a zero byte con un messaggio che non parla di troncamento", () => {
  assert.throws(
    () => parseFxp(makeFxp({ chunk: Buffer.alloc(0), declaredLen: 0 })),
    (err) => /0 byte/.test(err.message) && !/troncato/.test(err.message),
  );
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

test("splitZlibStreams ignora una coda che comincia per 0x78 senza essere un header valido", () => {
  // Caso reale: PD-Leaderboard0.fxp (RetroSynthwavePack2) lascia esattamente questa coda
  // dopo i suoi due stream. 0x78 0x0a non e' un header zlib valido (RFC 1950, check mod 31).
  const a = deflateSync(Buffer.from("stato"));
  const b = deflateSync(rampStream(2));
  const chunk = Buffer.concat([a, b, Buffer.from([0x78, 0x0a, 0x00, 0x00])]);
  const streams = splitZlibStreams(chunk);
  assert.equal(streams.length, 2);
  assert.equal(streams[0].toString(), "stato");
  assert.equal(streams[1].length, 2 * SERUM_FRAME_SIZE * 4);
});

test("splitZlibStreams accetta ancora gli header zlib reali 0x78 0x01 e 0x78 0x9c", () => {
  const fast = deflateSync(Buffer.from("stato"), { level: 1 }); // header 78 01
  const normal = deflateSync(rampStream(2), { level: 6 }); // header 78 9c
  assert.deepEqual(fast.subarray(0, 2), Buffer.from([0x78, 0x01]));
  assert.deepEqual(normal.subarray(0, 2), Buffer.from([0x78, 0x9c]));

  const streams = splitZlibStreams(Buffer.concat([fast, normal]));
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
