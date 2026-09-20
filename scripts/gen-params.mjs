#!/usr/bin/env node
// Genera ParameterTable.h, params.generated.ts, PresetTable.h e presets.generated.ts
// da Source/parameters/parameters.json e Source/parameters/presets.json.
// Uso: node scripts/gen-params.mjs   (oppure: cd WebUI && pnpm gen:params)
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const KIND = { float: "Float", int: "Int", bool: "Bool", choice: "Choice" };
const MAP = { linear: "Linear", log: "Log", db: "Db", "ms-squared": "MsSquared" };
const LABEL = { hz: "Hz", time: "Time", pan: "Pan", signed: "Signed", "arp-rate": "ArpRate" };

const cstr = (s) => (s == null ? "nullptr" : JSON.stringify(s));
const f = (n) => `${Number(n)}f`.replace(/^(-?\d+)f$/, "$1.0f");

/**
 * Il dominio in cui vive `default` per ogni kind — che NON è lo stesso per tutti, ed è la
 * ragione per cui questo controllo esiste. Per Kind::Float e Kind::Bool l'APVTS tiene il valore
 * normalizzato 0..1 (ParameterMapping.h li crea con NormalisableRange {0,1}), quindi `default`
 * è 0..1 e `map` descrive solo come si legge; per Kind::Int il parametro vive nel suo range
 * naturale, quindi `default` sta in map.min..map.max; per Kind::Choice è l'indice dell'opzione.
 * Confondere questi tre domini è esattamente il bug delle quattro ottave, in forma di dato.
 */
const chunk = (xs, n) => xs.reduce((acc, x, i) => (i % n === 0 ? acc.push([x]) : acc[acc.length - 1].push(x), acc), []);

const defaultDomain = (p) => {
  if (p.kind === "int") return [p.map.min, p.map.max];
  if (p.kind === "choice") return [0, (p.options?.length ?? 1) - 1];
  return [0, 1];
};

/**
 * Convalida alla generazione ciò che altrimenti si scoprirebbe a runtime nel plugin: è l'assert
 * che Vital tiene nel costruttore della sua tabella, spostato di qualche ora più in là.
 */
export function validate(ps) {
  const seen = new Set();

  for (const [i, p] of ps.entries()) {
    const where = `parametro #${i} ("${p.id}")`;

    if (typeof p.id !== "string" || p.id === "") throw new Error(`parametro #${i}: id mancante`);
    if (seen.has(p.id)) throw new Error(`${where}: id duplicato`);
    seen.add(p.id);

    // Dichiarazione obbligatoria, non opzionale: chi aggiunge un parametro deve *decidere* se il
    // motore lo legge. Senza questo campo lo slot non verrebbe generato e collectEngineParams
    // leggerebbe un parametro che non esiste — in silenzio.
    if (typeof p.slot !== "boolean")
      throw new Error(`${where}: manca "slot" (true = ha uno slot params::ParamSlot letto da collectEngineParams, false = no)`);

    if (p.kind === "int" && !p.map) throw new Error(`${where}: un kind "int" deve avere "map" (è il suo range naturale)`);
    if (p.map && !(Number(p.map.min) < Number(p.map.max)))
      throw new Error(`${where}: map.min (${p.map.min}) deve essere < map.max (${p.map.max})`);
    if (p.kind === "choice" && (p.options?.length ?? 0) < 2)
      throw new Error(`${where}: un kind "choice" deve avere almeno 2 opzioni`);

    const def = typeof p.default === "boolean" ? (p.default ? 1 : 0) : p.default;
    if (typeof def !== "number" || !Number.isFinite(def)) throw new Error(`${where}: default assente o non numerico`);

    // Un target del mod matrix e' un valore normalizzato 0..1 su cui SynthVoice somma depth x
    // livello: solo Kind::Float ha quel dominio (l'engine lo ribadisce con uno static_assert), e
    // senza slot il motore non lo leggerebbe. E' la guardia del "bug delle quattro ottave" al
    // livello del dato.
    if (p.modTarget !== undefined && typeof p.modTarget !== "boolean") throw new Error(`${where}: "modTarget" deve essere booleano`);
    if (p.modTarget && p.kind !== "float") throw new Error(`${where}: modTarget su un kind "${p.kind}": solo i float hanno un valore normalizzato modulabile`);
    if (p.modTarget && !p.slot) throw new Error(`${where}: modTarget richiede "slot": true, altrimenti il motore non legge il parametro`);

    const [lo, hi] = defaultDomain(p);
    if (def < lo || def > hi)
      throw new Error(`${where}: default ${def} fuori da ${lo}..${hi} (dominio del kind "${p.kind}")`);
  }
}

