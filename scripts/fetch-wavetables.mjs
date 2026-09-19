#!/usr/bin/env node
// Scarica le onde AKWF (CC0) e le converte nelle tavole .xwt del plugin.
// Uso: node scripts/fetch-wavetables.mjs
// Gira raramente: i .xwt sono committati, questo script serve solo a rigenerarli.
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  alignFramesToPhase,
  encodeXwt,
  enforceHarmonicPhaseContinuity,
  equaliseFrameRms,
  midMorphRmsLosses,
  normaliseTable,
  parseWav16Mono,
  removeDc,
  resampleCycle,
  selectFrames,
} from "./wavetable-dsp.mjs";

const REPO = "KristofferKarlAxelEkstrand/AKWF-FREE";
const BRANCH = "main";
const FRAMES = 64;
const FRAME_SIZE = 2048;

// Quante onde AKWF si scaricano per tavola. Deve restare sotto FRAMES, altrimenti
// `selectFrames` si ritrova un'ancora per frame, il fattore di interpolazione è
// sempre 0 e il "morph" diventa una fila di 64 onde scollegate: Position fa
// scatti invece di una transizione.
const ANCHORS = 32;

// Perdita di RMS tollerata a metà strada fra due frame adiacenti.
const LOSS_TARGET_DB = -1.0;

// Quanto si pareggia l'RMS fra i frame. Le onde AKWF arrivano già a picco 1.0
// una per una, con RMS che dentro la stessa famiglia varia di una decina di dB:
// lo squilibrio è nel materiale, non nella pipeline, e senza questo passaggio
// muovere Position cambia volume. A 1.0 però si appiattirebbe anche quello che
// è giusto — in uno sweep PWM l'impulso stretto DEVE suonare più piano — quindi
// si comprime l'escursione al 30% e basta.
const RMS_EXPONENT = 0.7;

// L'ordine è quello delle opzioni di `wtIndex` in Source/parameters/parameters.json.
const TABLES = [
  { id: "basic", label: "Basic Shapes", family: "AKWF_bw_perfectwaves" },
  { id: "saws", label: "Analog Saws", family: "AKWF_bw_saw" },
  { id: "grit", label: "Digital Grit", family: "AKWF_bitreduced" },
  { id: "vocal", label: "Vocal Formant", family: "AKWF_hvoice" },
  { id: "bells", label: "Glass Bells", family: "AKWF_fmsynth" },
  { id: "pwm", label: "PWM Sweep", family: "AKWF_bw_squ" },
];

async function listFamily(family) {
  const url = `https://api.github.com/repos/${REPO}/contents/AKWF/${family}`;
  const res = await fetch(url, { headers: { accept: "application/vnd.github+json" } });
  if (!res.ok) throw new Error(`lista di ${family}: HTTP ${res.status}`);
  const entries = await res.json();
  return entries
    .filter((e) => e.type === "file" && e.name.toLowerCase().endsWith(".wav"))
    .sort((a, b) => a.name.localeCompare(b.name))
    .map((e) => e.download_url);
}

