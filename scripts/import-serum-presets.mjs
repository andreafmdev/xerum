#!/usr/bin/env node
// Genera preset Xerum dai .fxp di un pack Serum, prendendo dal file solo cio' che si legge
// senza conoscere lo struct opaco di Serum: il nome, il prefisso di categoria e la wavetable
// incorporata. I valori dei parametri NON vengono dal preset Serum — vengono da un modello
// per categoria piu' uno scarto deterministico ricavato dal nome. Vedi l'intestazione del
// file generato per il perche'.
import { createHash } from "node:crypto";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { framesFromStream, parseFxp, splitZlibStreams, SERUM_FRAME_SIZE } from "./serum-fxp.mjs";
import { classifyTable } from "./import-serum-wavetables.mjs";
import { decodeXwt } from "./wavetable-dsp.mjs";

/** Prefisso del pack -> categoria di Xerum (le stesse di CATEGORIES in WebUI/src/synth/presets.ts). */
export const CATEGORY_BY_PREFIX = {
  BS: "Bass",
  SUB: "Bass",
  LD: "Lead",
  PD: "Pad",
  KY: "Keys",
  PL: "Pluck",
  FX: "FX",
  DR: "FX",
  SQ: "Seq",
  INIT: "User",
};

/** La categoria dal prefisso del nome file, o null se il prefisso non e' fra quelli noti. */
export function categoryForFile(filename) {
  const dash = filename.indexOf("-");
  if (dash <= 0) return null;
  return CATEGORY_BY_PREFIX[filename.slice(0, dash)] ?? null;
}

/**
 * Il nome leggibile del preset a partire dal nome che Serum scrive nell'header FPCh
 * (`BS-RacingDestructionKit5`, a volte senza prefisso come `neochip-base1`).
 *
 * Toglie il prefisso solo se e' uno di quelli noti — `neochip-base1` non ne ha uno — separa le
 * parole attaccate in CamelCase tenendo insieme gli acronimi (`GGSisters` -> `GG Sisters`),
 * stacca le cifre finali e scarta la punteggiatura che il pack usa per marcare le varianti
 * (`...Kit6-`, `...Kit8+`): due varianti cosi' arrivano allo stesso nome, e a separarle ci
 * pensa la deduplica.
 */
export function presetNameFrom(rawName) {
  const prefix = /^([A-Z]+)-/.exec(rawName);
  let s = prefix && prefix[1] in CATEGORY_BY_PREFIX ? rawName.slice(prefix[0].length) : rawName;

  s = s.replace(/[-_]+/g, " ").replace(/[^A-Za-z0-9 ]+/g, "");
  s = s
    .replace(/([A-Z]+)([A-Z][a-z])/g, "$1 $2")
    .replace(/([a-z])([A-Z])/g, "$1 $2")
    .replace(/([A-Za-z])(\d)/g, "$1 $2")
    .replace(/(\d)([A-Za-z])/g, "$1 $2");

  return s
    .split(/\s+/)
    .filter(Boolean)
    .map((w) => w[0].toUpperCase() + w.slice(1))
    .join(" ");
}

/**
 * Rende unico ogni nome, numerando le ripetizioni dalla seconda in poi. `taken` sono i nomi
 * gia' occupati da altri preset — quelli scritti a mano in presets.json — che il pack non deve
 * poter sovrascrivere: il menu li mostra tutti nella stessa lista.
 */
export function dedupeNames(names, taken = []) {
  const used = new Set(taken);

  return names.map((name) => {
    if (!used.has(name)) {
      used.add(name);
      return name;
    }
    for (let n = 2; ; n++) {
      const candidate = `${name} ${n}`;
      if (!used.has(candidate)) {
        used.add(candidate);
        return candidate;
      }
    }
  });
}

/**
 * `count` numeri in [0,1) ricavati dal solo `name`: lo scarto che distingue due preset della
 * stessa categoria con la stessa wavetable, che altrimenti sarebbero identici. Deterministico
 * apposta — rilanciare l'importatore non deve cambiare un preset gia' spedito.
 */
export function seededUnits(name, count) {
  const digest = createHash("sha1").update(name).digest();
  let state = digest.readUInt32BE(0) || 1;

  const units = [];
  for (let i = 0; i < count; i++) {
    // xorshift32: basta a spargere, e sta in tre righe leggibili.
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    state >>>= 0;
    units.push(state / 0x100000000);
  }
  return units;
}

