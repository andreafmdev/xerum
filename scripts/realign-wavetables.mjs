#!/usr/bin/env node
// Ri-processa sul posto le tavole .xwt già committate: allineamento di fase fra
// i frame e normalizzazione globale. Niente rete, legge e riscrive solo
// Resources/wavetables.
//
// Uso: node scripts/realign-wavetables.mjs [--dry-run] [--keep-frames] [--rms-exponent=N]
//   --dry-run          stampa le metriche senza riscrivere niente
//   --keep-frames      si ferma ai passi sulla fase, senza reinterpolare i frame
//   --rms-exponent=N   quanto pareggiare l'RMS fra i frame (default 0.7, 0 = niente)
//
// Attenzione: l'equalizzazione di RMS non è idempotente, due passaggi comprimono
// due volte. Lo script va dato in pasto ai .xwt appena generati, non a sé stesso.
//
// Perché serve: le tavole erano state costruite normalizzando ogni frame al suo
// picco (quindi con RMS molto diversi fra frame) e fondendo onde AKWF non
// allineate fra loro, che nel crossfade si cancellavano a pettine. Il risultato
// era che muovere Position cambiava volume e timbro in modo imprevedibile.
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  adjacentCorrelations,
  alignFramesToPhase,
  decodeXwt,
  encodeXwt,
  enforceHarmonicPhaseContinuity,
  fundamentalPhaseDeviation,
  equaliseFrameRms,
  midMorphLossBounds,
  midMorphRmsLosses,
  normaliseTable,
  rmsSpreadDb,
  selectFrames,
} from "./wavetable-dsp.mjs";

const TABLES = ["basic", "saws", "grit", "vocal", "bells", "pwm"];

// Soglie di accettazione del morph: perdita a metà strada fra due frame e
// somiglianza fra frame adiacenti.
const LOSS_TARGET_DB = -1.0;
const CORRELATION_TARGET = 0.9;

// Quanto si pareggia l'RMS fra i frame. A 1.0 sparirebbe ogni differenza di
// livello, anche quelle giuste: in uno sweep PWM l'impulso stretto deve suonare
// più piano. A 0.7 l'escursione in dB scende al 30% e il resto è musica.
const DEFAULT_RMS_EXPONENT = 0.7;

// Quante ancore provare nel passo C, dalla meno invasiva in giù.
const ANCHOR_CANDIDATES = [48, 40, 32, 24, 16, 12, 8];

const mean = (values) => values.reduce((a, b) => a + b, 0) / values.length;

/**
 * Passo C: ricostruisce i 64 frame interpolando fra `count` ancore prese dalla
 * tavola già allineata.
 *
 * Serve perché per le famiglie AKWF con almeno 64 onde la pipeline prendeva
 * esattamente 64 onde e `selectFrames` finiva per non interpolare niente
 * (t sempre 0): i frame erano 64 onde diverse messe in fila, non un morph.
 * Ora che le ancore sono in fase il crossfade lineare non si cancella più, e i
 * frame intermedi diventano passaggi graduali invece che salti.
 */
function rebuildFromAnchors(frames, count) {
  const last = frames.length - 1;
  const anchors = Array.from({ length: count }, (_, i) => frames[Math.round((last * i) / (count - 1))]);
  return selectFrames(anchors, frames.length);
}

/** Le tre metriche del morph più l'escursione di RMS fra i frame. */
function measure(frames) {
  return {
    phaseDeviation: fundamentalPhaseDeviation(frames),
    correlation: mean(adjacentCorrelations(frames)),
    worstLoss: Math.min(...midMorphRmsLosses(frames)),
    meanLoss: mean(midMorphRmsLosses(frames)),
    rmsSpread: rmsSpreadDb(frames),
  };
}

function format(m) {
  return [
    `fase ${m.phaseDeviation.toFixed(1).padStart(5)}°`,
    `corr ${m.correlation.toFixed(3)}`,
    `perdita peggiore ${m.worstLoss.toFixed(2).padStart(6)} dB`,
    `media ${m.meanLoss.toFixed(2).padStart(6)} dB`,
    `escursione RMS ${m.rmsSpread.toFixed(2).padStart(5)} dB`,
  ].join("  |  ");
}

/** Le soglie di accettazione, sempre valutate sulla tavola finita. */
function meetsTargets(m) {
  return m.worstLoss >= LOSS_TARGET_DB && m.correlation > CORRELATION_TARGET;
}

const clone = (frames) => frames.map((frame) => Float32Array.from(frame));

/**
 * Applica su una copia gli ultimi due passi — equalizzazione parziale dell'RMS e
 * normalizzazione globale — e misura il risultato.
 *
 * Si lavora su una copia perché ogni decisione (serve il passo B? serve il C?)
 * va presa guardando la tavola come sarà davvero: l'equalizzazione cambia i
 * livelli relativi fra frame adiacenti, quindi sposta anche la perdita a metà
 * morph. Deciderla sulle metriche intermedie porterebbe fuori strada.
 */
