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

/** Toglie la continua da un singolo frame. In place. */
export function removeDc(frame) {
  let sum = 0;
  for (let i = 0; i < frame.length; i++) sum += frame[i];
  const mean = sum / frame.length;
  for (let i = 0; i < frame.length; i++) frame[i] -= mean;
  return frame;
}

/**
 * Toglie la continua frame per frame e poi scala l'INTERA tavola con un solo
 * fattore, quello che porta a 1 il massimo |campione| su tutti i frame.
 *
 * Normalizzare frame per frame (quello che si faceva prima) è il motivo per
 * cui muovere Position cambiava il volume fino a 16 dB: frame timbricamente
 * diversi finivano tutti allo stesso picco, quindi con RMS molto diversi.
 * Un solo fattore conserva invece i rapporti di livello originali fra frame,
 * che è quello che fa Vital in `Wavetable::postProcess` (`scale = 2 / max_span`
 * calcolato su tutti i frame).
 */
export function normaliseTable(frames) {
  let peak = 0;
  for (const frame of frames) {
    removeDc(frame);
    for (let i = 0; i < frame.length; i++) peak = Math.max(peak, Math.abs(frame[i]));
  }

  if (peak > 0) {
    const scale = 1 / peak;
    for (const frame of frames) for (let i = 0; i < frame.length; i++) frame[i] *= scale;
  }
  return frames;
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

/** Legge un .xwt e restituisce i frame. Speculare a encodeXwt. */
export function decodeXwt(buffer) {
  if (buffer.length < 12 || buffer.toString("ascii", 0, 4) !== "XWT1") throw new Error("non è un file XWT1");
  const frameCount = buffer.readUInt32LE(4);
  const frameSize = buffer.readUInt32LE(8);
  if (buffer.length < 12 + frameCount * frameSize * 4) throw new Error("file troncato");

  const frames = [];
  let offset = 12;
  for (let f = 0; f < frameCount; f++) {
    const frame = new Float32Array(frameSize);
    for (let i = 0; i < frameSize; i++) {
      frame[i] = buffer.readFloatLE(offset);
      offset += 4;
    }
    frames.push(frame);
  }
  return { frames, frameSize };
}

// ---------------------------------------------------------------------------
// FFT e allineamento di fase fra frame
// ---------------------------------------------------------------------------

/** Vero se n è una potenza di due (e almeno 1). */
function isPowerOfTwo(n) {
  return n > 0 && (n & (n - 1)) === 0;
}

/** FFT complessa in place, radix-2 decimation-in-time. Richiede n potenza di due. */
function fftPow2(re, im, inverse) {
  const n = re.length;

  // Permutazione bit-reversed: mette gli ingressi nell'ordine che le farfalle si aspettano.
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const tr = re[i]; re[i] = re[j]; re[j] = tr;
      const ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }

  for (let len = 2; len <= n; len <<= 1) {
    const half = len >> 1;
    const step = ((inverse ? 2 : -2) * Math.PI) / len;
    for (let i = 0; i < n; i += len) {
      for (let k = 0; k < half; k++) {
        // Twiddle calcolato di volta in volta: a 2048 campioni costa poco e non
        // accumula l'errore della ricorrenza.
        const ang = step * k;
        const wr = Math.cos(ang);
        const wi = Math.sin(ang);
        const a = i + k;
        const b = a + half;
        const vr = re[b] * wr - im[b] * wi;
        const vi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - vr;
        im[b] = im[a] - vi;
        re[a] += vr;
        im[a] += vi;
      }
    }
  }

  if (inverse) for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

/**
 * FFT complessa in place per qualsiasi lunghezza. Se n non è potenza di due
 * usa Bluestein (chirp-z): la DFT diventa una convoluzione, che si fa con due
 * FFT radix-2 più lunghe. Resta O(n log n), niente doppio ciclo O(n²).
 */
export function fft(re, im, inverse = false) {
  const n = re.length;
  if (im.length !== n) throw new Error("parte reale e immaginaria di lunghezza diversa");
  if (n <= 1) return;
  if (isPowerOfTwo(n)) return fftPow2(re, im, inverse);

  const sign = inverse ? 1 : -1;
  let m = 1;
  while (m < 2 * n - 1) m <<= 1;

  // Chirp e^{sign * i * π k² / n}, con k² preso mod 2n per non perdere precisione.
  const chirpRe = new Float64Array(n);
  const chirpIm = new Float64Array(n);
  for (let k = 0; k < n; k++) {
    const ang = (sign * Math.PI * ((k * k) % (2 * n))) / n;
    chirpRe[k] = Math.cos(ang);
    chirpIm[k] = Math.sin(ang);
  }

  const ar = new Float64Array(m);
  const ai = new Float64Array(m);
  for (let k = 0; k < n; k++) {
    ar[k] = re[k] * chirpRe[k] - im[k] * chirpIm[k];
    ai[k] = re[k] * chirpIm[k] + im[k] * chirpRe[k];
  }

  const br = new Float64Array(m);
  const bi = new Float64Array(m);
  br[0] = chirpRe[0];
  bi[0] = -chirpIm[0];
  for (let k = 1; k < n; k++) {
    br[k] = br[m - k] = chirpRe[k];
    bi[k] = bi[m - k] = -chirpIm[k];
  }

  fftPow2(ar, ai, false);
  fftPow2(br, bi, false);
  for (let k = 0; k < m; k++) {
    const pr = ar[k] * br[k] - ai[k] * bi[k];
    ai[k] = ar[k] * bi[k] + ai[k] * br[k];
    ar[k] = pr;
  }
  fftPow2(ar, ai, true);

  for (let k = 0; k < n; k++) {
    re[k] = ar[k] * chirpRe[k] - ai[k] * chirpIm[k];
    im[k] = ar[k] * chirpIm[k] + ai[k] * chirpRe[k];
    if (inverse) { re[k] /= n; im[k] /= n; }
  }
}

/** FFT di un segnale reale: restituisce { re, im } come Float64Array di lunghezza n. */
export function fftReal(signal) {
  const re = Float64Array.from(signal);
  const im = new Float64Array(signal.length);
  fft(re, im, false);
  return { re, im };
}

/**
 * Correlazione incrociata circolare: out[s] = Σ a[i] · b[(i + s) mod n].
 * Via FFT — IFFT(conj(FFT(a)) · FFT(b)) — non con il doppio ciclo O(n²).
 */
export function circularCrossCorrelation(a, b) {
  const n = a.length;
  if (b.length !== n) throw new Error("i due segnali hanno lunghezza diversa");

  const A = fftReal(a);
  const B = fftReal(b);
  const re = new Float64Array(n);
  const im = new Float64Array(n);
  for (let k = 0; k < n; k++) {
    // conj(A) · B
    re[k] = A.re[k] * B.re[k] + A.im[k] * B.im[k];
    im[k] = A.re[k] * B.im[k] - A.im[k] * B.re[k];
  }
  fft(re, im, true);
  return re;
}

/** Lo shift circolare che allinea al meglio `frame` su `reference`. */
export function bestCircularShift(reference, frame) {
  const corr = circularCrossCorrelation(reference, frame);
  let best = 0;
  for (let s = 1; s < corr.length; s++) if (corr[s] > corr[best]) best = s;
  return best;
}

/** Ruota un ciclo di `shift` campioni: out[i] = frame[(i + shift) mod n]. */
export function rotateCycle(frame, shift) {
  const n = frame.length;
  const out = new Float32Array(n);
  const s = ((shift % n) + n) % n;
  for (let i = 0; i < n; i++) out[i] = frame[(i + s) % n];
  return out;
}

/**
 * Passo A: allineamento a fase lineare. Ogni frame viene ruotato sullo shift
 * circolare che massimizza la correlazione con il frame precedente già
 * allineato. Su un ciclo singolo la rotazione è solo un offset di fase: la
 * forma d'onda e il suo spettro di AMPIEZZA restano identici, quindi il timbro
 * del singolo frame non cambia. Cambia solo come i frame si sommano durante il
 * morph, che è esattamente il difetto da togliere.
 *
 * Restituisce nuovi frame; l'ingresso non viene toccato.
 */
export function alignFramesToPhase(frames) {
  if (frames.length === 0) return [];
  const out = [Float32Array.from(frames[0])];
  for (let k = 1; k < frames.length; k++) {
    out.push(rotateCycle(frames[k], bestCircularShift(out[k - 1], frames[k])));
  }
  return out;
}

/**
 * Passo B: continuità di fase per armonica. Per ogni armonica si srotola
 * (unwrap) la fase lungo i frame e la si riscrive come retta ai minimi
 * quadrati, così che vari con continuità invece di saltare. Le ampiezze
 * restano invariate, ma i singoli frame SÌ che cambiano: da usare solo dove il
 * passo A non basta a fermare la cancellazione a pettine.
 *
 * Restituisce nuovi frame; l'ingresso non viene toccato.
 */
export function enforceHarmonicPhaseContinuity(frames) {
  const count = frames.length;
  if (count < 2) return frames.map((f) => Float32Array.from(f));

  const n = frames[0].length;
  const spectra = frames.map((f) => fftReal(f));
  const half = Math.floor(n / 2);

  // Somme per la retta ai minimi quadrati: x è l'indice di frame, uguale per tutte le armoniche.
  let sumX = 0;
  let sumXX = 0;
  for (let j = 0; j < count; j++) { sumX += j; sumXX += j * j; }
  const denom = count * sumXX - sumX * sumX;

  for (let k = 1; k <= half; k++) {
    // Il bin di Nyquist di un segnale reale deve restare reale: non ha una fase
    // libera da riscrivere, quindi lo si lascia com'è.
    if (n % 2 === 0 && k === half) continue;

    // Srotolamento: ogni salto oltre π viene riportato dentro sommando multipli di 2π.
    const phase = new Float64Array(count);
    let prev = Math.atan2(spectra[0].im[k], spectra[0].re[k]);
    phase[0] = prev;
    for (let j = 1; j < count; j++) {
      const raw = Math.atan2(spectra[j].im[k], spectra[j].re[k]);
      let d = raw - prev;
      d -= 2 * Math.PI * Math.round(d / (2 * Math.PI));
      phase[j] = phase[j - 1] + d;
      prev = raw;
    }

    let sumY = 0;
    let sumXY = 0;
    for (let j = 0; j < count; j++) { sumY += phase[j]; sumXY += j * phase[j]; }
    const slope = denom === 0 ? 0 : (count * sumXY - sumX * sumY) / denom;
    const intercept = (sumY - slope * sumX) / count;

    for (let j = 0; j < count; j++) {
      const s = spectra[j];
      const amp = Math.hypot(s.re[k], s.im[k]);
      const p = intercept + slope * j;
      s.re[k] = amp * Math.cos(p);
      s.im[k] = amp * Math.sin(p);
      // Il bin speculare deve restare il coniugato, altrimenti il frame non torna reale.
      const mirror = n - k;
      if (mirror < n && mirror !== k) { s.re[mirror] = s.re[k]; s.im[mirror] = -s.im[k]; }
    }
  }

  return spectra.map((s) => {
    fft(s.re, s.im, true);
    const out = new Float32Array(n);
    for (let i = 0; i < n; i++) out[i] = s.re[i];
    return out;
  });
}

// ---------------------------------------------------------------------------
// Metriche: servono ai test e allo script che ri-processa le tavole
// ---------------------------------------------------------------------------

/** RMS di un frame. */
export function frameRms(frame) {
  let sum = 0;
  for (let i = 0; i < frame.length; i++) sum += frame[i] * frame[i];
  return Math.sqrt(sum / frame.length);
}

/** Spettro di ampiezza di un frame (bin 0..n/2). */
export function amplitudeSpectrum(frame) {
  const { re, im } = fftReal(frame);
  const half = Math.floor(frame.length / 2);
  const out = new Float64Array(half + 1);
  for (let k = 0; k <= half; k++) out[k] = Math.hypot(re[k], im[k]);
  return out;
}

/** Coefficiente di correlazione (a shift zero) fra coppie di frame adiacenti. */
export function adjacentCorrelations(frames) {
  const out = [];
  for (let k = 1; k < frames.length; k++) {
    const a = frames[k - 1];
    const b = frames[k];
    let num = 0;
    let da = 0;
    let db = 0;
    for (let i = 0; i < a.length; i++) { num += a[i] * b[i]; da += a[i] * a[i]; db += b[i] * b[i]; }
    out.push(da > 0 && db > 0 ? num / Math.sqrt(da * db) : 0);
  }
  return out;
}

/**
 * Perdita di RMS a metà morph, in dB, per ogni coppia di frame adiacenti:
 * RMS di 0.5·(frame[k] + frame[k+1]) contro la media dei due RMS. Se i frame
 * sono allineati vale circa 0 dB; se si cancellano a pettine scende.
 */
export function midMorphRmsLosses(frames) {
  const out = [];
  for (let k = 1; k < frames.length; k++) {
    const a = frames[k - 1];
    const b = frames[k];
    const mix = new Float32Array(a.length);
    for (let i = 0; i < a.length; i++) mix[i] = 0.5 * (a[i] + b[i]);
    const reference = 0.5 * (frameRms(a) + frameRms(b));
    out.push(reference > 0 ? 20 * Math.log10(Math.max(frameRms(mix), 1e-12) / reference) : 0);
  }
  return out;
}

/**
 * Deviazione standard circolare (in gradi) della fase della fondamentale lungo
 * i frame: dice quanto i frame partono da punti diversi del ciclo.
 */
export function fundamentalPhaseDeviation(frames) {
  let sx = 0;
  let sy = 0;
  for (const frame of frames) {
    const { re, im } = fftReal(frame);
    const p = Math.atan2(im[1], re[1]);
    sx += Math.cos(p);
    sy += Math.sin(p);
  }
  const r = Math.hypot(sx, sy) / frames.length;
  if (r >= 1) return 0;
  return (Math.sqrt(-2 * Math.log(Math.max(r, 1e-12))) * 180) / Math.PI;
}

/** Escursione (max − min, in dB) dell'RMS fra i frame della tavola. */
export function rmsSpreadDb(frames) {
  let min = Infinity;
  let max = 0;
  for (const frame of frames) {
    const r = frameRms(frame);
    if (r < min) min = r;
    if (r > max) max = r;
  }
  if (!(min > 0)) return Infinity;
  return 20 * Math.log10(max / min);
}

/**
 * Perdita minima possibile a metà morph, in dB, per ogni coppia adiacente:
 * quella che resterebbe anche se i frame fossero perfettamente in fase.
 * Dipende solo da quanto si somigliano gli spettri di ampiezza, quindi dal
 * materiale di partenza: nessun trattamento di fase può scendere sotto.
 */
export function midMorphLossBounds(frames) {
  const norm = (v) => Math.sqrt(v.reduce((s, x) => s + x * x, 0));
  const spectra = frames.map(amplitudeSpectrum);
  const out = [];
  for (let k = 1; k < frames.length; k++) {
    const a = spectra[k - 1];
    const b = spectra[k];
    const mix = a.map((v, i) => 0.5 * (v + b[i]));
    const reference = 0.5 * (norm(a) + norm(b));
    out.push(reference > 0 ? 20 * Math.log10(Math.max(norm(mix), 1e-12) / reference) : 0);
  }
  return out;
}

/**
 * Equalizzazione parziale dell'RMS fra i frame: ogni frame viene moltiplicato
 * per `(rmsTarget / rms)^exponent`. In place.
 *
 * Serve perché le onde AKWF arrivano già normalizzate a picco 1.0 una per una,
 * con RMS che dentro la stessa famiglia varia di una decina di dB: lo squilibrio
 * è nel materiale, non nella pipeline, quindi la normalizzazione globale non ha
 * niente da correggere. Senza questo passaggio muovere Position cambia volume.
 *
 * L'esponente è il punto: a 1.0 l'escursione sparirebbe del tutto, ma in uno
 * sweep PWM l'impulso che si stringe DEVE calare di volume, è il suono giusto.
 * A 0.7 l'escursione in dB si riduce esattamente al 30% — vale
 * `dB' = e·dB(target) + (1 − e)·dB(rms)`, quindi la compressione è (1 − e)
 * qualunque sia il target — e le differenze musicalmente corrette restano.
 *
 * Il guadagno è costante su tutto il frame: i rapporti fra le armoniche non si
 * toccano, quindi il timbro del singolo frame è quello di prima.
 *
 * Il target è la mediana: siccome sposta solo il livello complessivo (che la
 * normalizzazione globale poi riazzera) e non l'escursione, tanto vale prendere
 * la statistica che un singolo frame anomalo non trascina.
 */
export function equaliseFrameRms(frames, exponent = 0.7) {
  if (frames.length === 0) return frames;

  const levels = frames.map(frameRms);
  const sorted = Array.from(levels).sort((a, b) => a - b);
  const mid = sorted.length >> 1;
  const target = sorted.length % 2 === 1 ? sorted[mid] : 0.5 * (sorted[mid - 1] + sorted[mid]);
  if (!(target > 0)) return frames;

  for (let k = 0; k < frames.length; k++) {
    if (!(levels[k] > 0)) continue;
    const gain = Math.pow(target / levels[k], exponent);
    const frame = frames[k];
    for (let i = 0; i < frame.length; i++) frame[i] *= gain;
  }
  return frames;
}