/**
 * Un modello per categoria piu' l'ampiezza dello scarto ammesso su ciascun parametro mobile.
 *
 * `base` sono valori normalizzati 0..1 con la stessa convenzione di presets.json, ricalcati
 * sui dodici preset scritti a mano. `jitter` e' lo scarto massimo in piu' o in meno che il
 * nome puo' produrre; `wtpos` e' l'intervallo dentro cui la posizione viene scelta, perche' li'
 * il modello non ha un valore giusto da cui scostarsi.
 *
 * `level`, `drive` e `volume` non compaiono mai in `jitter`: sono i tre che decidono se il
 * preset accende il clipper, e PresetGainStagingTests li suona davvero.
 */
export const TEMPLATES = {
  Bass: {
    base: { oct: 0.3333, ftype: "LP", slope: "24", cutoff: 0.4, res: 0.3, drive: 0.3464, keytrk: 0.6,
            att: 0.0, dec: 0.25, sus: 0.72, rel: 0.08, level: 0.75, volume: 0.8 },
    jitter: { cutoff: 0.06, res: 0.1, dec: 0.06, rel: 0.04 },
    wtpos: [0.0, 0.5],
  },
  Lead: {
    base: { ftype: "LP", slope: "24", cutoff: 0.72, res: 0.25, drive: 0.3162, keytrk: 0.5,
            att: 0.022, dec: 0.25, sus: 0.86, rel: 0.11, level: 0.78, volume: 0.8 },
    jitter: { cutoff: 0.07, res: 0.08, dec: 0.06, rel: 0.04 },
    wtpos: [0.0, 1.0],
  },
  Pad: {
    base: { ftype: "LP", slope: "24", cutoff: 0.55, res: 0.2, drive: 0.0, keytrk: 0.4,
            att: 0.35, dec: 0.47, sus: 0.78, rel: 0.52, level: 0.7, volume: 0.7 },
    jitter: { cutoff: 0.06, res: 0.06, dec: 0.08, rel: 0.08 },
    wtpos: [0.2, 0.8],
  },
  Keys: {
    base: { ftype: "LP", slope: "24", cutoff: 0.66, res: 0.14, drive: 0.0, keytrk: 0.65,
            att: 0.015, dec: 0.3, sus: 0.28, rel: 0.22, level: 0.8, volume: 0.8 },
    jitter: { cutoff: 0.07, res: 0.06, dec: 0.08, rel: 0.06 },
    wtpos: [0.0, 0.8],
  },
  Pluck: {
    base: { ftype: "LP", slope: "24", cutoff: 0.73, res: 0.33, drive: 0.3162, keytrk: 0.7,
            att: 0.0, dec: 0.16, sus: 0.0, rel: 0.08, level: 0.82, volume: 0.8 },
    jitter: { cutoff: 0.07, res: 0.09, dec: 0.05, rel: 0.03 },
    wtpos: [0.0, 0.7],
  },
  FX: {
    base: { ftype: "LP", slope: "24", cutoff: 0.5, res: 0.5, drive: 0.0, keytrk: 0.3,
            att: 0.45, dec: 0.55, sus: 0.7, rel: 0.64, level: 0.68, volume: 0.7 },
    jitter: { cutoff: 0.1, res: 0.12, dec: 0.08, rel: 0.08 },
    wtpos: [0.0, 1.0],
  },
  Seq: {
    base: { ftype: "LP", slope: "24", cutoff: 0.7, res: 0.38, drive: 0.2887, keytrk: 0.6,
            att: 0.0, dec: 0.14, sus: 0.05, rel: 0.07, level: 0.8, volume: 0.8,
            arpOn: 1, arpMode: "Up", arpRate: 0.5, arpGate: 0.5 },
    jitter: { cutoff: 0.07, res: 0.1, dec: 0.04, rel: 0.03, arpGate: 0.15 },
    wtpos: [0.0, 0.9],
  },
  User: {
    base: { ftype: "LP", slope: "24", cutoff: 0.6, res: 0.2, drive: 0.0, keytrk: 0.5,
            att: 0.02, dec: 0.3, sus: 0.8, rel: 0.16, level: 0.78, volume: 0.8 },
    jitter: { cutoff: 0.05, res: 0.05, dec: 0.05, rel: 0.05 },
    wtpos: [0.0, 1.0],
  },
};

const clamp01 = (x) => Math.min(1, Math.max(0, x));
const round4 = (x) => Math.round(x * 1e4) / 1e4;

