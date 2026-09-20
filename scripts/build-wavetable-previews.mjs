#!/usr/bin/env node
// Genera WebUI/src/synth/wavetables.generated.ts dalle tavole .xwt che finiscono nel binario.
// Uso: node scripts/build-wavetable-previews.mjs   (oppure: cd WebUI && pnpm gen:wavetables)
//
// Girando sugli stessi file che carica il motore, l'onda a schermo non puo' divergere dal
// suono: se una tavola cambia, l'anteprima cambia con lei alla prossima generazione.
import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { decodeXwt } from "./wavetable-dsp.mjs";

/** Nove frame perche' e' gia' la pila che WaveDisplay disegna; 256 punti perche' il canvas
    non ne mostra di piu' e 2048 numeri per frame farebbero un file da megabyte. */
const OUT_FRAMES = 9;
const OUT_POINTS = 256;

/**
 * Riduce una tavola a `outFrames` x `outPoints`. Lungo i frame si prende il piu' vicino
 * (i frame estremi restano quelli veri); lungo il ciclo si fa la media della finestra, non
 * si salta un campione ogni otto: una tavola brillante decimata a secco disegnerebbe
 * un'onda che non e' la sua.
 */
export function buildPreview(frames, outFrames, outPoints) {
  const stride = frames[0].length / outPoints;
  const out = [];

  for (let f = 0; f < outFrames; f++) {
    const source = frames[outFrames === 1 ? 0 : Math.round((f * (frames.length - 1)) / (outFrames - 1))];
    const row = [];
    for (let p = 0; p < outPoints; p++) {
      let sum = 0;
      const from = Math.round(p * stride);
      const to = Math.round((p + 1) * stride);
      for (let i = from; i < to; i++) sum += source[i];
      row.push(Math.round((sum / (to - from)) * 1e4) / 1e4);
    }
    out.push(row);
  }

  return out;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = resolve(fileURLToPath(import.meta.url), "../..");
  const params = JSON.parse(readFileSync(resolve(root, "Source/parameters/parameters.json"), "utf8"));
  const wtIndex = params.params.find((p) => p.id === "wtIndex");
  if (!wtIndex) throw new Error("parameters.json non ha il parametro wtIndex");

  const entries = wtIndex.options.map((option) => {
    const file = resolve(root, `Resources/wavetables/${option.value}.xwt`);
    const { frames } = decodeXwt(readFileSync(file));
    return `  { value: ${JSON.stringify(option.value)}, frames: [\n${buildPreview(frames, OUT_FRAMES, OUT_POINTS)
      .map((row) => `    [${row.join(",")}]`)
      .join(",\n")}\n  ] },`;
  });

  const ts = [
    "// GENERATO da scripts/build-wavetable-previews.mjs a partire da Resources/wavetables/*.xwt.",
    "// Non modificare a mano. Rigenera con: node scripts/build-wavetable-previews.mjs",
    "",
    "export type WavetablePreview = { value: string; frames: number[][] };",
    "",
    `export const WAVETABLE_FRAMES = ${OUT_FRAMES};`,
    `export const WAVETABLE_POINTS = ${OUT_POINTS};`,
    "",
    "/** Nell'ordine delle opzioni di wtIndex in parameters.json: l'indice del choice e' l'indice qui. */",
    "export const WAVETABLES: WavetablePreview[] = [",
    ...entries,
    "];",
    "",
  ].join("\n");

  writeFileSync(resolve(root, "WebUI/src/synth/wavetables.generated.ts"), ts);
  console.log(`build-wavetable-previews: ${entries.length} tavole → wavetables.generated.ts`);
}
