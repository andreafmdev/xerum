#!/usr/bin/env node
// Rende eseguibili le regole di docs/superpowers/specs/2026-09-20-motion-system-design.md:
// whitelist delle proprietà animabili, divieto di layout animations, durate solo dai token.
import { readFileSync } from "node:fs";
import { readdir } from "node:fs/promises";
import { join, resolve, extname } from "node:path";

const RULES = [
  // Una transizione che nomina backdrop-filter o blur: il blur dei layer è statico.
  { rule: "animated-blur", re: /transition[^;{}]*\b(backdrop-filter|blur)\b/ },
  // layout / layoutId di motion: forzano reflow, ed è la feature che LazyMotion esclude.
  // Richiede che segua "=" (attributo con valore) oppure la chiusura del tag JSX
  // (es. `layout />` o `layout>`), altrimenti la parola "layout" nei commenti italiani
  // ordinari farebbe scattare falsi positivi.
  { rule: "layout-animation", re: /\blayout(Id)?(=|\s*\/?>)/ },
  // Il namespace pieno di motion aggira LazyMotion e gonfia il bundle.
  // Richiede "<" davanti: un commento che nomina solo il file motion.ts (senza "<")
  // non deve essere segnalato.
  { rule: "full-motion-namespace", re: /<motion\.[a-z]/ },
  // Durate scritte a mano: duration-150, duration-[200ms]. duration-0 è ammessa.
  // Colpisce solo le utility Tailwind "duration-<numero>", mai le dichiarazioni di
  // custom property come "--dur-state: 140ms" (synth.css le usa deliberatamente).
  { rule: "hardcoded-duration", re: /\bduration-(\[?\d*[1-9]\d*m?s?\]?)\b/ },
];

/** Le violazioni di un insieme di file già letti. */
export function findViolations(files) {
  const out = [];
  for (const { path, text } of files) {
    text.split("\n").forEach((line, i) => {
      for (const { rule, re } of RULES) {
        if (re.test(line)) out.push({ path, line: i + 1, rule });
      }
    });
  }
  return out;
}

const EXT = new Set([".ts", ".tsx", ".css"]);
// Deroghe permanenti: motion.ts è la sorgente delle durate, theme.css la loro copia CSS.
// select.tsx è una primitiva shadcn vendorizzata che viene rigenerata dal CLI shadcn:
// non si modifica a mano, quindi non ha senso farla sistemare da una task del piano.
const EXEMPT = new Set(["motion.ts", "theme.css", "select.tsx"]);

// Deroghe temporanee: spariscono con la task 10 del piano di motion (Tabs.tsx è già
// sistemata qui; PresetOverlay.tsx resta finché la task 6 non la tocca).
const GRANDFATHERED = new Set(["Tabs.tsx", "PresetOverlay.tsx"]);

async function collect(dir, acc = []) {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    if (e.name === "node_modules" || e.name === "dist") continue;
    const path = join(dir, e.name);
    if (e.isDirectory()) await collect(path, acc);
    else if (EXT.has(extname(e.name)) && !EXEMPT.has(e.name) && !GRANDFATHERED.has(e.name)) {
      acc.push({ path, text: readFileSync(path, "utf8") });
    }
  }
  return acc;
}

if (import.meta.filename === process.argv[1]) {
  // Percorsi relativi a questo file (<repo>/scripts), non alla cwd: lo script si invoca
  // sia dalla radice sia da WebUI/.
  const here = import.meta.dirname;
  const roots = [resolve(here, "../WebUI/src"), resolve(here, "../WebUI/packages/ui/src")];
  const files = (await Promise.all(roots.map((r) => collect(r)))).flat();
  const bad = findViolations(files);
  for (const v of bad) console.error(`${v.path}:${v.line}  ${v.rule}`);
  if (bad.length) {
    console.error(`\n${bad.length} motion rule violation(s). See docs/superpowers/specs/2026-09-20-motion-system-design.md`);
    process.exit(1);
  }
  console.log("motion rules: ok");
}
