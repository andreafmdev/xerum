import { describe, expect, it } from "vitest";
import { PRESETS, CATEGORIES, DESCRIPTIONS, filterPresets, presetWave, step } from "./presets";
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

  it("ogni categoria usata da un preset e' filtrabile e ha una descrizione", () => {
    // Un preset in una categoria che non sta in CATEGORIES non e' raggiungibile dal menu:
    // resta solo sotto "All". I preset generati dal pack ne hanno portata una nuova (Seq).
    for (const cat of new Set(PRESETS.map((p) => p.cat))) {
      expect(CATEGORIES, `categoria non filtrabile: ${cat}`).toContain(cat);
      expect(DESCRIPTIONS[cat], `categoria senza descrizione: ${cat}`).toBeTruthy();
    }
  });

  it("presetWave porta la tavola del preset, cercata per nome e non per aritmetica d'indice", () => {
    // Non la formula dell'implementazione riscritta qui (sarebbe tautologico: rileverebbe
    // solo se le due copie della stessa aritmetica divergessero fra loro, mai se fossero
    // sbagliate insieme). "Sub Pulse" ha "wtIndex": "pwm" in presets.json: la tavola attesa
    // e' quella con value "pwm", nome per nome.
    const subPulse = PRESETS.find((p) => p.name === "Sub Pulse")!;
    expect(subPulse.values.wtIndex).toBeDefined();
    expect(presetWave(subPulse).frames).toBe(WAVETABLES.find((w) => w.value === "pwm")!.frames);
  });

  it("un preset che non tocca wtIndex prende la tavola di default", () => {
    const init = PRESETS.find((p) => p.name === "Init")!;
    expect(presetWave(init).frames).toBe(WAVETABLES[0]!.frames);
  });
});
