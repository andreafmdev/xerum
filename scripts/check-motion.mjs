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
  // Una transition-[...] con proprietà arbitraria di Tailwind che nomina una proprietà fuori
  // whitelist: height/width/top/left forzano layout, grid-template-rows lo stesso, backdrop-filter
  // è il caso già coperto per il CSS vero da "animated-blur" ma qui arriva come classe Tailwind,
  // non come dichiarazione CSS — un contesto che "animated-blur" non guarda (per costruzione,
  // dopo il fix del falso positivo su .tsx: quel controllo legge solo dentro blocchi .css). Vale
  // ovunque compaia la stringa, .tsx o .css: non è un caso `transition:` di un oggetto JS che
  // possa confondersi con altro, è sempre e solo il nome letterale di una utility Tailwind.
  // `\b` sui confini basta anche per un token con trattini come grid-template-rows, perché il
  // trattino è un carattere "non di parola" tanto quanto la parentesi quadra o la virgola.
  //
  // `pre: beforeRealComment` è solo di questa regola, non un default globale: un commento che
  // *nomina* un token vietato fra backtick per dire "non lo uso" (successo davvero su
  // Fader.tsx) andrebbe letto come violazione senza questo filtro. Le altre regole di questo
  // elenco continuano a leggere la riga intera — vedi il commento su beforeRealComment per il
  // perché una troncatura generale per tutte le regole è stata scartata.
  {
    rule: "forbidden-transition-property",
    re: /\btransition-\[[^\]]*\b(?:height|width|top|left|grid-template-rows|backdrop-filter)\b[^\]]*\]/,
    pre: beforeRealComment,
  },
  // transition-all: la Tailwind utility che anima "tutto" — il difetto con cui si apre lo spec
  // (button.tsx lo usava; round finale, finding 4: rimosso anche dalle ultime tre primitive
  // vendorizzate che lo avevano ancora, tabs.tsx/switch.tsx/badge.tsx). Vale sia come classe
  // Tailwind (`transition-all`) sia come dichiarazione CSS vera (`transition: all` /
  // `transition-property: all`): stesso doppio mondo di forbidden-transition-property qui sopra.
  //
  // `pre` compone due filtri, non uno: beforeRealComment (un commento che *nomina* la classe per
  // spiegare che non la usa più non è una violazione — lo stesso caso già risolto per
  // forbidden-transition-property) e beforeNegativeAssertion, che toglie un'asserzione negativa
  // di test (`expect(el).not.toHaveClass("transition-all")`, il caso reale in Button.test.tsx:
  // nomina la stringa esatta per dimostrare che NON compare più; senza questo filtro sarebbe una
  // violazione fantasma identica nello spirito a quella del commento).
  {
    rule: "transition-all",
    re: /\btransition-all\b|\btransition(-property)?\s*:\s*all\b/,
    pre: (line) => beforeNegativeAssertion(beforeRealComment(line)),
  },
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

// Una troncatura al primo "//" applicata a TUTTE le regole riga-per-riga è stata provata e
// scartata: un `//` dentro uno schema URL (`http://…`) non è un commento, e tagliare lì
// avrebbe fatto sparire in silenzio una violazione vera più avanti sulla stessa riga, per
// QUALSIASI regola — un fallimento silenzioso, il peggiore che un guardrail possa avere (nessuno
// se ne accorgerebbe finché qualcuno non aggiunge un URL a un elemento con className). Le regole
// di questo elenco leggono quindi la riga intera per default: solo forbidden-transition-property,
// che ne ha bisogno per non scambiare un commento che *nomina* un token vietato per il token
// stesso (successo davvero su Fader.tsx: `// niente transition-[height] qui`), usa il filtro
// sotto — ed è scritto per non lasciarsi ingannare da un URL.
//
// "//" preceduto da ":" non è mai l'inizio di un commento: è la firma di uno schema URL
// (http://, https://, …). È l'unico segnale che serve: uno schema URL ha sempre un ":" subito
// prima delle due barre, un commento reale (dopo uno spazio, una graffa, l'inizio riga) mai.
function realCommentIndex(line) {
  let from = 0;
  for (;;) {
    const i = line.indexOf("//", from);
    if (i === -1) return -1;
    if (line[i - 1] !== ":") return i;
    from = i + 2;
  }
}
/** Il testo di una riga fino al suo commento `//` vero, se c'è. Solo per le regole che lo
    richiedono esplicitamente (vedi sopra) — non è il default di tutto l'elenco. */
