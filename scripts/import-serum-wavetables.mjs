#!/usr/bin/env node
// Estrae le wavetable custom incorporate nei preset .fxp di Serum e le scrive come .xwt.
// Uso: node scripts/import-serum-wavetables.mjs <cartella-fxp> [--out Resources/wavetables]
//                                               [--min-frames 8] [--dry-run]
//
// Non tocca i parametri del preset: lo stato Serum e' un altro strumento, con altri
// oscillatori e un'altra matrice, e tradurlo non e' questo lavoro.
import { createHash } from "node:crypto";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { framesFromStream, parseFxp, splitZlibStreams, SERUM_FRAME_SIZE } from "./serum-fxp.mjs";
import {
  adjacentCorrelations,
  alignFramesToPhase,
  encodeXwt,
  enforceHarmonicPhaseContinuity,
  equaliseFrameRms,
  midMorphRmsLosses,
  normaliseTable,
  rmsSpreadDb,
  selectFrames,
} from "./wavetable-dsp.mjs";

/** I 64 frame che Xerum si aspetta in ogni .xwt. */
const TARGET_FRAMES = 64;

/**
 * Le cinque tavole del pack che si spediscono, riconosciute per sha1 dello stream grezzo. Lo
 * slug e' anche il `value` dell'opzione di wtIndex e il nome del file .xwt: cambiarlo qui senza
 * cambiarlo in parameters.json fa fallire lo static_assert in ParamCollect.h.
 *
 * Il pack ne conteneva sette: due erano la stessa tavola salvata due volte con arrotondamenti
 * float diversi (vedi KNOWN_DUPLICATE_TABLES sotto) e non si spediscono due volte.
 */
export const KNOWN_TABLES = {
  "56780dc5": "retro-racing",
  d15054da: "retro-ggsisters",
  "8490d094": "retro-commando",
  b5167ba8: "retro-uridium-pad",
  fa38ca0c: "retro-leaderboard",
};

/**
 * Due sha1 del pack sono la STESSA tavola di una gia' in KNOWN_TABLES, salvata due volte con
 * arrotondamenti float diversi — la deduplica per sha1 dello stream grezzo non li vede perche'
 * gli stream sorgente differiscono davvero, di rumore (`maxDiff` e' la differenza massima
 * campione per campione fra i due .xwt gia' scritti, misurata due volte: ~1 ULP float32 per
 * `retro-spindizzy`/`retro-racing`).
 *
 * Restano fuori da KNOWN_TABLES di proposito, ma non devono sparire in silenzio dietro
 * l'avviso generico "nessuno slug noto": chi rilancia l'importatore deve leggere che sono
 * duplicati noti e scartati apposta, non un buco nella tabella.
 */
export const KNOWN_DUPLICATE_TABLES = {
  "40bbc013": { slug: "retro-spindizzy", duplicateOf: "retro-racing", ofSha1: "56780dc5", maxDiff: 5.96e-8 },
  "34b742bb": { slug: "retro-uridium", duplicateOf: "retro-leaderboard", ofSha1: "fa38ca0c", maxDiff: 4.77e-7 },
};

/**
 * Come va trattato uno sha1 trovato nel pack: una tavola nota da scrivere (`known`), un
 * duplicato noto di una tavola gia' spedita da scartare (`duplicate`), o uno sha1 mai visto
 * (`unknown`). Pura, cosi' i test possono coprire il caso `duplicate` senza leggere .fxp veri.
 */
export function classifyTable(sha1) {
  const known = KNOWN_TABLES[sha1];
  if (known) return { kind: "known", slug: known };

  const duplicate = KNOWN_DUPLICATE_TABLES[sha1];
  if (duplicate) return { kind: "duplicate", ...duplicate };

  return { kind: "unknown" };
}

/**
 * Le due tavole misurate fuori soglia sul morph — `retro-commando` in antifase fra frame
 * adiacenti (correlazione minima -0.225), `retro-uridium-pad` di fatto scorrelata (0.013).
 * Solo per queste due si applica la pipeline di `scripts/fetch-wavetables.mjs` — allineamento
 * di fase, continuita' di fase per armonica dove serve, equalizzazione parziale dell'RMS — ed
 * e' un opt-in per tavola: le altre tre (piu' `retro-racing`, esplicitamente lasciata com'e')
 * restano fuori da questo insieme e si rigenerano bit-identiche a quelle gia' nel repo.
 */
export const REALIGN_TABLES = new Set(["retro-commando", "retro-uridium-pad"]);

/** Soglia e fattore della pipeline di riallineamento: gli stessi di scripts/fetch-wavetables.mjs. */
const REALIGN_LOSS_TARGET_DB = -1.0;
const REALIGN_RMS_EXPONENT = 0.7;