async function downloadWave(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${url}: HTTP ${res.status}`);
  return parseWav16Mono(Buffer.from(await res.arrayBuffer()));
}

/**
 * Gli ultimi due passi, sempre in quest'ordine: si pareggia in parte l'RMS fra i
 * frame (guadagno costante per frame, quindi timbralmente neutro) e poi si scala
 * tutta la tavola con un solo fattore, così il picco globale è esattamente 1.0.
 */
function finish(frames) {
  equaliseFrameRms(frames, RMS_EXPONENT);
  normaliseTable(frames);
  return frames;
}

async function buildTable(table, outDir) {
  const urls = await listFamily(table.family);
  // Scaricare tutta la famiglia quando ne servono poche è spreco: si prendono
  // prima le ancore, poi si interpola fra quelle.
  const wanted = urls.length <= ANCHORS ? urls : selectIndices(urls, ANCHORS);
  const waves = [];
  for (const url of wanted) waves.push(await downloadWave(url));

  // Il ricampionamento va fatto qui e non dopo l'interpolazione: la serie di
  // Fourier è lineare, quindi il risultato è lo stesso, ma così l'allineamento
  // lavora già sulla lunghezza definitiva.
  const cycles = waves.map((wave) => removeDc(resampleCycle(wave, FRAME_SIZE)));

  // Le onde AKWF non sono in fase fra loro: fonderle così le fa cancellare a
  // pettine. Prima si allineano (è solo un offset di fase, il timbro di ogni
  // onda resta quello), poi si interpola.
  let anchors = alignFramesToPhase(cycles);
  let frames = finish(selectFrames(anchors, FRAMES));

  // Se l'allineamento non basta, si passa alla continuità di fase per armonica,
  // che invece le onde le altera. La verifica va fatta sulla tavola finita:
  // l'equalizzazione cambia i livelli relativi fra frame adiacenti e quindi
  // sposta anche la perdita a metà morph.
  let usedStepB = false;
  if (Math.min(...midMorphRmsLosses(frames)) < LOSS_TARGET_DB) {
    anchors = enforceHarmonicPhaseContinuity(anchors);
    frames = finish(selectFrames(anchors, FRAMES));
    usedStepB = true;
  }

  writeFileSync(resolve(outDir, `${table.id}.xwt`), encodeXwt(frames, FRAME_SIZE));
  const worst = Math.min(...midMorphRmsLosses(frames)).toFixed(2);
  console.log(
    `${table.id}: ${waves.length} onde da ${table.family} → ${FRAMES} frame` +
      `${usedStepB ? " (con continuità di fase)" : ""}, perdita peggiore a metà morph ${worst} dB`,
  );
}

function selectIndices(list, count) {
  return Array.from({ length: count }, (_, i) => list[Math.round(((list.length - 1) * i) / (count - 1))]);
}

async function main() {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
  const outDir = resolve(root, "Resources/wavetables");
  mkdirSync(outDir, { recursive: true });

  for (const table of TABLES) await buildTable(table, outDir);

  writeFileSync(
    resolve(outDir, "CREDITS.md"),
    [
      "# Wavetables",
      "",
      `Generate da \`scripts/fetch-wavetables.mjs\` il ${new Date().toISOString().slice(0, 10)}.`,
      "",
      "Fonte: **Adventure Kid Waveforms (AKWF)** — Kristoffer Ekstrand",
      `<https://github.com/${REPO}>`,
      "",
      "Licenza: **CC0-1.0** (pubblico dominio). L'attribuzione non è dovuta: è qui per tracciare la provenienza.",
      "",
      "## Pipeline",
      "",
      `Per ogni tavola si scaricano fino a ${ANCHORS} onde della famiglia, che fanno da ancore del morph.`,
      "",
      `1. ogni onda viene ricampionata a ${FRAME_SIZE} campioni per ciclo (serie di Fourier) e le si toglie la continua;`,
      "2. **allineamento di fase**: ogni ancora viene ruotata sullo shift circolare che la correla al massimo con",
      "   la precedente. È solo un offset di fase, lo spettro di ampiezza non cambia, ma senza questo passaggio il",
      "   crossfade fra onde sfasate le fa cancellare a pettine;",
      `3. i ${FRAMES} frame si ricavano interpolando linearmente fra ancore adiacenti;`,
      `4. se a metà strada fra due frame si perde più di ${-LOSS_TARGET_DB} dB di RMS, si applica anche la continuità`,
      "   di fase per armonica (fase srotolata lungo i frame e riscritta come retta, ampiezze invariate);",
      `5. **equalizzazione parziale dell'RMS**: ogni frame prende un guadagno \`(rms_mediano / rms)^${RMS_EXPONENT}\`. Le onde`,
      "   AKWF sono già normalizzate a picco 1.0 una per una, quindi il loro RMS varia di una decina di dB dentro la",
      `   stessa famiglia. L'esponente ${RMS_EXPONENT} comprime l'escursione al ${Math.round((1 - RMS_EXPONENT) * 100)}% invece di appiattirla: in uno sweep PWM`,
      "   l'impulso che si stringe deve calare di volume. È un guadagno costante per frame, quindi timbralmente neutro;",
      "6. **normalizzazione globale**: un solo fattore di scala per tutta la tavola, quello che porta a 1 il massimo",
      "   campione su tutti i frame. Normalizzare frame per frame farebbe cambiare volume a Position.",
      "",
      "Le stesse operazioni si possono applicare a tavole già scritte con `node scripts/realign-wavetables.mjs`,",
      "che stampa le metriche prima e dopo.",
      "",
      "| tavola | famiglia AKWF |",
      "|---|---|",
      ...TABLES.map((t) => `| ${t.label} (\`${t.id}\`) | \`${t.family}\` |`),
      "",
    ].join("\n"),
  );
}

await main();
