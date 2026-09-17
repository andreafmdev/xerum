import { describe, expect, it } from "vitest";
import { TONES, toneStyle } from "./tone";

describe("toneStyle", () => {
  it("returns undefined without a tone", () => {
    expect(toneStyle(undefined)).toBeUndefined();
  });

  it("maps a tone to the --tone CSS variable", () => {
    expect(toneStyle("filter")).toEqual({ "--tone": "var(--color-filter)" });
  });

  it("lists all six tones", () => {
    expect(TONES).toEqual(["osc", "filter", "env", "lfo", "fx", "master"]);
  });
});
