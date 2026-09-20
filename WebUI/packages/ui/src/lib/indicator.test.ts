import { describe, expect, it } from "vitest";
import { measureIndicator } from "./indicator";

/** Un elemento finto con il solo `getBoundingClientRect` che serve alla misura. */
const at = (left: number, width: number) =>
  ({ getBoundingClientRect: () => ({ left, width }) }) as unknown as HTMLElement;

describe("measureIndicator", () => {
  it("returns the item offset relative to its container", () => {
    expect(measureIndicator(at(100, 300), at(160, 80))).toEqual({ x: 60, width: 80 });
  });

  it("is zeroed when there is no layout, as in jsdom", () => {
    expect(measureIndicator(at(0, 0), at(0, 0))).toEqual({ x: 0, width: 0 });
  });
});
