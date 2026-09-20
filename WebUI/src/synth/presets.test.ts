import { describe, expect, it } from "vitest";
import { PRESETS, CATEGORIES, filterPresets, presetWave, step } from "./presets";
import { PARAM_SPECS, type ParamId } from "./params.generated";
import { WAVETABLES } from "./wavetables.generated";

describe("presets", () => {
  it("ships the catalogue", () => {
    expect(PRESETS.length).toBeGreaterThan(10);
    expect(CATEGORIES[0]).toBe("All");
  });
  it("filters by category and query, case-insensitive", () => {
    expect(filterPresets(PRESETS, "All", "").length).toBe(PRESETS.length);
    expect(filterPresets(PRESETS, "Bass", "").every((p) => p.cat === "Bass")).toBe(true);
    expect(filterPresets(PRESETS, "All", "glass").map((p) => p.name)).toEqual(["Glass Pad"]);
  });
  it("step cycles an index with wrap", () => {
    expect(step(0, -1, 5)).toBe(4);
    expect(step(4, 1, 5)).toBe(0);
    expect(step(2, 1, 5)).toBe(3);
  });

  it("ogni preset generato ha un nome, una categoria e solo parametri esistenti", () => {
    expect(PRESETS.length).toBeGreaterThanOrEqual(12);
    for (const preset of PRESETS) {
      expect(preset.name).toBeTruthy();
      expect(preset.cat).toBeTruthy();
      for (const [id, value] of Object.entries(preset.values)) {
        expect(PARAM_SPECS[id as ParamId], `parametro sconosciuto: ${id}`).toBeDefined();
        expect(value).toBeGreaterThanOrEqual(0);
        expect(value).toBeLessThanOrEqual(1);
      }
    }
  });

  it("presetWave porta la tavola del preset", () => {
    const withTable = PRESETS.find((p) => p.values.wtIndex !== undefined)!;
    const { frames } = presetWave(withTable);
    expect(frames).toBe(WAVETABLES[Math.round(withTable.values.wtIndex! * (WAVETABLES.length - 1))]!.frames);
  });

  it("un preset che non tocca wtIndex prende la tavola di default", () => {
    const init = PRESETS.find((p) => p.name === "Init")!;
    expect(presetWave(init).frames).toBe(WAVETABLES[0]!.frames);
  });
});