function finalise(frames, options) {
  const out = clone(frames);
  if (options.rmsExponent > 0) equaliseFrameRms(out, options.rmsExponent);
  normaliseTable(out);
  return { frames: out, metrics: measure(out) };
}

function processTable(id, path, options) {
  const { frames, frameSize } = decodeXwt(readFileSync(path));
  const before = measure(frames);

  // Passo A: allineamento a fase lineare. Ruotare un ciclo è solo un offset di
  // fase, quindi lo spettro di ampiezza di ogni frame resta quello di prima.
  let treated = alignFramesToPhase(frames);
  let final = finalise(treated, options);
  const afterA = final.metrics;

  // Passo B: solo se il passo A non basta, perché questo i frame li altera.
  const usedStepB = !meetsTargets(afterA);
  if (usedStepB) {
    treated = enforceHarmonicPhaseContinuity(treated);
    final = finalise(treated, options);
  }
  const afterB = final.metrics;

  // Quello che resta dopo A e B non è più un problema di fase: è la distanza
  // fra gli spettri di ampiezza dei frame adiacenti. Sotto questo limite non si
  // scende comunque, per quanto si lavori sulla fase.
  const bound = Math.min(...midMorphLossBounds(final.frames));

  // Passo C: se il limite stesso è sopra soglia, gli spettri adiacenti sono
  // troppo lontani e l'unica strada è rimettere i passaggi intermedi che la
  // pipeline non ha mai interpolato.
  let anchors = 0;
  if (!options.keepFrames && !meetsTargets(final.metrics)) {
    for (const count of ANCHOR_CANDIDATES) {
      const candidate = rebuildFromAnchors(treated, count);
      const rebuilt = finalise(candidate, options);
      if (meetsTargets(rebuilt.metrics)) {
        treated = candidate;
        final = rebuilt;
        anchors = count;
        break;
      }
    }
  }

  const after = final.metrics;

  console.log(`\n${id}`);
  console.log(`  prima   ${format(before)}`);
  console.log(`  passo A ${format(afterA)}`);
  if (usedStepB) console.log(`  passo B ${format(afterB)}`);
  console.log(`  limite di fase: perdita peggiore ${bound.toFixed(2)} dB`);
  if (anchors) console.log(`  passo C ${anchors} ancore interpolate su ${frames.length} frame`);
  console.log(`  finale  ${format(after)}`);
  if (!meetsTargets(after)) console.log("  ATTENZIONE: le soglie non sono state raggiunte");

  if (!options.dryRun) writeFileSync(path, encodeXwt(final.frames, frameSize));
  return { id, before, after, usedStepB, anchors, bound };
}

/** Legge --rms-exponent=N, con il default se non c'è. */
function readExponent() {
  const arg = process.argv.find((a) => a.startsWith("--rms-exponent="));
  if (arg === undefined) return DEFAULT_RMS_EXPONENT;
  const value = Number(arg.slice("--rms-exponent=".length));
  if (!Number.isFinite(value) || value < 0 || value > 1) throw new Error(`--rms-exponent fuori da 0..1: ${arg}`);
  return value;
}

function main() {
  const options = {
    dryRun: process.argv.includes("--dry-run"),
    // Con --keep-frames si fermano i passi A e B e si tengono tutti i frame
    // distinti, anche dove il morph resta a scalini.
    keepFrames: process.argv.includes("--keep-frames"),
    rmsExponent: readExponent(),
  };
  const outDir = resolve(dirname(fileURLToPath(import.meta.url)), "..", "Resources/wavetables");
  const results = TABLES.map((id) => processTable(id, resolve(outDir, `${id}.xwt`), options));

  console.log(`\nriepilogo (prima → dopo), esponente RMS ${options.rmsExponent}`);
  console.log("| tavola | dev.std fase | correlazione | perdita a metà morph | escursione RMS | passi |");
  console.log("|---|---|---|---|---|---|");
  for (const r of results)
    console.log(
      `| ${r.id} | ${r.before.phaseDeviation.toFixed(1)}° → ${r.after.phaseDeviation.toFixed(1)}° ` +
        `| ${r.before.correlation.toFixed(3)} → ${r.after.correlation.toFixed(3)} ` +
        `| ${r.before.worstLoss.toFixed(2)} dB → ${r.after.worstLoss.toFixed(2)} dB ` +
        `| ${r.before.rmsSpread.toFixed(2)} dB → ${r.after.rmsSpread.toFixed(2)} dB ` +
        `| A${r.usedStepB ? "+B" : ""}${r.anchors ? `+C(${r.anchors})` : ""}${options.rmsExponent > 0 ? "+D" : ""} |`,
    );
  if (options.dryRun) console.log("\n--dry-run: nessun file è stato riscritto.");
}

main()
