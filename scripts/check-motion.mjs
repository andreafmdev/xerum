#!/usr/bin/env node
// Rende eseguibili le regole di docs/superpowers/specs/2026-09-20-motion-system-design.md:
// whitelist delle proprietà animabili, divieto di layout animations, durate solo dai token.
import { readFileSync } from "node:fs";
import { readdir } from "node:fs/promises";
import { join, resolve, extname } from "node:path";

// ---- Regole valutate riga per riga: non richiedono di guardare oltre la riga corrente. -------
const LINE_RULES = [
  // layout / layoutId di motion: forzano reflow, ed è la feature che LazyMotion esclude.
  // Richiede che segua "=" (attributo con valore) oppure la chiusura del tag JSX
  // (es. `layout />` o `layout>`), altrimenti la parola "layout" nei commenti italiani
  // ordinari farebbe scattare falsi positivi.
  { rule: "layout-animation", re: /\blayout(Id)?(=|\s*\/?>)/ },
  // Il namespace pieno di motion aggira LazyMotion e gonfia il bundle.
  // Richiede "<" davanti: un commento che nomina solo il file motion.ts (senza "<")
  // non deve essere segnalato.
  { rule: "full-motion-namespace", re: /<motion\.[a-z]/ },
  // Durate scritte a mano come utility Tailwind: duration-150, duration-[200ms]. duration-0 è
  // ammessa. Non tocca le dichiarazioni di custom property (--dur-state: 140ms), che non
  // contengono la sottostringa "duration-".
  { rule: "hardcoded-duration", re: /\bduration-(\[?\d*[1-9]\d*m?s?\]?)\b/ },
];

// Righe tra questi due marker sono escluse da ogni regola: usati nel blocco di override dei
// token di durata per variante dentro synth.css, l'unico punto dove i ms si scrivono a mano
// di proposito. Lo scoping è per riga, non per file: il resto di synth.css resta sorvegliato.
const BLOCK_START = "motion-token-block:start";
const BLOCK_END = "motion-token-block:end";

function excludedLineRanges(text) {
  const ranges = [];
  let start = -1;
  text.split("\n").forEach((line, i) => {
    if (line.includes(BLOCK_START)) start = i;
    else if (line.includes(BLOCK_END) && start !== -1) {
      ranges.push([start, i]);
      start = -1;
    }
  });
  return ranges;
}
const isExcludedLine = (line0, ranges) => ranges.some(([s, e]) => line0 >= s && line0 <= e);

/** Numero di riga (1-based) di un indice di carattere nel testo del file. */
function lineOf(text, index) {
  let line = 1;
  for (let i = 0; i < index && i < text.length; i++) if (text[i] === "\n") line++;
  return line;
}

// ---- animated-blur: backdrop-filter o filter:blur() animati. --------------------------------
// Nominati direttamente nel valore di transition/transition-property, anche su una
// dichiarazione multi-riga (transition:\n  backdrop-filter ...): [^;] include gli a-capo.
const TRANSITION_BACKDROP_RE = /\btransition(-property)?\s*:[^;]*\bbackdrop-filter\b/g;
const TRANSITION_BLURWORD_RE = /\btransition(-property)?\s*:[^;]*\bblur\b/g;

// Il bypass più naturale: `transition: filter ...` e `filter: blur(...)` sono due
// dichiarazioni distinte nella stessa regola, in un ordine qualsiasi. Servono i confini del
// blocco `{ ... }` per correlarle; la stessa suddivisione in blocchi isola anche il contenuto
// di un @keyframes indipendentemente da come viene referenziato altrove.
function cssBlocks(text) {
  const blocks = [];
  const stack = [];
  let pos = 0;
  for (let i = 0; i < text.length; i++) {
    if (text[i] === "{") {
      stack.push({ selector: text.slice(pos, i), bodyStart: i + 1 });
      pos = i + 1;
    } else if (text[i] === "}") {
      const top = stack.pop();
      if (top) blocks.push({ selector: top.selector, body: text.slice(top.bodyStart, i), bodyStart: top.bodyStart });
      pos = i + 1;
    }
  }
  return blocks;
}
const TRANSITION_FILTER_RE = /\btransition(-property)?\s*:[^;]*\bfilter\b/;
const FILTER_USES_BLUR_RE = /\bfilter\s*:[^;]*\bblur\(/;
const KEYFRAMES_BACKDROP_RE = /\bbackdrop-filter\s*:/;
const KEYFRAMES_FILTER_BLUR_RE = /\bfilter\s*:[^;]*\bblur\(/;

// ---- hardcoded-css-duration: durate scritte a mano in transition/animation (CSS, non
// Tailwind). `(?<!-)` esclude il nome di una custom property come
// `--default-transition-duration`, che contiene la sottostringa "transition-duration" ma non
// È quella proprietà. -------------------------------------------------------------------------
const DURATION_DECL_RE =
  /(?<!-)\b(transition-duration|transition-delay|animation-duration|animation-delay|transition|animation)\s*:([^;]+);/g;
const RAW_DURATION_RE = /\b\d*[1-9]\d*m?s\b/;

/** Le violazioni di un insieme di file già letti. */
export function findViolations(files) {
  const out = [];
  for (const { path, text } of files) {
    const excluded = excludedLineRanges(text);
    const push = (line, rule) => {
      if (!isExcludedLine(line - 1, excluded)) out.push({ path, line, rule });
    };

    text.split("\n").forEach((line, i) => {
      for (const { rule, re } of LINE_RULES) if (re.test(line)) push(i + 1, rule);
    });

    for (const re of [TRANSITION_BACKDROP_RE, TRANSITION_BLURWORD_RE]) {
      re.lastIndex = 0;
      let m;
      while ((m = re.exec(text))) push(lineOf(text, m.index), "animated-blur");
    }

    for (const block of cssBlocks(text)) {
      if (/@keyframes/.test(block.selector)) {
        if (KEYFRAMES_BACKDROP_RE.test(block.body) || KEYFRAMES_FILTER_BLUR_RE.test(block.body)) {
          push(lineOf(text, block.bodyStart), "animated-blur");
        }
      } else if (TRANSITION_FILTER_RE.test(block.body) && FILTER_USES_BLUR_RE.test(block.body)) {
        push(lineOf(text, block.bodyStart), "animated-blur");
      }
    }

    DURATION_DECL_RE.lastIndex = 0;
    let d;
    while ((d = DURATION_DECL_RE.exec(text))) {
      if (RAW_DURATION_RE.test(d[2])) push(lineOf(text, d.index), "hardcoded-css-duration");
    }
  }
  return out;
}

const EXT = new Set([".ts", ".tsx", ".css"]);
// Deroghe permanenti: motion.ts è la sorgente delle durate, theme.css la loro copia CSS.
// select.tsx è una primitiva shadcn vendorizzata che viene rigenerata dal CLI shadcn:
// non si modifica a mano, quindi non ha senso farla sistemare da una task del piano.
const EXEMPT = new Set(["motion.ts", "theme.css", "select.tsx"]);

// Deroghe temporanee: spariscono con la task 10 del piano di motion. Tabs.tsx:331 ha ancora
// la violazione live (duration-100); non è "già sistemata", solo grandfathered fino a quel
// punto del piano, come PresetOverlay.tsx.
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
