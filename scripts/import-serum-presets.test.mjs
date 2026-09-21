import test from "node:test";
import assert from "node:assert/strict";
import { deflateSync } from "node:zlib";
import {
  buildPresetValues,
  collectPackPresets,
  categoryForFile,
  dedupeNames,
  levelScaleFromRms,
  presetNameFrom,
  seededUnits,
  TEMPLATES,
} from "./import-serum-presets.mjs";

test("categoryForFile traduce il prefisso del pack nella categoria di Xerum", () => {
  assert.equal(categoryForFile("BS-airwolf.fxp"), "Bass");
  assert.equal(categoryForFile("LD-Commando18.fxp"), "Lead");
  assert.equal(categoryForFile("PD-Uridium1.fxp"), "Pad");
  assert.equal(categoryForFile("KY-Leaderboard7.fxp"), "Keys");
  assert.equal(categoryForFile("PL-Commando14.fxp"), "Pluck");
  assert.equal(categoryForFile("FX-Spindizzy.fxp"), "FX");
  assert.equal(categoryForFile("SUB-something.fxp"), "Bass");
  assert.equal(categoryForFile("DR-Commando1.fxp"), "FX");
});

test("categoryForFile manda in Seq i sequence e in User gli INIT", () => {
  assert.equal(categoryForFile("SQ-Spindizzy7.fxp"), "Seq");
  assert.equal(categoryForFile("INIT-neochip-base1.fxp"), "User");
});

test("categoryForFile non inventa una categoria per un prefisso mai visto", () => {
  assert.equal(categoryForFile("ZZ-qualcosa.fxp"), null);
  assert.equal(categoryForFile("senzaprefisso.fxp"), null);
});

test("presetNameFrom toglie il prefisso di categoria e separa le parole attaccate", () => {
  assert.equal(presetNameFrom("BS-RacingDestructionKit5"), "Racing Destruction Kit 5");
  assert.equal(presetNameFrom("PD-Uridium1"), "Uridium 1");
  assert.equal(presetNameFrom("FX-Spindizzy"), "Spindizzy");
});

test("presetNameFrom tiene insieme un acronimo iniziale", () => {
  assert.equal(presetNameFrom("FX-GGSisters4"), "GG Sisters 4");
});

test("presetNameFrom non toglie un trattino che non separa un prefisso noto", () => {
  assert.equal(presetNameFrom("neochip-base1"), "Neochip Base 1");
});

test("presetNameFrom scarta la punteggiatura finale che il pack usa come variante", () => {
  assert.equal(presetNameFrom("FX-RacingDestructionKit6-"), "Racing Destruction Kit 6");
  assert.equal(presetNameFrom("FX-RacingDestructionKit8+"), "Racing Destruction Kit 8");
});

test("dedupeNames lascia stare i nomi gia' unici", () => {
  assert.deepEqual(dedupeNames(["Uridium 1", "Spindizzy"]), ["Uridium 1", "Spindizzy"]);
});

test("dedupeNames numera le ripetizioni tenendo il primo intatto", () => {
  assert.deepEqual(
    dedupeNames(["Racing Destruction Kit 6", "Spindizzy", "Racing Destruction Kit 6", "Racing Destruction Kit 6"]),
    ["Racing Destruction Kit 6", "Spindizzy", "Racing Destruction Kit 6 2", "Racing Destruction Kit 6 3"],
  );
});

test("dedupeNames non collide con i nomi gia' presi dai preset scritti a mano", () => {
  assert.deepEqual(dedupeNames(["Init", "Glass Pad"], ["Init", "Glass Pad"]), ["Init 2", "Glass Pad 2"]);
});

test("seededUnits ridA' gli stessi numeri per lo stesso nome", () => {
  assert.deepEqual(seededUnits("Racing Destruction Kit 5", 4), seededUnits("Racing Destruction Kit 5", 4));
});

test("seededUnits distingue due nomi vicini", () => {
  assert.notDeepEqual(seededUnits("Spindizzy 7", 4), seededUnits("Spindizzy 8", 4));
});

test("seededUnits sta dentro [0,1) e rende la lunghezza chiesta", () => {
  const units = seededUnits("Uridium 1", 16);
  assert.equal(units.length, 16);
  for (const u of units) assert.ok(u >= 0 && u < 1, `fuori range: ${u}`);
  assert.equal(new Set(units).size > 1, true, "non devono essere tutti lo stesso numero");
});