function finishRealigned(frames) {
  equaliseFrameRms(frames, REALIGN_RMS_EXPONENT);
  normaliseTable(frames);
  return frames;
}

/**
 * Applica a `frames` la stessa pipeline di `scripts/fetch-wavetables.mjs`, nello stesso
 * ordine: allineamento di fase (passo A), riduzione/interpolazione a TARGET_FRAMES,
 * equalizzazione parziale dell'RMS e normalizzazione globale. Se il passo A non basta a
 * portare la perdita peggiore a meta' morph sopra la soglia, si applica anche la continuita'
 * di fase per armonica (passo B) sui frame allineati, e si ripetono riduzione ed
 * equalizzazione — esattamente come in `fetch-wavetables.mjs`. Non va oltre: nessun passo di
 * reinterpolazione ulteriore, nessun filtro inventato.
 *
 * Restituisce i frame finiti e se il passo B e' stato usato; l'ingresso non viene toccato.
 */
export function realignSerumTable(frames) {
  const aligned = alignFramesToPhase(frames);
  let reduced = finishRealigned(selectFrames(aligned, TARGET_FRAMES));
  let usedStepB = false;

  if (Math.min(...midMorphRmsLosses(reduced)) < REALIGN_LOSS_TARGET_DB) {
    const continuous = enforceHarmonicPhaseContinuity(aligned);
    reduced = finishRealigned(selectFrames(continuous, TARGET_FRAMES));
    usedStepB = true;
  }

  return { frames: reduced, usedStepB };
}

/** Le tre metriche del morph che si stampano prima e dopo il riallineamento. */
function morphMetrics(frames) {
  return {
    minCorrelation: Math.min(...adjacentCorrelations(frames)),
    worstLoss: Math.min(...midMorphRmsLosses(frames)),
    rmsSpread: rmsSpreadDb(frames),
  };
}

function formatMorphMetrics(m) {
  return (
    `correlazione minima ${m.minCorrelation.toFixed(3)}, ` +
    `peggiore perdita a meta' morph ${m.worstLoss.toFixed(2)} dB, ` +
    `escursione RMS ${m.rmsSpread.toFixed(2)} dB`
  );
}

/** Vero se `m` raggiunge il criterio di riuscita del riallineamento: correlazione minima
    positiva e perdita a meta' morph non peggiore della soglia. */
function meetsRealignTarget(m) {
  return m.minCorrelation > 0 && m.worstLoss >= REALIGN_LOSS_TARGET_DB;
}

/**
 * Raccoglie le tavole distinte fra i file dati. Lo stream 0 e' sempre lo stato Serum
 * e non viene guardato; quelli dopo sono tavole quando la lunghezza torna e i frame
 * bastano. Sotto `minFrames` si tratta di onde singole, non di morph: si scartano.
 */
export function collectWavetables(files, minFrames) {
  const found = new Map();

  for (const { name, buffer } of files) {
    const { chunk } = parseFxp(buffer);

    for (const stream of splitZlibStreams(chunk).slice(1)) {
      const frames = framesFromStream(stream, SERUM_FRAME_SIZE);
      if (frames === null || frames.length < minFrames) continue;

      const sha1 = createHash("sha1").update(stream).digest("hex").slice(0, 8);
      const entry = found.get(sha1);
      if (entry) entry.sources.push(name);
      else found.set(sha1, { sha1, frames, sources: [name] });
    }
  }

  return found;
}

function parseArgs(argv) {
  const positional = [];
  const opts = { out: "Resources/wavetables", minFrames: 8, dryRun: false };

  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--out") opts.out = argv[++i];
    else if (argv[i] === "--min-frames") opts.minFrames = Number(argv[++i]);
    else if (argv[i] === "--dry-run") opts.dryRun = true;
    else positional.push(argv[i]);
  }

  if (positional.length !== 1) throw new Error("uso: import-serum-wavetables.mjs <cartella-fxp> [--out DIR] [--min-frames N] [--dry-run]");
  if (!Number.isFinite(opts.minFrames) || opts.minFrames < 1) throw new Error(`--min-frames non valido: ${opts.minFrames}`);
  return { dir: positional[0], ...opts };
}

const peakOf = (frames) => frames.reduce((p, f) => f.reduce((q, s) => Math.max(q, Math.abs(s)), p), 0);
const rmsOf = (frames) => {
  let sum = 0;
  let n = 0;
  for (const f of frames) for (const s of f) { sum += s * s; n++; }
  return Math.sqrt(sum / n);
};