/**
 * I valori del preset: il modello della categoria, la wavetable letta dal file e lo scarto
 * ricavato dal nome. Nessuno di questi numeri viene dallo stato Serum — quello struct non e'
 * documentato e non viene nemmeno aperto.
 */
export function buildPresetValues(category, wavetableSlug, name, levelScale = 1) {
  const template = TEMPLATES[category];
  if (!template) throw new Error(`nessun modello per la categoria ${category}`);

  const mobile = Object.keys(template.jitter);
  const units = seededUnits(name, mobile.length + 1);

  const values = { wtIndex: wavetableSlug, wtpos: 0, ...template.base };
  values.wtpos = round4(template.wtpos[0] + units[0] * (template.wtpos[1] - template.wtpos[0]));

  mobile.forEach((id, i) => {
    values[id] = round4(clamp01(template.base[id] + (units[i + 1] * 2 - 1) * template.jitter[id]));
  });

  values.level = round4(clamp01(template.base.level * levelScale));
  return values;
}

/**
 * Di quanto va scalato il `level` di un preset che usa una tavola di RMS `tableRms`, preso
 * `referenceRms` come la tavola su cui i modelli sono tarati.
 *
 * Serve perche' le tavole del pack sono molto piu' calde delle sei originali — `retro-racing`
 * ha 4,2 dB di RMS in piu' di `saws` — e il livello dei modelli e' stato scelto guardando i
 * preset scritti a mano, che quelle tavole non le usano. Senza questa correzione otto preset
 * del pack accendevano il soft clipper su un accordo di quattro note (PresetGainStagingTests).
 */
export function levelScaleFromRms(tableRms, referenceRms) {
  return referenceRms / tableRms;
}

/** Sotto questa soglia lo stream non e' una tavola di morph ma un'onda sola: l'importatore
    delle wavetable la scarta, e un preset non puo' puntare a una tavola che non esiste. */
const MIN_FRAMES = 8;

/** Lo slug della tavola incorporata nel file, o null se non ce n'e' una fra quelle importate.
    Un duplicato noto punta alla tavola davvero spedita: e' la stessa tavola, salvata due volte
    dal pack con arrotondamenti float diversi. */
function wavetableSlugOf(buffer, classify) {
  for (const stream of splitZlibStreams(parseFxp(buffer).chunk).slice(1)) {
    const frames = framesFromStream(stream, SERUM_FRAME_SIZE);
    if (frames === null || frames.length < MIN_FRAMES) continue;

    const sha1 = createHash("sha1").update(stream).digest("hex").slice(0, 8);
    const classified = classify(sha1);
    if (classified.kind === "known") return classified.slug;
    if (classified.kind === "duplicate") return classified.duplicateOf;
  }
  return null;
}

/**
 * I preset da scrivere e i file scartati con il motivo. Si tiene solo chi porta una tavola
 * fra quelle importate in Resources/wavetables: gli altri citano per nome tavole di fabbrica
 * Xfer che non abbiamo, e un preset che punta a una tavola sbagliata e' peggio di nessun
 * preset. `taken` sono i nomi gia' usati dai preset scritti a mano; `levelScaleBySlug` porta
 * per ogni tavola il fattore di livello di `levelScaleFromRms`.
 */
export function collectPackPresets(files, { classify = classifyTable, taken = [], levelScaleBySlug = {} } = {}) {
  const kept = [];
  const skipped = [];

  for (const { name, buffer } of [...files].sort((a, b) => a.name.localeCompare(b.name))) {
    const category = categoryForFile(name);
    if (category === null) {
      skipped.push({ file: name, reason: "prefisso non riconosciuto" });
      continue;
    }

    const slug = wavetableSlugOf(buffer, classify);
    if (slug === null) {
      skipped.push({ file: name, reason: "nessuna tavola fra quelle importate" });
      continue;
    }

    kept.push({ file: name, category, slug, rawName: parseFxp(buffer).name });
  }

  const names = dedupeNames(kept.map((k) => presetNameFrom(k.rawName)), taken);
  const presets = kept.map((k, i) => ({
    name: names[i],
    cat: k.category,
    values: buildPresetValues(k.category, k.slug, names[i], levelScaleBySlug[k.slug] ?? 1),
  }));

  return { presets, skipped };
}

