import { describe, expect, it } from "vitest";
import { sampleWave, filterPath, envPath, lfoPath, spectrum } from "./curves";

describe("sampleWave", () => {
  it("is a sine at position 0", () => {
    expect(sampleWave(0, 0.25, 0)).toBeCloseTo(1);
    expect(sampleWave(0, 0.5, 0)).toBeCloseTo(0);
  });
  it("is a saw at position 1/3", () => {
    expect(sampleWave(1 / 3, 0, 0)).toBeCloseTo(-1);
    expect(sampleWave(1 / 3, 0.5, 0)).toBeCloseTo(0);
  });
  it("is a square at 2/3", () => {
    expect(sampleWave(2 / 3, 0.25, 0)).toBeCloseTo(1);
    expect(sampleWave(2 / 3, 0.75, 0)).toBeCloseTo(-1);
  });
  it("stays within -1..1", () => {
    for (let p = 0; p <= 1; p += 0.1) for (let t = 0; t < 1; t += 0.05) {
      const s = sampleWave(p, t, 0.5);
      expect(s).toBeGreaterThanOrEqual(-1);
      expect(s).toBeLessThanOrEqual(1);
    }
  });
});

describe("paths", () => {
  it("filterPath is an M…L path spanning the width", () => {
    const d = filterPath(0.5, 0.3, "LP", 200, 60);
    expect(d.startsWith("M0.0 ")).toBe(true);
    expect(d).toContain("L200.0 ");
  });
  it("LP is high on the left and low on the right (y grows downward)", () => {
    const d = filterPath(0.5, 0, "LP", 200, 60);
    const ys = d.replace(/^M/, "").split(" L").map((p) => Number(p.split(" ")[1]));
    expect(ys[0]!).toBeLessThan(ys[ys.length - 1]!);
  });
  it("HP is the mirror", () => {
    const d = filterPath(0.5, 0, "HP", 200, 60);
    const ys = d.replace(/^M/, "").split(" L").map((p) => Number(p.split(" ")[1]));
    expect(ys[0]!).toBeGreaterThan(ys[ys.length - 1]!);
  });
  it("envPath starts and ends at the floor", () => {
    const d = envPath(0.2, 0.3, 0.7, 0.4, 200, 70);
    expect(d.startsWith("M6 64")).toBe(true);
    expect(d.endsWith(" 64")).toBe(true);
  });
  it("lfoPath has 101 points", () => {
    expect(lfoPath("Sine", 200, 70).split(" L")).toHaveLength(101);
  });
});

describe("spectrum", () => {
  it("returns N normalised magnitudes, fundamental strongest for a sine", () => {
    const s = spectrum(0, 0, 1, 16);
    expect(s).toHaveLength(16);
    expect(Math.max(...s)).toBe(s[0]);
    for (const m of s) {
      expect(m).toBeGreaterThanOrEqual(0);
      expect(m).toBeLessThanOrEqual(1);
    }
  });
  it("scales with level", () => {
    expect(spectrum(0.3, 0.2, 0.5, 8)[0]).toBeCloseTo(spectrum(0.3, 0.2, 1, 8)[0]! * 0.5);
  });
});
