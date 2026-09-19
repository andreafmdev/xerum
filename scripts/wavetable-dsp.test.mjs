import test from "node:test";
import assert from "node:assert/strict";
import {
  adjacentCorrelations,
  alignFramesToPhase,
  amplitudeSpectrum,
  bestCircularShift,
  circularCrossCorrelation,
  decodeXwt,
  encodeXwt,
  enforceHarmonicPhaseContinuity,
  equaliseFrameRms,
  fft,
  midMorphLossBounds,
  midMorphRmsLosses,
  normaliseTable,
  parseWav16Mono,
  removeDc,
  resampleCycle,
  rmsSpreadDb,
  rotateCycle,
  selectFrames,
} from "./wavetable-dsp.mjs";

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

test("removeDc toglie la continua e lascia stare il livello", () => {
  const f = Float32Array.from([0.5, 0.7, 0.5, 0.3]);
  removeDc(f);
  const mean = f.reduce((a, b) => a + b, 0) / f.length;
  assert.ok(Math.abs(mean) < 1e-6, `media ${mean}`);
  assert.ok(Math.abs(Math.max(...Array.from(f, Math.abs)) - 0.2) < 1e-6, "il picco non va toccato");
});

test("normaliseTable usa un solo fattore per tutta la tavola", () => {
  // Due frame con picchi molto diversi: dopo la normalizzazione globale il
  // rapporto fra i due deve restare quello di partenza, non appiattirsi a 1.
  const forte = Float32Array.from(sineCycle(64));
  const piano = Float32Array.from(sineCycle(64, 3), (v) => v * 0.25);
  normaliseTable([forte, piano]);

  const peakForte = Math.max(...Array.from(forte, Math.abs));
  const peakPiano = Math.max(...Array.from(piano, Math.abs));
  assert.ok(Math.abs(peakForte - 1) < 1e-6, `picco della tavola ${peakForte}`);
  assert.ok(Math.abs(peakPiano - 0.25) < 1e-6, `il frame piano è stato alzato a ${peakPiano}`);
});

