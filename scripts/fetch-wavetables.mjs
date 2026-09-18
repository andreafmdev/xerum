#!/usr/bin/env node
// Scarica le onde AKWF (CC0) e le converte nelle tavole .xwt del plugin.
// Uso: node scripts/fetch-wavetables.mjs
// Gira raramente: i .xwt sono committati, questo script serve solo a rigenerarli.
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { encodeXwt, parseWav16Mono, removeDcAndNormalise, resampleCycle, selectFrames } from "./wavetable-dsp.mjs";

const REPO = "KristofferKarlAxelEkstrand/AKWF-FREE";
const BRANCH = "main";
const FRAMES = 64;
const FRAME_SIZE = 2048;

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

async function buildTable(table, outDir) {
  const urls = await listFamily(table.family);
  // Scaricare tutta la famiglia quando ne servono 64 è spreco: si prendono
  // prima gli indici utili, poi si interpola fra quelli.
  const wanted = urls.length <= FRAMES ? urls : selectIndices(urls, FRAMES);
  const waves = [];
  for (const url of wanted) waves.push(await downloadWave(url));

  const frames = selectFrames(waves, FRAMES).map((frame) => removeDcAndNormalise(resampleCycle(frame, FRAME_SIZE)));
  writeFileSync(resolve(outDir, `${table.id}.xwt`), encodeXwt(frames, FRAME_SIZE));
  console.log(`${table.id}: ${waves.length} onde da ${table.family} → ${FRAMES} frame`);
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
      "| tavola | famiglia AKWF |",
      "|---|---|",
      ...TABLES.map((t) => `| ${t.label} (\`${t.id}\`) | \`${t.family}\` |`),
      "",
    ].join("\n"),
  );
}

await main();
