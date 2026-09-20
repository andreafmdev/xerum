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
import { adjacentCorrelations, encodeXwt, midMorphRmsLosses, normaliseTable, selectFrames } from "./wavetable-dsp.mjs";

/** I 64 frame che Xerum si aspetta in ogni .xwt. */
const TARGET_FRAMES = 64;

/**
 * Le sette tavole del pack, riconosciute per sha1 dello stream grezzo. Lo slug e'
 * anche il `value` dell'opzione di wtIndex e il nome del file .xwt: cambiarlo qui
 * senza cambiarlo in parameters.json fa fallire lo static_assert in ParamCollect.h.
 */
export const KNOWN_TABLES = {
  "56780dc5": "retro-racing",
  "40bbc013": "retro-spindizzy",
  d15054da: "retro-ggsisters",
  "8490d094": "retro-commando",
  b5167ba8: "retro-uridium-pad",
  fa38ca0c: "retro-leaderboard",
  "34b742bb": "retro-uridium",
};

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
    const slug = KNOWN_TABLES[sha1];
    if (!slug) {
      console.warn(`  ${sha1}: ${frames.length} frame, nessuno slug noto — saltata (da ${sources[0]})`);
      continue;
    }

    const before = { frames: frames.length, peak: peakOf(frames), rms: rmsOf(frames) };
    const reduced = normaliseTable(selectFrames(frames, TARGET_FRAMES));
    const after = { frames: reduced.length, peak: peakOf(reduced), rms: rmsOf(reduced) };

    // Picco e RMS sono globali: non possono mostrare una cancellazione che avviene a meta'
    // crossfade fra due frame vicini, perche' quei due numeri si fanno su tutta la tavola.
    // La correlazione minima e la peggiore perdita RMS a meta' morph sono la verifica per
    // frame adiacente che la spec chiedeva al punto 8, a giustificare la scelta di non
    // riallineare le fasi di queste sette tavole (solo diagnostica: i dati scritti nel .xwt
    // restano quelli di `reduced`, invariati).
    const minCorrelation = Math.min(...adjacentCorrelations(reduced));
    const worstMidMorphLoss = Math.min(...midMorphRmsLosses(reduced));

    console.log(
      `  ${slug} (${sha1}): ${before.frames} → ${after.frames} frame, ` +
        `picco ${before.peak.toFixed(3)} → ${after.peak.toFixed(3)}, ` +
        `rms ${before.rms.toFixed(3)} → ${after.rms.toFixed(3)}, ` +
        `correlazione minima fra frame adiacenti ${minCorrelation.toFixed(3)}, ` +
        `peggiore perdita RMS a meta' morph ${worstMidMorphLoss.toFixed(2)} dB, ` +
        `da ${sources.length} preset (${sources[0]})`,
    );

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
