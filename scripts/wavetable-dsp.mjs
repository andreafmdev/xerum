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