export function generate(json) {
  const ps = json.params;
  validate(ps);
  const slots = ps.filter((p) => p.slot);
  const targets = ps.filter((p) => p.modTarget);
  const header = [
    "// GENERATED da Source/parameters/parameters.json — non modificare a mano.",
    "// Rigenera con: node scripts/gen-params.mjs",
    "#pragma once",
    "",
    "namespace params",
    "{",
    "enum class Kind { Float, Int, Bool, Choice };",
    "enum class Map { None, Linear, Log, Db, MsSquared };",
    "enum class Label { None, Hz, Time, Pan, Signed, ArpRate };",
    "",
    "struct Spec",
    "{",
    "    const char* id; const char* name; const char* group;",
    "    Kind kind; Map map; float min; float max; float offset; float def;",
    "    const char* unit; int decimals; Label label;",
    "    const char* const* options; int numOptions;",
    "};",
    "",
    ...ps.filter((p) => p.options).map((p) =>
      `inline constexpr const char* const kOptions_${p.id}[] = { ${p.options.map((o) => cstr(o.label)).join(", ")} };`),
    // I `value` accanto alle label: sono gli id stabili delle opzioni (la WebUI li salva, il C++
    // ci lega i file delle wavetable), le label sono solo testo.
    ...ps.filter((p) => p.options).map((p) =>
      `inline constexpr const char* const kOptionValues_${p.id}[] = { ${p.options.map((o) => cstr(o.value)).join(", ")} };`),
    "",
    `inline constexpr int kNumParams = ${ps.length};`,
    "inline constexpr Spec kTable[kNumParams] = {",
    ...ps.map((p) => {
      const m = p.map ?? {};
      const def = typeof p.default === "boolean" ? (p.default ? 1 : 0) : p.default;
      return `    { ${cstr(p.id)}, ${cstr(p.name)}, ${cstr(p.group)}, Kind::${KIND[p.kind]}, Map::${p.map ? MAP[m.type] : "None"}, ${f(m.min ?? 0)}, ${f(m.max ?? 1)}, ${f(m.offset ?? 0)}, ${f(def)}, ${cstr(p.unit)}, ${p.decimals ?? 0}, Label::${p.labelKind ? LABEL[p.labelKind] : "None"}, ${p.options ? `kOptions_${p.id}` : "nullptr"}, ${p.options?.length ?? 0} },`;
    }),
    "};",
    "",
    "inline constexpr const Spec* find (const char* id) noexcept",
    "{",
    "    for (const auto& s : kTable)",
    "    {",
    "        const char* a = s.id; const char* b = id;",
    "        while (*a != 0 && *a == *b) { ++a; ++b; }",
    "        if (*a == 0 && *b == 0) return &s;",
    "    }",
    "    return nullptr;",
    "}",
    "",
    "// --- slot del motore -------------------------------------------------------------------",
    "//",
    "// Identifica un parametro grezzo senza passare per il suo nome: chi implementa l'accessore",
    "// risolve `id -> puntatore` una volta sola alla costruzione (vedi PluginProcessor::paramSlots_,",
    "// che cicla su kSlotIds), e qui dentro e' solo un indice di array.",
    "//",
    "// Ci sono soltanto i parametri con \"slot\": true in parameters.json, cioe' quelli che il motore",
    "// legge una volta per blocco attraverso params::collectEngineParams. Gli altri o non sono",
    "// ancora cablati, o viaggiano per conto loro (wtIndex e volume, vedi PluginProcessor).",
    "//",
    "// L'ordine e' quello di parameters.json, ma resta un dettaglio interno fra questo header e chi",
    "// scrive l'accessore, non un ABI pubblico: nessuno stato salvato contiene un indice di slot.",
    "// Non coincide con l'indice dentro kTable, perche' i parametri senza slot creano dei buchi:",
    "// per passare dall'uno all'altro c'e' specForSlot().",
    `inline constexpr int kNumSlots = ${slots.length};`,
    "",
    "enum class ParamSlot : int",
    "{",
    ...chunk(slots.map((p) => p.id), 8).map((row) => `    ${row.join(", ")},`),
    "    count",
    "};",
    "",
    "static_assert ((int) ParamSlot::count == kNumSlots, \"enum e conteggio devono coincidere\");",
    "",
    "/** L'id del parametro di ogni slot, nello stesso ordine dell'enum. */",
    `inline constexpr const char* kSlotIds[kNumSlots] = {`,
    ...chunk(slots.map((p) => cstr(p.id)), 8).map((row) => `    ${row.join(", ")},`),
    "};",
    "",
    "/** L'indice dentro kTable di ogni slot: kTable[kSlotTableIndex[i]].id e' kSlotIds[i]. */",
    "inline constexpr int kSlotTableIndex[kNumSlots] = {",
    ...chunk(slots.map((p) => String(ps.indexOf(p))), 16).map((row) => `    ${row.join(", ")},`),
    "};",
    "",
    "// --- target del mod matrix -----------------------------------------------------------",
    "//",
    "// I parametri con \"modTarget\": true, nell'ordine di parameters.json. L'indice qui dentro e' la",
    "// valuta con cui il thread audio somma le modulazioni (EngineParams::modBase); gli id testuali",
    "// sono quelli che la UI scrive nel nodo MODS e che state::buildModSnapshot risolve. Solo",
    "// Kind::Float con slot: lo impone il generatore, e engine/ModMatrix.h lo ribadisce a compile",
    "// time. Nessuno stato salvato contiene un indice: riordinare il JSON non rompe un preset.",
    `inline constexpr ParamSlot kModTargets[] = { ${targets.map((p) => `ParamSlot::${p.id}`).join(", ")} };`,
    `inline constexpr const char* kModTargetIds[] = { ${targets.map((p) => cstr(p.id)).join(", ")} };`,
    "",
    "/** La spec del parametro dietro uno slot. constexpr: non costa niente a runtime. */",
    "inline constexpr const Spec& specForSlot (ParamSlot s) noexcept",
    "{",
    "    return kTable[kSlotTableIndex[(int) s]];",
    "}",
    "} // namespace params",
    "",
  ].join("\n");

  const ts = [
    "// GENERATED da Source/parameters/parameters.json — non modificare a mano.",
    "// Rigenera con: pnpm gen:params",
    "",
    `export type ParamId = ${ps.map((p) => JSON.stringify(p.id)).join(" | ")};`,
    'export type ParamKind = "float" | "int" | "bool" | "choice";',
    'export type MapType = "linear" | "log" | "db" | "ms-squared";',
    'export type LabelKind = "hz" | "time" | "pan" | "signed" | "arp-rate";',
    "export interface ParamSpec {",
    "  id: ParamId; name: string; group: string; kind: ParamKind;",
    "  /** true se il motore lo legge via params::ParamSlot (vedi ParameterTable.h). */",
    "  slot: boolean;",
    "  map?: { type: MapType; min: number; max: number; offset?: number };",
    "  default: number | boolean; unit?: string; decimals?: number; labelKind?: LabelKind; bipolar?: boolean;",
    "  /** true se il motore lo modula (params::kModTargets): la UI accetta il drop di una sorgente solo qui. */",
    "  modTarget?: boolean;",
    "  options?: { value: string; label: string }[];",
    "}",
    `export const GROUPS: Record<string, string> = ${JSON.stringify(json.groups)};`,
    `export const PARAM_IDS = ${JSON.stringify(ps.map((p) => p.id))} as const satisfies readonly ParamId[];`,
    `export const MOD_TARGETS = ${JSON.stringify(targets.map((p) => p.id))} as const satisfies readonly ParamId[];`,
    "export const PARAM_SPECS: Record<ParamId, ParamSpec> = {",
    ...ps.map((p) => `  ${JSON.stringify(p.id)}: ${JSON.stringify(p)},`),
    "};",
    "",
  ].join("\n");

  return { header, ts };
}