test("buildPresetValues punta alla wavetable trovata nel file", () => {
  const values = buildPresetValues("Lead", "retro-commando", "Commando 18");
  assert.equal(values.wtIndex, "retro-commando");
});

test("buildPresetValues tiene level, drive e volume fermi dentro la categoria", () => {
  // Sono i tre che decidono se clippa: PresetGainStagingTests suona ogni preset di
  // presets.json e pretende un margine. Lo scarto per nome non deve toccarli.
  const a = buildPresetValues("Pad", "retro-uridium-pad", "Uridium 1");
  const b = buildPresetValues("Pad", "retro-racing", "Leaderboard 0");
  for (const id of ["level", "drive", "volume"]) assert.equal(a[id], b[id], id);
});

test("buildPresetValues distingue due preset della stessa categoria con la stessa tavola", () => {
  const a = buildPresetValues("FX", "retro-racing", "Spindizzy 2");
  const b = buildPresetValues("FX", "retro-racing", "Spindizzy 3");
  assert.notDeepEqual(a, b);
});

test("buildPresetValues resta dentro 0..1 su ogni parametro numerico", () => {
  for (const cat of Object.keys(TEMPLATES))
    for (const seed of ["A", "Zzz 9", "Racing Destruction Kit 6"]) {
      const values = buildPresetValues(cat, "retro-racing", seed);
      for (const [id, v] of Object.entries(values))
        if (typeof v === "number") assert.ok(v >= 0 && v <= 1, `${cat}/${seed}: ${id} = ${v}`);
    }
});

test("buildPresetValues accende l'arpeggiatore solo per i Seq", () => {
  // I bool si scrivono 0/1 come ogni altro valore normalizzato: presets.json non ha un tipo
  // booleano, e gen-params rifiuta un `true` come valore fuori range.
  assert.equal(buildPresetValues("Seq", "retro-racing", "Spindizzy 7").arpOn, 1);
  assert.equal("arpOn" in buildPresetValues("Lead", "retro-racing", "Spindizzy 7"), false);
});

test("buildPresetValues rifiuta una categoria senza modello", () => {
  assert.throws(() => buildPresetValues("Drums", "retro-racing", "X"), /Drums/);
});

/** Un .fxp come quelli del pack: header FPCh, stato Serum finto, poi la tavola in float32. */
function makeFxp(headerName, { frames = 64, tableBytes = null } = {}) {
  const state = deflateSync(Buffer.alloc(64, 7));
  const table =
    tableBytes === null ? deflateSync(Buffer.alloc(frames * 2048 * 4)) : deflateSync(tableBytes);
  const chunk = Buffer.concat([state, table]);

  const head = Buffer.alloc(60);
  head.write("CcnK", 0, "ascii");
  head.writeUInt32BE(chunk.length + 52, 4);
  head.write("FPCh", 8, "ascii");
  head.writeUInt32BE(1, 12);
  head.write("XfsX", 16, "ascii");
  head.write(headerName, 28, 28, "ascii");
  head.writeUInt32BE(chunk.length, 56);
  return Buffer.concat([head, chunk]);
}

const classifyFake = (sha1) => {
  if (sha1 === "aaaaaaaa") return { kind: "known", slug: "retro-racing" };
  if (sha1 === "bbbbbbbb") return { kind: "duplicate", slug: "retro-spindizzy", duplicateOf: "retro-racing" };
  return { kind: "unknown" };
};
const alwaysKnown = () => ({ kind: "known", slug: "retro-racing" });
const alwaysUnknown = () => ({ kind: "unknown" });

test("collectPackPresets costruisce un preset per ogni file con una tavola nota", () => {
  const { presets } = collectPackPresets([{ name: "LD-Commando18.fxp", buffer: makeFxp("LD-Commando18") }], { classify: alwaysKnown });
  assert.equal(presets.length, 1);
  assert.equal(presets[0].name, "Commando 18");
  assert.equal(presets[0].cat, "Lead");
  assert.equal(presets[0].values.wtIndex, "retro-racing");
});