test("normaliseTable toglie la continua frame per frame", () => {
  const a = Float32Array.from(sineCycle(64), (v) => v + 0.3);
  const b = Float32Array.from(sineCycle(64, 2), (v) => v - 0.7);
  normaliseTable([a, b]);
  for (const f of [a, b]) {
    const mean = f.reduce((x, y) => x + y, 0) / f.length;
    assert.ok(Math.abs(mean) < 1e-6, `media ${mean}`);
  }
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

/** DFT diretta O(n²): lenta ma ovviamente corretta, serve da metro di paragone. */
function naiveDft(signal) {
  const n = signal.length;
  const re = new Float64Array(n);
  const im = new Float64Array(n);
  for (let k = 0; k < n; k++)
    for (let i = 0; i < n; i++) {
      const a = (-2 * Math.PI * k * i) / n;
      re[k] += signal[i] * Math.cos(a);
      im[k] += signal[i] * Math.sin(a);
    }
  return { re, im };
}

/** Somma diretta della correlazione circolare, come sopra: riferimento O(n²). */
function naiveCrossCorrelation(a, b) {
  const n = a.length;
  const out = new Float64Array(n);
  for (let s = 0; s < n; s++) for (let i = 0; i < n; i++) out[s] += a[i] * b[(i + s) % n];
  return out;
}

const maxDiff = (a, b) => a.reduce((m, v, i) => Math.max(m, Math.abs(v - b[i])), 0);

test("fft coincide con la DFT diretta su lunghezza potenza di due", () => {
  const x = Float64Array.from({ length: 64 }, (_, i) => Math.sin(i) + 0.3 * Math.cos(3 * i));
  const ref = naiveDft(x);
  const re = Float64Array.from(x);
  const im = new Float64Array(64);
  fft(re, im, false);
  assert.ok(maxDiff(re, ref.re) < 1e-9, `parte reale, errore ${maxDiff(re, ref.re)}`);
  assert.ok(maxDiff(im, ref.im) < 1e-9, `parte immaginaria, errore ${maxDiff(im, ref.im)}`);
});

test("fft coincide con la DFT diretta anche quando n non è potenza di due", () => {
  // 600 è la lunghezza dei cicli AKWF: qui entra in gioco Bluestein.
  const x = Float64Array.from({ length: 600 }, (_, i) => Math.sin((2 * Math.PI * 7 * i) / 600) + 0.2 * Math.sin(i));
  const ref = naiveDft(x);
  const re = Float64Array.from(x);
  const im = new Float64Array(600);
  fft(re, im, false);
  assert.ok(maxDiff(re, ref.re) < 1e-7, `parte reale, errore ${maxDiff(re, ref.re)}`);
  assert.ok(maxDiff(im, ref.im) < 1e-7, `parte immaginaria, errore ${maxDiff(im, ref.im)}`);
});

test("fft inversa riporta il segnale di partenza", () => {
  for (const n of [128, 600]) {
    const x = Float64Array.from({ length: n }, (_, i) => Math.cos(i * 1.7) * (1 + (i % 5)));
    const re = Float64Array.from(x);
    const im = new Float64Array(n);
    fft(re, im, false);
    fft(re, im, true);
    assert.ok(maxDiff(re, x) < 1e-9, `n=${n}, errore ${maxDiff(re, x)}`);
  }
});

test("circularCrossCorrelation coincide con la somma diretta", () => {
  const a = Float32Array.from(sineCycle(32));
  const b = Float32Array.from(sineCycle(32, 3), (v, i) => v + 0.5 * Math.cos(i));
  const fast = circularCrossCorrelation(a, b);
  const slow = naiveCrossCorrelation(a, b);
  assert.ok(maxDiff(fast, slow) < 1e-9, `errore ${maxDiff(fast, slow)}`);
});

test("rotateCycle sposta il ciclo in modo circolare", () => {
  const f = Float32Array.from([0, 1, 2, 3]);
  assert.deepEqual(Array.from(rotateCycle(f, 1)), [1, 2, 3, 0]);
  assert.deepEqual(Array.from(rotateCycle(f, -1)), [3, 0, 1, 2]);
  assert.deepEqual(Array.from(rotateCycle(f, 4)), [0, 1, 2, 3]);
});

test("bestCircularShift ritrova lo spostamento noto fra due cicli", () => {
  const a = sineCycle(256);
  for (const shift of [1, 37, 200]) {
    const b = rotateCycle(a, -shift); // b è a spostata indietro di `shift`
    assert.equal(bestCircularShift(a, b), shift);
  }
});

test("alignFramesToPhase rimette in fase frame ruotati a caso", () => {
  const base = sineCycle(256);
  const frames = [base, rotateCycle(base, 91), rotateCycle(base, 13), rotateCycle(base, 200)];
  const aligned = alignFramesToPhase(frames);
  for (const f of aligned) assert.ok(maxDiff(f, base) < 1e-5, "i frame allineati devono tornare identici");
  assert.ok(Math.min(...adjacentCorrelations(aligned)) > 0.999, "correlazione fra adiacenti");
});

test("alignFramesToPhase non tocca lo spettro di ampiezza dei singoli frame", () => {
  // È la garanzia che rende il passo A gratis: ruotare un ciclo cambia solo la
  // fase, quindi il timbro del frame resta quello.
  const frames = [sineCycle(256), sineCycle(256, 2), sineCycle(256, 5)].map((f, k) =>
    Float32Array.from(f, (v, i) => v + 0.4 * Math.sin((2 * Math.PI * (k + 3) * i) / 256 + k)),
  );
  const before = frames.map(amplitudeSpectrum);
  const aligned = alignFramesToPhase(frames);
  for (let k = 0; k < frames.length; k++) {
    const err = maxDiff(amplitudeSpectrum(aligned[k]), before[k]);
    assert.ok(err < 1e-6, `frame ${k}, errore sullo spettro ${err}`);
  }
});

test("alignFramesToPhase lascia intatti i frame di partenza", () => {
  const original = sineCycle(64);
  const copy = Float32Array.from(original);
  alignFramesToPhase([copy, rotateCycle(copy, 7)]);
  assert.ok(maxDiff(copy, original) === 0, "l'ingresso non va modificato");
});

test("enforceHarmonicPhaseContinuity tiene le ampiezze e recupera l'RMS a metà morph", () => {
  // Frame con lo stesso contenuto armonico ma fasi sparpagliate: mediandone due
  // adiacenti si cancellano a pettine. Siccome le ampiezze sono uguali per tutti
  // i frame, il limite raggiungibile con la sola fase è 0 dB di perdita.
  const n = 256;
  let seed = 12345;
  const rand = () => ((seed = (seed * 1103515245 + 12345) % 2147483648) / 2147483648) * 2 * Math.PI;
  const frames = Array.from({ length: 8 }, () => {
    const out = new Float32Array(n);
    for (let h = 1; h <= 6; h++) {
      const phase = rand();
      for (let i = 0; i < n; i++) out[i] += Math.sin((2 * Math.PI * h * i) / n + phase) / h;
    }
    return out;
  });

  const before = frames.map(amplitudeSpectrum);
  const worstBefore = Math.min(...midMorphRmsLosses(frames));
  const fixed = enforceHarmonicPhaseContinuity(frames);
  const worstAfter = Math.min(...midMorphRmsLosses(fixed));

  for (let k = 0; k < frames.length; k++) {
    const err = maxDiff(amplitudeSpectrum(fixed[k]), before[k]);
    assert.ok(err < 1e-6, `frame ${k}, errore sullo spettro ${err}`);
  }
  assert.ok(worstBefore < -3, `il caso di prova deve partire rotto, era ${worstBefore}`);
  assert.ok(worstAfter > worstBefore + 2, `perdita residua ${worstAfter}, partiva da ${worstBefore}`);
  assert.ok(worstAfter > -0.6, `perdita residua ${worstAfter}`);
});

test("enforceHarmonicPhaseContinuity conserva un'evoluzione di fase già lineare", () => {
  // Uno sweep PWM è esattamente questo: per ogni armonica la fase avanza di un
  // passo costante da un frame all'altro. La retta ai minimi quadrati la
  // ricalca, quindi i frame devono restare quelli.
  const n = 256;
  const frames = Array.from({ length: 8 }, (_, k) => {
    const out = new Float32Array(n);
    for (let h = 1; h <= 6; h++)
      for (let i = 0; i < n; i++) out[i] += Math.sin((2 * Math.PI * h * i) / n + 0.21 * h * k) / h;
    return out;
  });

  const fixed = enforceHarmonicPhaseContinuity(frames);
  for (let k = 0; k < frames.length; k++)
    assert.ok(maxDiff(fixed[k], frames[k]) < 1e-5, `frame ${k} alterato di ${maxDiff(fixed[k], frames[k])}`);
});

test("enforceHarmonicPhaseContinuity lascia intatti i frame di partenza", () => {
  const original = sineCycle(64);
  const copy = Float32Array.from(original);
  enforceHarmonicPhaseContinuity([copy, sineCycle(64, 2)]);
  assert.ok(maxDiff(copy, original) === 0, "l'ingresso non va modificato");
});

test("midMorphRmsLosses vede la cancellazione e non segnala falsi allarmi", () => {
  const a = sineCycle(64);
  assert.ok(Math.abs(midMorphRmsLosses([a, Float32Array.from(a)])[0]) < 1e-6, "frame identici: nessuna perdita");
  const opposta = Float32Array.from(a, (v) => -v);
  assert.ok(midMorphRmsLosses([a, opposta])[0] < -100, "frame in opposizione: cancellazione totale");
});

test("decodeXwt rilegge quello che encodeXwt ha scritto", () => {
  const frames = [Float32Array.from([0.25, -0.5]), Float32Array.from([1, 0])];
  const { frames: back, frameSize } = decodeXwt(encodeXwt(frames, 2));
  assert.equal(frameSize, 2);
  assert.equal(back.length, 2);
  assert.deepEqual(Array.from(back[0]), [0.25, -0.5]);
  assert.deepEqual(Array.from(back[1]), [1, 0]);
});

test("midMorphLossBounds è il limite che la fase da sola non supera", () => {
  // Due frame con lo stesso spettro di ampiezza: il limite è 0 dB e una tavola
  // in fase lo tocca. Con spettri disgiunti il limite scende a −3 dB.
  const a = sineCycle(64);
  const b = rotateCycle(a, 9);
  assert.ok(Math.abs(midMorphLossBounds([a, b])[0]) < 1e-9, "stesso spettro, limite 0 dB");
  assert.ok(midMorphRmsLosses(alignFramesToPhase([a, b]))[0] > -1e-3, "allineati, il limite si tocca");

  const disgiunti = [sineCycle(64, 1), sineCycle(64, 7)];
  assert.ok(Math.abs(midMorphLossBounds(disgiunti)[0] + 3.01) < 0.02, "spettri disgiunti, limite −3 dB");
});

test("equaliseFrameRms comprime l'escursione di (1 − esponente)", () => {
  // Tre frame con lo stesso timbro ma livelli lontanissimi: 40 dB di escursione.
  const frames = [1, 0.1, 0.01].map((gain) => Float32Array.from(sineCycle(64), (v) => v * gain));
  const before = rmsSpreadDb(frames);
  assert.ok(Math.abs(before - 40) < 0.1, `escursione di partenza ${before}`);

  equaliseFrameRms(frames, 0.7);
  const after = rmsSpreadDb(frames);
  assert.ok(Math.abs(after - before * 0.3) < 0.05, `escursione ${after}, attesa ${before * 0.3}`);
});

test("equaliseFrameRms con esponente 1 pareggia, con esponente 0 non tocca niente", () => {
  const build = () => [1, 0.25].map((gain) => Float32Array.from(sineCycle(64), (v) => v * gain));
  assert.ok(rmsSpreadDb(equaliseFrameRms(build(), 1)) < 1e-4, "esponente 1: tutti allo stesso RMS");
  assert.ok(Math.abs(rmsSpreadDb(equaliseFrameRms(build(), 0)) - rmsSpreadDb(build())) < 1e-6, "esponente 0: nessun effetto");
});

test("equaliseFrameRms è un guadagno costante: lo spettro resta proporzionale a sé stesso", () => {
  const frames = [
    Float32Array.from(sineCycle(256), (v, i) => v + 0.4 * Math.sin((2 * Math.PI * 5 * i) / 256 + 1)),
    Float32Array.from(sineCycle(256, 3), (v, i) => 0.2 * v + 0.05 * Math.cos((2 * Math.PI * 9 * i) / 256)),
  ];
  const before = frames.map(amplitudeSpectrum);
  equaliseFrameRms(frames, 0.7);

  for (let k = 0; k < frames.length; k++) {
    const after = amplitudeSpectrum(frames[k]);
    // Il rapporto bin per bin deve essere lo stesso su tutte le armoniche che
    // contano davvero: sotto i −60 dB dal picco c'è solo il rumore dei float32.
    const soglia = Math.max(...before[k]) * 1e-3;
    const bins = [];
    for (let i = 1; i < before[k].length; i++) if (before[k][i] > soglia) bins.push(after[i] / before[k][i]);
    const spread = Math.max(...bins) - Math.min(...bins);
    assert.ok(spread < 1e-5, `frame ${k}, il guadagno varia di ${spread} fra le armoniche`);
  }
});