function beforeRealComment(line) {
  const i = realCommentIndex(line);
  return i === -1 ? line : line.slice(0, i);
}

// Solo per transition-all (vedi sopra): un'asserzione negativa di test nomina la stringa esatta
// del token per dimostrare che NON compare più su un elemento, non per applicarla. Togliere il
// contenuto della chiamata prima di cercare il token evita di leggere quella dimostrazione come
// la violazione stessa.
function beforeNegativeAssertion(line) {
  return line.replace(/\.not\.(?:toHaveClass|toContain)\([^)]*\)/g, "");
}

// ---- animated-blur: backdrop-filter o filter:blur() animati, in CSS vero. --------------------
// Nominati direttamente nel valore di transition/transition-property, anche su una
// dichiarazione multi-riga (transition:\n  backdrop-filter ...): [^;] include gli a-capo.
const TRANSITION_BACKDROP_RE = /\btransition(-property)?\s*:[^;]*\bbackdrop-filter\b/;
const TRANSITION_BLURWORD_RE = /\btransition(-property)?\s*:[^;]*\bblur\b/;

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

// Una graffa non è per forza una regola CSS: in un .ts/.tsx la stessa `{`/`}` apre un letterale
// oggetto, un corpo di funzione, un `if`/`for`, una callback... l'insieme delle forme che una
// graffa JS può assumere non è enumerabile con un'euristica sul testo che la precede (un
// tentativo con un elenco di "codette" tipiche di JS — `={`, `={{`, `=> {` — si è rivelato
// bucato: cadeva già sulla graffa di una normale dichiarazione di funzione, tipo
// `function Foo(props: Props) {`, che finisce in `) ` come una regola `:not(.foo) {`). L'unico
// segnale davvero affidabile in questo repo è quello vero: qui il CSS vive solo nei file .css
// (nessun CSS-in-JS, verificato: niente styled-components, niente template `css\``), quindi i
// controlli che leggono dentro una graffa come se fosse CSS si applicano solo li'. La prop
// `transition` di motion (`animate={{ transition: T.layerIn }}`) resta cosi' quello che è: un
// valore JS, mai scambiato per una dichiarazione CSS solo perché contiene la parola "transition:".
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
      for (const { rule, re, pre } of LINE_RULES) if (re.test(pre ? pre(line) : line)) push(i + 1, rule);
    });

    // Tutti i controlli di animated-blur guardano dentro blocchi `{ ... }`, ma solo nei file
    // .css: è li' che vive il CSS vero in questo repo (nessun CSS-in-JS). Su un .ts/.tsx la
    // stessa sintassi a graffe apre oggetti letterali, corpi di funzione, callback — la prop
    // `transition` di un componente motion inclusa — e non ha senso leggerla come CSS.
    for (const block of extname(path) === ".css" ? cssBlocks(text) : []) {
      if (/@keyframes/.test(block.selector)) {
        if (KEYFRAMES_BACKDROP_RE.test(block.body) || KEYFRAMES_FILTER_BLUR_RE.test(block.body)) {
          push(lineOf(text, block.bodyStart), "animated-blur");
        }
      } else if (
        TRANSITION_BACKDROP_RE.test(block.body) ||
        TRANSITION_BLURWORD_RE.test(block.body) ||
        (TRANSITION_FILTER_RE.test(block.body) && FILTER_USES_BLUR_RE.test(block.body))
      ) {
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

// Le deroghe temporanee del piano di motion sono finite con la task 11 (Tabs.tsx:331, il
// duration-100 dello step dell'arp): non resta nessun file grandfathered, solo le deroghe
// permanenti sopra.
export function isDeroga(relPath) {
  return EXEMPT.has(relPath);
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
