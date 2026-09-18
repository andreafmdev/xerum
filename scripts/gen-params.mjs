#!/usr/bin/env node
// Genera ParameterTable.h e params.generated.ts da Source/parameters/parameters.json.
// Uso: node scripts/gen-params.mjs   (oppure: cd WebUI && pnpm gen:params)
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const KIND = { float: "Float", int: "Int", bool: "Bool", choice: "Choice" };
const MAP = { linear: "Linear", log: "Log", db: "Db", "ms-squared": "MsSquared" };
const LABEL = { hz: "Hz", time: "Time", pan: "Pan", signed: "Signed", "arp-rate": "ArpRate" };

const cstr = (s) => (s == null ? "nullptr" : JSON.stringify(s));
const f = (n) => `${Number(n)}f`.replace(/^(-?\d+)f$/, "$1.0f");

export function generate(json) {
  const ps = json.params;
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
    "  map?: { type: MapType; min: number; max: number; offset?: number };",
    "  default: number | boolean; unit?: string; decimals?: number; labelKind?: LabelKind; bipolar?: boolean;",
    "  options?: { value: string; label: string }[];",
    "}",
    `export const GROUPS: Record<string, string> = ${JSON.stringify(json.groups)};`,
    `export const PARAM_IDS = ${JSON.stringify(ps.map((p) => p.id))} as const satisfies readonly ParamId[];`,
    "export const PARAM_SPECS: Record<ParamId, ParamSpec> = {",
    ...ps.map((p) => `  ${JSON.stringify(p.id)}: ${JSON.stringify(p)},`),
    "};",
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
}
