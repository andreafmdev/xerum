#!/usr/bin/env node
// Rende eseguibili le regole di docs/superpowers/specs/2026-09-20-motion-system-design.md:
// whitelist delle proprietà animabili, divieto di layout animations, durate solo dai token.
import { readFileSync } from "node:fs";
import { readdir } from "node:fs/promises";
import { join, resolve, relative, extname, sep } from "node:path";

// Radice del repo, per trasformare un path assoluto in un path relativo stabile (POSIX, quindi
// identico su ogni SO) da confrontare con le deroghe sotto. Prima le deroghe confrontavano il
// solo basename (e.name in collect()): due file con lo stesso nome in cartelle diverse
// condividevano la stessa deroga in silenzio (è successo davvero con Tabs.tsx, vedi sotto).
const REPO_ROOT = resolve(import.meta.dirname, "..");
export function repoRelativePath(path) {
  return relative(REPO_ROOT, path).split(sep).join("/");
}

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

// Righe tra un marker `<nome>:start` e il suo `<nome>:end` sono escluse da ogni regola. Ogni
// deroga ha il proprio nome (es. `motion-token-block`, `ambient-loop-block`): visibile,
// cercabile con grep, e motivata da un commento accanto — mai un'indirezione (una custom
// property qualsiasi con un valore letterale dentro) che aggirerebbe la regola in silenzio.
// Lo scoping è per riga, non per file: il resto del file resta sorvegliato.
const BLOCK_MARKER_RE = /([\w-]+):(start|end)\b/;

function excludedLineRanges(text) {
  const ranges = [];
  const open = new Map();
  text.split("\n").forEach((line, i) => {
    const m = line.match(BLOCK_MARKER_RE);
    if (!m) return;
    const [, name, kind] = m;
    if (kind === "start") open.set(name, i);
    else if (open.has(name)) {
      ranges.push([open.get(name), i]);
      open.delete(name);
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
// Tailwind). Un prefisso vendor opzionale (-webkit-/-moz-/-ms-/-o-) fa parte del match, così
// `-webkit-transition` resta sorvegliato: è la stessa proprietà con un altro nome. Il
// lookbehind `(?<![\w-])` richiede che nulla di "attaccato" preceda l'inizio del match: questo
// esclude la sottostringa "transition-duration" dentro il NOME di una custom property come
// `--default-transition-duration` (lì il carattere subito prima è un `-` di "default-"), senza
// per questo escludere `-webkit-transition` (lì il carattere subito prima del prefisso vendor
// è uno spazio o un `;`, non un carattere di identificatore).
const DURATION_DECL_RE =
  /(?<![\w-])(?:-(?:webkit|moz|ms|o)-)?(transition-duration|transition-delay|animation-duration|animation-delay|transition|animation)\s*:([^;]+);/g;
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
// Deroghe permanenti, un path per riga: motion.ts è la sorgente delle durate, theme.css la loro
// copia CSS. select.tsx è una primitiva shadcn vendorizzata che viene rigenerata dal CLI shadcn:
// non si modifica a mano, quindi non ha senso farla sistemare da una task del piano.
//
// Path completi dalla radice del repo, non bare basename: il matching per solo nome file
// copriva silenziosamente OGNI file con quel nome in tutte le cartelle scandite, non solo
// quello inteso. È il bug reale che ha reso `check:motion → ok` vero e privo di significato per
// il diff del Task 6: WebUI/packages/ui/src/components/Tabs/Tabs.tsx (il file toccato dal task)
// non è mai stato scansionato, perché condivideva il nome con WebUI/src/synth/ui/Tabs.tsx (il
// file davvero grandfathered, che ha la violazione viva a riga 331) e la deroga sul basename
// copriva entrambi. Una deroga ora nomina esattamente un file.
const EXEMPT = new Set([
  "WebUI/packages/ui/src/motion.ts",
  "WebUI/packages/ui/src/theme.css",
  "WebUI/packages/ui/src/components/ui/select.tsx",
]);

// Deroghe temporanee: spariscono con la task 10 del piano di motion. Tabs.tsx:331 (quello sotto
// WebUI/src/synth/ui/, NON la libreria @xerum/ui) ha ancora la violazione live (duration-100);
// non è "già sistemata", solo grandfathered fino a quel punto del piano, come PresetOverlay.tsx.
const GRANDFATHERED = new Set(["WebUI/src/synth/ui/Tabs.tsx", "WebUI/src/synth/ui/PresetOverlay.tsx"]);

export function isDeroga(relPath) {
  return EXEMPT.has(relPath) || GRANDFATHERED.has(relPath);
}

async function collect(dir, acc = []) {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    if (e.name === "node_modules" || e.name === "dist") continue;
    const path = join(dir, e.name);
    if (e.isDirectory()) await collect(path, acc);
    else if (EXT.has(extname(e.name)) && !isDeroga(repoRelativePath(path))) {
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