function main(argv) {
  const { dir, out, minFrames, dryRun } = parseArgs(argv);
  const root = resolve(dirname_(), "..");
  const outDir = resolve(root, out);

  const names = readdirSync(dir).filter((n) => n.toLowerCase().endsWith(".fxp")).sort();
  if (names.length === 0) throw new Error(`nessun .fxp in ${dir}`);

  const files = names.map((n) => ({ name: n, buffer: readFileSync(resolve(dir, n)) }));
  const found = collectWavetables(files, minFrames);
  console.log(`${names.length} preset letti, ${found.size} tavole distinte sopra i ${minFrames} frame`);

  const written = [];
  for (const { sha1, frames, sources } of found.values()) {
    const classified = classifyTable(sha1);

    if (classified.kind === "duplicate") {
      console.warn(
        `  ${sha1}: duplicato noto di \`${classified.duplicateOf}\` (${classified.ofSha1}) — stessa tavola salvata due volte nel ` +
          `pack con arrotondamenti float diversi (differenza massima campione per campione ${classified.maxDiff.toExponential(2)}), ` +
          `scartato di proposito e non spedito come \`${classified.slug}\` (da ${sources[0]})`,
      );
      continue;
    }
    if (classified.kind === "unknown") {
      console.warn(`  ${sha1}: ${frames.length} frame, nessuno slug noto — saltata (da ${sources[0]})`);
      continue;
    }

    const slug = classified.slug;
    const before = { frames: frames.length, peak: peakOf(frames), rms: rmsOf(frames) };

    let reduced;
    let usedStepB = false;
    let realignBefore = null;
    const realign = REALIGN_TABLES.has(slug);
    if (realign) {
      realignBefore = morphMetrics(normaliseTable(selectFrames(frames, TARGET_FRAMES)));
      const result = realignSerumTable(frames);
      reduced = result.frames;
      usedStepB = result.usedStepB;
    } else {
      reduced = normaliseTable(selectFrames(frames, TARGET_FRAMES));
    }

    const after = { frames: reduced.length, peak: peakOf(reduced), rms: rmsOf(reduced) };
    const afterMorph = morphMetrics(reduced);

    if (realign) {
      console.log(
        `  ${slug} (${sha1}): riallineata (passo A${usedStepB ? " + continuita' di fase" : ""}), ` +
          `${before.frames} → ${after.frames} frame, da ${sources.length} preset (${sources[0]})`,
      );
      console.log(`    prima  — ${formatMorphMetrics(realignBefore)}`);
      console.log(`    dopo   — ${formatMorphMetrics(afterMorph)}`);
      if (!meetsRealignTarget(afterMorph))
        console.warn(
          `    ATTENZIONE: ${slug} non raggiunge il criterio di riuscita (correlazione minima positiva e perdita a meta' morph >= ` +
            `${REALIGN_LOSS_TARGET_DB} dB) nemmeno dopo la pipeline. Nessun altro passo e' stato tentato: i numeri sopra sono il risultato finale.`,
        );
    } else {
      // Picco e RMS sono globali: non possono mostrare una cancellazione che avviene a meta'
      // crossfade fra due frame vicini, perche' quei due numeri si fanno su tutta la tavola.
      // La correlazione minima e la peggiore perdita RMS a meta' morph sono la verifica per
      // frame adiacente (solo diagnostica per queste tre: i dati scritti nel .xwt restano
      // quelli di `reduced`, invariati — vedi REALIGN_TABLES sopra per le due eccezioni).
      console.log(
        `  ${slug} (${sha1}): ${before.frames} → ${after.frames} frame, ` +
          `picco ${before.peak.toFixed(3)} → ${after.peak.toFixed(3)}, ` +
          `rms ${before.rms.toFixed(3)} → ${after.rms.toFixed(3)}, ` +
          `correlazione minima fra frame adiacenti ${afterMorph.minCorrelation.toFixed(3)}, ` +
          `peggiore perdita RMS a meta' morph ${afterMorph.worstLoss.toFixed(2)} dB, ` +
          `da ${sources.length} preset (${sources[0]})`,
      );
    }

    if (!dryRun) writeFileSync(resolve(outDir, `${slug}.xwt`), encodeXwt(reduced, SERUM_FRAME_SIZE));
    written.push({ slug, sha1, sources, sourceFrames: before.frames });
  }

  const missing = Object.entries(KNOWN_TABLES).filter(([sha1]) => !found.has(sha1));
  if (missing.length > 0) throw new Error(`tavole attese e non trovate: ${missing.map(([s, n]) => `${n} (${s})`).join(", ")}`);

  console.log(dryRun ? "--dry-run: nessun file scritto" : `${written.length} file .xwt scritti in ${outDir}`);
  console.log("\nRighe per CREDITS.md:\n");
  for (const w of written)
    console.log(`| \`${w.slug}\` | ${w.sourceFrames} frame | ${w.sources.length} preset, es. \`${basename(w.sources[0], ".fxp")}\` |`);
}

function dirname_() {
  return resolve(fileURLToPath(import.meta.url), "..");
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`import-serum-wavetables: ${error.message}`);
    process.exit(1);
  }
}
