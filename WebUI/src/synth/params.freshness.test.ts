import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { generate } from "../../../scripts/gen-params.mjs";

const root = resolve(__dirname, "../../..");
const json = JSON.parse(readFileSync(resolve(root, "Source/parameters/parameters.json"), "utf8"));

describe("generated parameter files", () => {
  const out = generate(json);
  it("params.generated.ts is up to date (run: pnpm gen:params)", () => {
    expect(readFileSync(resolve(root, "WebUI/src/synth/params.generated.ts"), "utf8")).toBe(out.ts);
  });
  it("ParameterTable.h is up to date (run: pnpm gen:params)", () => {
    expect(readFileSync(resolve(root, "Source/parameters/ParameterTable.h"), "utf8")).toBe(out.header);
  });
  it("ids are unique and choice params have ≥ 2 options", () => {
    const ids = json.params.map((p: { id: string }) => p.id);
    expect(new Set(ids).size).toBe(ids.length);
    for (const p of json.params) if (p.kind === "choice") expect(p.options.length).toBeGreaterThanOrEqual(2);
  });
});
