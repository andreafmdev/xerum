import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { bezier, DUR, EASE, EXIT_RATIO, T } from "./motion";

const theme = readFileSync(resolve(import.meta.dirname, "theme.css"), "utf8");

describe("motion tokens", () => {
  it("mirrors every duration as a --dur-* custom property", () => {
    for (const [name, ms] of Object.entries(DUR)) {
      expect(theme).toContain(`--dur-${name}: ${ms}ms;`);
    }
  });

  it("mirrors every easing as an --ease-* custom property", () => {
    for (const [name, curve] of Object.entries(EASE)) {
      expect(theme).toContain(`--ease-${name}: ${bezier(curve)};`);
    }
  });

  it("keeps the Tailwind default duration equal to DUR.state", () => {
    expect(theme).toContain(`--default-transition-duration: ${DUR.state}ms;`);
  });

  it("derives the layer exit from the enter with the glass asymmetry ratio", () => {
    expect(T.layerOut.duration).toBeCloseTo(T.layerIn.duration * EXIT_RATIO, 5);
  });
});
