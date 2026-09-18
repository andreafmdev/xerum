import { describe, expect, it } from "vitest";
import { PRESETS, CATEGORIES, filterPresets, step } from "./presets";

describe("presets", () => {
  it("ships the catalogue", () => {
    expect(PRESETS.length).toBeGreaterThan(10);
    expect(CATEGORIES[0]).toBe("All");
  });
  it("filters by category and query, case-insensitive", () => {
    expect(filterPresets(PRESETS, "All", "").length).toBe(PRESETS.length);
    expect(filterPresets(PRESETS, "Bass", "").every((p) => p.cat === "Bass")).toBe(true);
    expect(filterPresets(PRESETS, "All", "glass")).toEqual([{ name: "Glass Pad", cat: "Pad" }]);
  });
  it("step cycles an index with wrap", () => {
    expect(step(0, -1, 5)).toBe(4);
    expect(step(4, 1, 5)).toBe(0);
    expect(step(2, 1, 5)).toBe(3);
  });
});
