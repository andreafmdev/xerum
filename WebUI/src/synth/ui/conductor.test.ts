import { describe, expect, it } from "vitest";
import { ZERO_METERS } from "../../juce/backend";
import { writeMeterVars } from "./conductor";

describe("writeMeterVars", () => {
  it("writes the three ambient variables as unitless numbers", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, out: 0.7, env: 0.25, lfo: -0.5 });
    expect(el.style.getPropertyValue("--m-out")).toBe("0.7");
    expect(el.style.getPropertyValue("--m-env")).toBe("0.25");
    // L'LFO è bipolare: l'ambient ne usa il modulo, perché serve una luminosità.
    expect(el.style.getPropertyValue("--m-lfo")).toBe("0.5");
  });

  it("clamps into 0..1, so a rogue frame cannot blow the brightness out", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, out: 4, env: Number.NaN, lfo: -9 });
    expect(el.style.getPropertyValue("--m-out")).toBe("1");
    expect(el.style.getPropertyValue("--m-env")).toBe("0");
    expect(el.style.getPropertyValue("--m-lfo")).toBe("1");
  });
});
