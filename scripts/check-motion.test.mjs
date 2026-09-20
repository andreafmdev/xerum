import test from "node:test";
import assert from "node:assert/strict";
import { findViolations } from "./check-motion.mjs";

const rules = (text, path = "a.tsx") => findViolations([{ path, text }]).map((v) => v.rule);

test("flags layout animations", () => {
  assert.deepEqual(rules('<m.div layoutId="tab" />'), ["layout-animation"]);
});

test("does not flag the word layout in an ordinary comment", () => {
  // Correzione 1: la parola "layout" ricorre in commenti italiani ordinari e non deve
  // scatenare la regola, che riguarda solo l'attributo JSX layout/layoutId.
  assert.deepEqual(rules("// jsdom non fa layout, quindi qui non si misura niente"), []);
  assert.deepEqual(rules("// dita anche su un layout non QWERTY."), []);
  assert.deepEqual(rules("// La verifica dello zoom, dopo il layout: una sola volta"), []);
});

test("flags the full motion namespace", () => {
  assert.deepEqual(rules('import { motion } from "motion/react";\n<motion.div />'), ["full-motion-namespace"]);
});

test("does not flag a comment that merely names motion.ts", () => {
  // Correzione 2: un commento che nomina il file motion.ts non deve attivare la regola,
  // altrimenti ogni riferimento alla sorgente dei token verrebbe segnalato come violazione.
  assert.deepEqual(rules("// motion.ts è la sorgente unica dei valori di durata ed easing"), []);
});

test("flags a hand-written Tailwind duration utility", () => {
  assert.deepEqual(rules('<div className="transition duration-150" />'), ["hardcoded-duration"]);
});

test("does not flag hand-written millisecond custom properties", () => {
  // Correzione 4: synth.css scrive valori ms a mano dentro dichiarazioni di custom property
  // (--dur-state: 140ms), non dentro utility Tailwind duration-*: sono ammesse.
  assert.deepEqual(rules("--dur-state: 140ms;\n--dur-layer: 160ms;", "synth.css"), []);
});

test("accepts the token forms", () => {
  const ok = [
    '<m.div className="transition-opacity duration-(--dur-layer) ease-glass" />',
    "a { transition: opacity var(--dur-state) var(--ease-glass); }",
    '<div className="duration-0" />',
  ].join("\n");
  assert.deepEqual(rules(ok), []);
});

test("reports the offending line number", () => {
  const [v] = findViolations([{ path: "x.tsx", text: '\n\n<m.div layout />' }]);
  assert.equal(v.line, 3);
});

// --- Finding 1 & 2: animated-blur, each mechanism proven in isolation ------------------------

test("flags a same-line transition naming backdrop-filter", () => {
  assert.deepEqual(rules(".sx { transition: backdrop-filter var(--dur-state); }", "a.css"), ["animated-blur"]);
});

test("flags a multi-line transition declaration naming backdrop-filter", () => {
  const text = [".sx {", "  transition:", "    backdrop-filter var(--dur-state);", "}"].join("\n");
  assert.deepEqual(rules(text, "a.css"), ["animated-blur"]);
});

test("flags transition-property naming backdrop-filter", () => {
  const text = ".sx { transition-property: backdrop-filter; transition-duration: var(--dur-state); }";
  assert.deepEqual(rules(text, "a.css"), ["animated-blur"]);
});

test("flags filter transitioned via transition-property when filter uses blur, declared in any order", () => {
  // Il bypass reale: transition e il blur() sono due dichiarazioni separate nella stessa
  // regola, e possono comparire in qualunque ordine. Isolato dalla riga 200ms (test dedicato).
  assert.deepEqual(
    rules('.sx { filter: blur(8px); transition-property: filter; transition-duration: var(--dur-state); }', "a.css"),
    ["animated-blur"],
  );
});

test("flags the exact reviewer bypass: filter:blur + transition:filter as sibling declarations", () => {
  const text = ['.foo { filter: blur(8px); transition: filter 200ms; }', '.foo:hover { filter: blur(0); }'].join("\n");
  // Qui la regola vietata (animated-blur) e la durata scritta a mano (200ms, non un token)
  // sono entrambe vere violazioni dello stesso testo.
  assert.deepEqual(rules(text, "a.css").sort(), ["animated-blur", "hardcoded-css-duration"]);
});

test("does not flag a plain filter transition without any blur", () => {
  // filter: brightness/saturate sono nella whitelist, anche se transizionati.
  assert.deepEqual(rules(".sx { filter: brightness(1.2); transition-property: filter; transition-duration: var(--dur-state); }", "a.css"), []);
});

test("does not flag a static backdrop-filter that is never transitioned", () => {
  assert.deepEqual(rules(".sx { backdrop-filter: blur(10px); }", "a.css"), []);
});

test("flags a @keyframes block that changes backdrop-filter", () => {
  const text = "@keyframes k { from { backdrop-filter: blur(0); } to { backdrop-filter: blur(10px); } }";
  assert.deepEqual(rules(text, "a.css"), ["animated-blur"]);
});

test("flags a @keyframes block that changes filter to a blur", () => {
  const text = "@keyframes k { from { filter: blur(0); } to { filter: blur(10px); } }";
  assert.deepEqual(rules(text, "a.css"), ["animated-blur"]);
});

test("does not flag a @keyframes block that only animates transform", () => {
  const text = "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }";
  assert.deepEqual(rules(text, "a.css"), []);
});

// --- Finding 3 / ruling: raw CSS duration literals in transition/animation shorthand ---------

test("flags a raw duration in a transition shorthand", () => {
  assert.deepEqual(rules(".sx { transition: opacity 150ms; }", "a.css"), ["hardcoded-css-duration"]);
});

test("flags a raw duration in a transition-duration longhand", () => {
  assert.deepEqual(rules(".sx { transition-duration: 150ms; }", "a.css"), ["hardcoded-css-duration"]);
});

test("flags a raw duration in an animation shorthand", () => {
  assert.deepEqual(rules(".sx { animation: pulse 1500ms ease-in-out infinite; }", "a.css"), ["hardcoded-css-duration"]);
});

test("flags a raw duration in an animation-duration longhand", () => {
  assert.deepEqual(rules(".sx { animation-duration: 37s; }", "a.css"), ["hardcoded-css-duration"]);
});

test("accepts token-based transition and animation durations", () => {
  const text = ".sx { transition: opacity var(--dur-state); animation: pulse var(--dur-layer) ease-in-out infinite; }";
  assert.deepEqual(rules(text, "a.css"), []);
});

test("does not flag a raw ms literal that is part of a custom property name, not a duration", () => {
  // "--default-transition-duration" contiene la sottostringa "transition-duration" ma è il
  // NOME di una custom property, non la proprietà CSS transition-duration.
  assert.deepEqual(rules("--default-transition-duration: 120ms;", "theme.css"), []);
});

test("does not flag a raw duration inside a marked motion-token-block", () => {
  const text = ["/* motion-token-block:start */", ".sx { transition: opacity 140ms; }", "/* motion-token-block:end */"].join("\n");
  assert.deepEqual(rules(text, "synth.css"), []);
});

test("still flags a raw duration outside the marked motion-token-block", () => {
  const text = [
    "/* motion-token-block:start */",
    ".sx { --dur-state: 140ms; }",
    "/* motion-token-block:end */",
    ".sy { transition: opacity 150ms; }",
  ].join("\n");
  assert.deepEqual(rules(text, "synth.css"), ["hardcoded-css-duration"]);
});