test("collectPackPresets fa puntare un duplicato noto alla tavola davvero spedita", () => {
  const files = [{ name: "SQ-Spindizzy7.fxp", buffer: makeFxp("SQ-Spindizzy7") }];
  const { presets } = collectPackPresets(files, { classify: () => classifyFake("bbbbbbbb") });
  assert.equal(presets[0].values.wtIndex, "retro-racing");
});

test("collectPackPresets scarta i file la cui tavola non e' fra quelle importate", () => {
  const files = [{ name: "BS-Syd4.fxp", buffer: makeFxp("BS-Syd4", { frames: 4 }) }];
  const { presets, skipped } = collectPackPresets(files, { classify: alwaysUnknown });
  assert.equal(presets.length, 0);
  assert.equal(skipped.length, 1);
  assert.match(skipped[0].reason, /tavola/);
});

test("collectPackPresets scarta i file senza tavola incorporata", () => {
  const files = [{ name: "BS-airwolf.fxp", buffer: makeFxp("BS-airwolf", { tableBytes: Buffer.alloc(0) }) }];
  const { presets, skipped } = collectPackPresets(files, { classify: alwaysKnown });
  assert.equal(presets.length, 0);
  assert.equal(skipped.length, 1);
});

test("collectPackPresets scarta un prefisso che non sa in che categoria mettere", () => {
  const files = [{ name: "ZZ-mistero.fxp", buffer: makeFxp("ZZ-mistero") }];
  const { presets, skipped } = collectPackPresets(files, { classify: alwaysKnown });
  assert.equal(presets.length, 0);
  assert.match(skipped[0].reason, /prefisso/);
});

test("collectPackPresets rende unici i nomi che il pack ripete", () => {
  const files = [
    { name: "FX-RacingDestructionKit6-.fxp", buffer: makeFxp("FX-RacingDestructionKit6-") },
    { name: "BS-RacingDestructionKit6-.fxp", buffer: makeFxp("BS-RacingDestructionKit6-") },
  ];
  const { presets } = collectPackPresets(files, { classify: alwaysKnown });
  assert.deepEqual(presets.map((p) => p.name), ["Racing Destruction Kit 6", "Racing Destruction Kit 6 2"]);
});

test("buildPresetValues abbassa il livello sulle tavole piu' calde", () => {
  const quiet = buildPresetValues("Lead", "retro-racing", "X", 1.0);
  const loud = buildPresetValues("Lead", "retro-racing", "X", 0.5);
  assert.equal(loud.level, round4Test(quiet.level * 0.5));
  assert.deepEqual({ ...loud, level: 0 }, { ...quiet, level: 0 }, "il fattore tocca solo il livello");
});

test("buildPresetValues non manda il livello fuori 0..1 con un fattore assurdo", () => {
  assert.ok(buildPresetValues("Lead", "retro-racing", "X", 99).level <= 1);
});

const round4Test = (x) => Math.round(x * 1e4) / 1e4;

test("levelScaleFromRms confronta la tavola con il riferimento, non con se stessa", () => {
  assert.equal(levelScaleFromRms(0.3305, 0.3305), 1);
  assert.ok(levelScaleFromRms(0.5361, 0.3305) < 1, "una tavola piu' calda del riferimento abbassa il livello");
  assert.ok(levelScaleFromRms(0.2361, 0.3305) > 1, "una piu' fredda lo alza");
});

test("collectPackPresets applica alla tavola trovata il fattore di livello che le tocca", () => {
  const files = [{ name: "LD-Commando18.fxp", buffer: makeFxp("LD-Commando18") }];
  const full = collectPackPresets(files, { classify: alwaysKnown });
  const halved = collectPackPresets(files, { classify: alwaysKnown, levelScaleBySlug: { "retro-racing": 0.5 } });
  assert.equal(halved.presets[0].values.level, round4Test(full.presets[0].values.level * 0.5));
});

test("collectPackPresets tiene conto dei nomi gia' presi anche quando arrivano per opzione", () => {
  const files = [{ name: "FX-Spindizzy.fxp", buffer: makeFxp("FX-Spindizzy") }];
  const { presets } = collectPackPresets(files, { classify: alwaysKnown, taken: ["Spindizzy"] });
  assert.equal(presets[0].name, "Spindizzy 2");
});
