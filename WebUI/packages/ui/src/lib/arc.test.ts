import { describe, expect, it } from "vitest";
import { arcPath, knobAngles, KNOB_START, KNOB_SWEEP } from "./arc";

describe("knobAngles", () => {
  it("unipolar: from start to start + sweep*value", () => {
    expect(knobAngles(0, false)).toEqual({ start: KNOB_START, end: KNOB_START });
    expect(knobAngles(1, false)).toEqual({ start: KNOB_START, end: KNOB_START + KNOB_SWEEP });
    expect(knobAngles(0.5, false)).toEqual({ start: KNOB_START, end: 270 });
  });

  it("bipolar: from centre (270°) to the value angle, ordered", () => {
    expect(knobAngles(0.5, true)).toEqual({ start: 270, end: 270 });
    expect(knobAngles(1, true)).toEqual({ start: 270, end: 405 });
    expect(knobAngles(0.25, true)).toEqual({ start: 202.5, end: 270 });
  });
});

describe("arcPath", () => {
  it("returns an SVG arc from start to end angle", () => {
    const d = arcPath(20, 20, 16, 270, 360);
    expect(d).toMatch(/^M 20 4 A 16 16 0 0 1 36 20$/);
  });

  it("uses the large-arc flag past 180°", () => {
    expect(arcPath(20, 20, 16, 135, 405)).toContain(" 0 1 1 ");
  });

  it("swaps the angles when end < start", () => {
    expect(arcPath(20, 20, 16, 360, 270)).toBe(arcPath(20, 20, 16, 270, 360));
  });
});
