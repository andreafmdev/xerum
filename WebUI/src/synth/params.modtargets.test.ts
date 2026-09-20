import { describe, expect, it } from "vitest";
import { generate, validate } from "../../../scripts/gen-params.mjs";

// `modTarget: true` in parameters.json e' la fonte unica dei target del mod matrix: da li'
// escono kModTargets/kModTargetIds (C++) e MOD_TARGETS (TS). Prima erano due tabelle a mano.
const floatParam = (id: string, extra = {}) => ({ id, name: id, group: "osc", kind: "float", slot: true, default: 0.5, map: { type: "linear", min: 0, max: 1 }, ...extra });

describe("gen-params: mod targets", () => {
  it("rifiuta modTarget su un parametro che non e' float (modBase e' un valore normalizzato 0..1)", () => {
    const ps = [floatParam("a", { modTarget: true }), { id: "b", name: "b", group: "osc", kind: "bool", slot: true, default: true, modTarget: true }];
    expect(() => validate(ps)).toThrow(/modTarget/);
  });
  it("rifiuta modTarget su un parametro senza slot", () => {
    expect(() => validate([floatParam("a", { modTarget: true, slot: false })])).toThrow(/modTarget/);
  });
  it("emette le due tabelle C++ e la lista TS nell'ordine del JSON", () => {
    const json = { groups: { osc: "Osc" }, params: [floatParam("x"), floatParam("a", { modTarget: true }), floatParam("b", { modTarget: true })] };
    const { header, ts } = generate(json);
    expect(header).toContain("inline constexpr ParamSlot kModTargets[] = { ParamSlot::a, ParamSlot::b };");
    expect(header).toContain('inline constexpr const char* kModTargetIds[] = { "a", "b" };');
    expect(ts).toContain('export const MOD_TARGETS = ["a","b"] as const satisfies readonly ParamId[];');
  });
});
