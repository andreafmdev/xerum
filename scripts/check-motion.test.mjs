import test from "node:test";
import assert from "node:assert/strict";
import { findViolations } from "./check-motion.mjs";

const rules = (text, path = "a.tsx") => findViolations([{ path, text }]).map((v) => v.rule);

test("flags an animated backdrop-filter", () => {
  assert.deepEqual(rules("a { transition: backdrop-filter 200ms; }", "a.css"), ["animated-blur"]);
});

test("flags an animated blur radius", () => {
  assert.deepEqual(rules("a { transition: filter 200ms; filter: blur(var(--b)); }\n.b { transition: blur 1ms }", "a.css"), ["animated-blur"]);
});

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

test("flags a hand-written duration utility", () => {
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
