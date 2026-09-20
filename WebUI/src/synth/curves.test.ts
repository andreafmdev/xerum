import { describe, expect, it } from "vitest";
import { tableSample, filterPath, envPath, lfoPath, spectrum } from "./curves";
import { WAVETABLES } from "./wavetables.generated";

/** Due frame piatti a -1 e +1: ogni valore letto dice da solo dove si trova. */
const flat: number[][] = [
  new Array(8).fill(-1),
  new Array(8).fill(1),
];

/** Un frame con un dente: serve a vedere che la fase conta. */
const ramp: number[][] = [Array.from({ length: 8 }, (_, i) => i / 4 - 1)];

describe("tableSample", () => {
  it("legge il primo e l'ultimo frame agli estremi di pos", () => {
    expect(tableSample(flat, 0, 0, 0)).toBeCloseTo(-1);
    expect(tableSample(flat, 1, 0, 0)).toBeCloseTo(1);
  });

  it("interpola fra due frame adiacenti", () => {
    expect(tableSample(flat, 0.5, 0, 0)).toBeCloseTo(0);
  });

  it("segue la fase dentro il frame", () => {
    expect(tableSample(ramp, 0, 0, 0)).toBeCloseTo(-1);
    expect(tableSample(ramp, 0, 0.5, 0)).toBeCloseTo(0);
  });

  it("il warp accelera la fase", () => {
    expect(tableSample(ramp, 0, 0.25, 1)).toBeCloseTo(tableSample(ramp, 0, 1, 0));
  });

  it("resta nei limiti per qualsiasi pos e t", () => {
    for (let p = 0; p <= 1; p += 0.1)
      for (let t = 0; t <= 1; t += 0.1) {
        const s = tableSample(flat, p, t, 0.5);
        expect(s).toBeGreaterThanOrEqual(-1.001);
        expect(s).toBeLessThanOrEqual(1.001);
      }
  });
});

describe("tableSample su tavole vere", () => {
  it("disegna tavole diverse per wtIndex diversi", () => {
    // Era in SynthWindow.test.tsx, ma non renderizza ne' SynthWindow ne' WaveDisplay: e' un
    // confronto fra due tavole di wavetables.generated.ts letto da tableSample, non un test di
    // rendering. Appartiene qui, dove sta il resto di tableSample.
    const a = WAVETABLES.find((w) => w.value === "basic")!.frames;
    const b = WAVETABLES.find((w) => w.value === "retro-racing")!.frames;
    const differs = Array.from({ length: 64 }, (_, i) => Math.abs(tableSample(a, 0.5, i / 64, 0) - tableSample(b, 0.5, i / 64, 0)));
    expect(Math.max(...differs)).toBeGreaterThan(0.05);
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
  it("restituisce N magnitudini non negative", () => {
    const s = spectrum(ramp, 0, 0, 1, 16);
    expect(s).toHaveLength(16);
    s.forEach((m) => expect(m).toBeGreaterThanOrEqual(0));
  });

  it("scala col level", () => {
    expect(spectrum(ramp, 0.3, 0.2, 0.5, 8)[0]).toBeCloseTo(spectrum(ramp, 0.3, 0.2, 1, 8)[0]! / 2);
  });

  it("una tavola piatta non ha armoniche", () => {
    expect(spectrum(flat, 0, 0, 1, 8).every((m) => m < 1e-6)).toBe(true);
  });

  it("una tavola non piatta ha almeno un'armonica non nulla", () => {
    // I tre test sopra passerebbero anche se `spectrum` tornasse sempre zero: `>= 0` e
    // `< 1e-6` sono veri per lo zero, e `0 ≈ 0/2` anche. Questo e' l'unico che fallirebbe.
    const s = spectrum(ramp, 0, 0, 1, 8);
    expect(Math.max(...s)).toBeGreaterThan(0);
  });
});

describe("envPath curve", () => {
  const seg = (d: string) => d.split(" C")[1]!.split(",").map((p) => p.trim().split(" ").map(Number));
  it("curve 0 (o assente) è la forma di sempre", () => {
    expect(envPath(0.2, 0.3, 0.7, 0.4, 200, 70, 0)).toBe(envPath(0.2, 0.3, 0.7, 0.4, 200, 70));
  });
  it("curve +1 mette i punti di controllo dell'attacco sulla corda (retta)", () => {
    const d = envPath(0.2, 0.3, 0.7, 0.4, 200, 70, 1);
    const [c1, c2, end] = seg(d);
    const x0 = 6, y0 = 64;
    const slope = (end![1]! - y0) / (end![0]! - x0);
    expect(Math.abs(c1![1]! - (y0 + slope * (c1![0]! - x0)))).toBeLessThan(0.3);
    expect(Math.abs(c2![1]! - (y0 + slope * (c2![0]! - x0)))).toBeLessThan(0.3);
  });
  it("curve -1 piega l'attacco più di curve 0", () => {
    const flat = seg(envPath(0.2, 0.3, 0.7, 0.4, 200, 70, 0));
    const sharp = seg(envPath(0.2, 0.3, 0.7, 0.4, 200, 70, -1));
    expect(sharp[0]![1]!).toBeLessThan(flat[0]![1]!); // più in alto (y più piccola) = più vicino al picco
  });
});