// Genera PresetTable.h e presets.generated.ts da presets.json, convalidando ogni
// preset contro l'insieme di id di parameters.json: un id sconosciuto o un valore
// fuori range 0..1 fanno fallire subito la generazione, non a runtime nel plugin.
export function generatePresets(presetsJson, paramsJson) {
  const specById = new Map(paramsJson.params.map((p) => [p.id, p]));
  const presets = presetsJson.presets;

  // I choice si scrivono per nome dell'opzione, non per valore normalizzato: `indice /
  // (numOpzioni - 1)` cambia sotto i piedi ogni volta che si aggiunge un'opzione, e un
  // preset scritto ieri punterebbe in silenzio a un'altra wavetable. Il nome no.
  const resolve_ = (presetName, id, value) => {
    const spec = specById.get(id);
    if (!spec) throw new Error(`preset "${presetName}": parametro sconosciuto ${id}`);

    if (spec.kind === "choice") {
      if (typeof value !== "string")
        throw new Error(`preset "${presetName}": ${id} e' un choice e va scritto col nome dell'opzione, non con ${JSON.stringify(value)}`);
      const index = spec.options.findIndex((o) => o.value === value);
      if (index < 0)
        throw new Error(`preset "${presetName}": ${id} non ha l'opzione "${value}" (ci sono: ${spec.options.map((o) => o.value).join(", ")})`);
      return spec.options.length === 1 ? 0 : index / (spec.options.length - 1);
    }

    if (typeof value !== "number" || value < 0 || value > 1)
      throw new Error(`preset "${presetName}": valore fuori range 0..1 per ${id}: ${JSON.stringify(value)}`);
    return value;
  };

  const resolved = presets.map((p) => ({
    ...p,
    values: Object.fromEntries(Object.entries(p.values ?? {}).map(([id, v]) => [id, resolve_(p.name, id, v)])),
  }));

  const header = [
    "#pragma once",
    "",
    "// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.",
    "// Non modificare a mano.",
    "",
    "namespace params",
    "{",
    "struct PresetValue",
    "{",
    "    const char* id;",
    "    float value;",
    "};",
    "",
    "struct Preset",
    "{",
    "    const char* name;",
    "    const char* category;",
    "    const PresetValue* values;",
    "    int numValues;",
    "};",
    "",
    ...resolved.map((p, i) => {
      const entries = Object.entries(p.values ?? {});
      if (entries.length === 0) return `inline constexpr const PresetValue* kPreset${i}Values = nullptr;`;
      return `inline constexpr PresetValue kPreset${i}Values[] = { ${entries.map(([id, v]) => `{ ${cstr(id)}, ${f(v)} }`).join(", ")} };`;
    }),
    "",
    `inline constexpr int kNumPresets = ${resolved.length};`,
    "inline constexpr Preset kPresetTable[kNumPresets] = {",
    ...resolved.map((p, i) => {
      const n = Object.entries(p.values ?? {}).length;
      return `    { ${cstr(p.name)}, ${cstr(p.cat)}, ${n === 0 ? "nullptr" : `kPreset${i}Values`}, ${n} },`;
    }),
    "};",
    "} // namespace params",
    "",
  ].join("\n");

  const ts = [
    "// GENERATO da scripts/gen-params.mjs a partire da Source/parameters/presets.json.",
    "// Non modificare a mano.",
    'import type { ParamId } from "./params.generated";',
    "",
    "export type Preset = { name: string; cat: string; values: Partial<Record<ParamId, number>> };",
    "",
    "export const PRESETS: Preset[] = [",
    ...resolved.map((p) => `  { name: ${JSON.stringify(p.name)}, cat: ${JSON.stringify(p.cat)}, values: ${JSON.stringify(p.values ?? {})} },`),
    "];",
    "",
  ].join("\n");

  return { header, ts };
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
  const json = JSON.parse(readFileSync(resolve(root, "Source/parameters/parameters.json"), "utf8"));
  const { header, ts } = generate(json);
  writeFileSync(resolve(root, "Source/parameters/ParameterTable.h"), header);
  writeFileSync(resolve(root, "WebUI/src/synth/params.generated.ts"), ts);
  console.log(`gen-params: ${json.params.length} parametri → ParameterTable.h, params.generated.ts`);

  const presetsJson = JSON.parse(readFileSync(resolve(root, "Source/parameters/presets.json"), "utf8"));
  const { header: presetHeader, ts: presetTs } = generatePresets(presetsJson, json);
  writeFileSync(resolve(root, "Source/parameters/PresetTable.h"), presetHeader);
  writeFileSync(resolve(root, "WebUI/src/synth/presets.generated.ts"), presetTs);
  console.log(`gen-params: ${presetsJson.presets.length} preset → PresetTable.h, presets.generated.ts`);
}
