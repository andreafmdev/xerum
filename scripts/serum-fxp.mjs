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
