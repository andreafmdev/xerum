import test from "node:test";
import assert from "node:assert/strict";
import { generatePresets, mergePresets } from "./gen-params.mjs";

const params = {
  params: [
    { id: "wtIndex", name: "Wavetable", group: "osc", kind: "choice", slot: false, default: 0,
      options: [{ value: "basic", label: "Basic" }, { value: "saws", label: "Saws" }, { value: "grit", label: "Grit" }] },
    { id: "cutoff", name: "Cutoff", group: "filter", kind: "float", slot: true, map: { type: "log", min: 20, max: 20000 }, default: 0.5 },
  ],
};

test("un choice nominato diventa il normalizzato del suo indice", () => {
  const { header } = generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "grit", cutoff: 0.5 } }] }, params);
  assert.match(header, /\{ "wtIndex", 1\.0f \}/);
  assert.match(header, /\{ "cutoff", 0\.5f \}/);
});

test("la prima opzione di un choice vale 0", () => {
  const { header } = generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "basic" } }] }, params);
  assert.match(header, /\{ "wtIndex", 0\.0f \}/);
});

test("un nome di opzione sconosciuto fa fallire la generazione", () => {
  assert.throws(
    () => generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "retro-nope" } }] }, params),
    /retro-nope/,
  );
});

test("un choice ancora numerico fa fallire la generazione", () => {
  assert.throws(
    () => generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: 1 } }] }, params),
    /wtIndex/,
  );
});

test("un float non puo' essere una stringa", () => {
  assert.throws(
    () => generatePresets({ presets: [{ name: "P", cat: "Bass", values: { cutoff: "mezzo" } }] }, params),
    /cutoff/,
  );
});

test("la TypeScript generata porta il numero, non il nome", () => {
  const { ts } = generatePresets({ presets: [{ name: "P", cat: "Bass", values: { wtIndex: "saws" } }] }, params);
  assert.match(ts, /"wtIndex":0\.5/);
});

test("mergePresets mette i preset del pack dopo quelli scritti a mano", () => {
  const hand = { presets: [{ name: "Init", cat: "User", values: {} }] };
  const pack = { presets: [{ name: "Uridium 1", cat: "Pad", values: {} }] };
  assert.deepEqual(mergePresets(hand, pack).presets.map((p) => p.name), ["Init", "Uridium 1"]);
});

test("mergePresets regge l'assenza del file del pack", () => {
  const hand = { presets: [{ name: "Init", cat: "User", values: {} }] };
  assert.deepEqual(mergePresets(hand, null).presets.map((p) => p.name), ["Init"]);
});

test("mergePresets rifiuta un nome che il pack ruba a un preset scritto a mano", () => {
  const hand = { presets: [{ name: "Glass Pad", cat: "Pad", values: {} }] };
  const pack = { presets: [{ name: "Glass Pad", cat: "Pad", values: {} }] };
  assert.throws(() => mergePresets(hand, pack), /Glass Pad/);
});