const PACK_NOTE =
  "GENERATO da scripts/import-serum-presets.mjs. Non modificare a mano: il prossimo giro " +
  "riscrive il file. Dal .fxp vengono solo il nome, la categoria (dal prefisso) e la " +
  "wavetable incorporata; i valori vengono dal modello della categoria piu' uno scarto " +
  "deterministico ricavato dal nome. Lo stato Serum non viene letto: e' uno struct opaco di " +
  "un altro strumento, con altri oscillatori e un'altra matrice. Questi non sono i preset di " +
  "Serum, sono preset di Xerum con il nome e la tavola del pack.";

/** La tavola su cui i `level` dei modelli sono tarati: e' quella dei preset a mano piu' vicini
    al centro della scala, e serve solo come metro — vedi levelScaleFromRms. */
const REFERENCE_TABLE = "saws";

const tableRms = (frames) => {
  let sum = 0;
  let n = 0;
  for (const frame of frames) for (const s of frame) { sum += s * s; n++; }
  return Math.sqrt(sum / n);
};

/** Il fattore di livello per ogni .xwt in `dir`, misurato sulle tavole vere e non scritto a
    mano: cambiare una tavola e rilanciare l'importatore ritara i preset che la usano. */
function levelScalesFrom(dir) {
  const rms = {};
  for (const file of readdirSync(dir).filter((n) => n.endsWith(".xwt")))
    rms[file.replace(/\.xwt$/, "")] = tableRms(decodeXwt(readFileSync(resolve(dir, file))).frames);

  const reference = rms[REFERENCE_TABLE];
  if (reference === undefined) throw new Error(`manca la tavola di riferimento ${REFERENCE_TABLE}.xwt in ${dir}`);

  return Object.fromEntries(Object.entries(rms).map(([slug, r]) => [slug, levelScaleFromRms(r, reference)]));
}

function parseArgs(argv) {
  const positional = [];
  const opts = { out: "Source/parameters/presets.pack.json", dryRun: false };

  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === "--out") opts.out = argv[++i];
    else if (argv[i] === "--dry-run") opts.dryRun = true;
    else positional.push(argv[i]);
  }

  if (positional.length !== 1) throw new Error("uso: import-serum-presets.mjs <cartella-fxp> [--out FILE] [--dry-run]");
  return { dir: positional[0], ...opts };
}

/** Il JSON come lo si vuole leggere in diff: un preset per riga, i valori in fila. */
function formatPackJson(presets) {
  const lines = presets.map((p) => {
    const values = Object.entries(p.values)
      .map(([id, v]) => `${JSON.stringify(id)}: ${JSON.stringify(v)}`)
      .join(", ");
    return `    { "name": ${JSON.stringify(p.name)}, "cat": ${JSON.stringify(p.cat)}, "values": { ${values} } }`;
  });

  return `{\n  "version": 1,\n  "_note": ${JSON.stringify(PACK_NOTE)},\n  "presets": [\n${lines.join(",\n")}\n  ]\n}\n`;
}

function main(argv) {
  const { dir, out, dryRun } = parseArgs(argv);
  const root = resolve(fileURLToPath(import.meta.url), "..", "..");

  const names = readdirSync(dir).filter((n) => n.toLowerCase().endsWith(".fxp")).sort();
  if (names.length === 0) throw new Error(`nessun .fxp in ${dir}`);
  const files = names.map((n) => ({ name: n, buffer: readFileSync(resolve(dir, n)) }));

  const handJson = JSON.parse(readFileSync(resolve(root, "Source/parameters/presets.json"), "utf8"));
  const taken = handJson.presets.map((p) => p.name);

  const levelScaleBySlug = levelScalesFrom(resolve(root, "Resources/wavetables"));
  const { presets, skipped } = collectPackPresets(files, { taken, levelScaleBySlug });

  const byCategory = {};
  for (const p of presets) byCategory[p.cat] = (byCategory[p.cat] ?? 0) + 1;
  console.log(`${names.length} preset letti, ${presets.length} tenuti, ${skipped.length} scartati`);
  console.log(`  per categoria: ${Object.entries(byCategory).map(([c, n]) => `${c} ${n}`).join(", ")}`);

  const reasons = {};
  for (const s of skipped) reasons[s.reason] = (reasons[s.reason] ?? 0) + 1;
  for (const [reason, n] of Object.entries(reasons)) console.log(`  scartati per "${reason}": ${n}`);

  if (dryRun) {
    console.log("--dry-run: nessun file scritto");
    return;
  }
  writeFileSync(resolve(root, out), formatPackJson(presets));
  console.log(`scritti in ${out} — ora: node scripts/gen-params.mjs`);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    console.error(`import-serum-presets: ${error.message}`);
    process.exit(1);
  }
}
