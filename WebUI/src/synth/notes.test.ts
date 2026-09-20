import { describe, expect, it } from "vitest";
import { noteHz, topNote } from "./notes";

describe("topNote", () => {
  it("is null when nothing plays", () => {
    expect(topNote([0, 0, 0, 0])).toBeNull();
  });

  it("returns the highest lit bit across the four words", () => {
    expect(topNote([1 << 5, 0, 0, 0])).toBe(5);
    expect(topNote([1 << 5, 1 << 3, 0, 0])).toBe(35);
    expect(topNote([0, 0, 0, 1 << 31])).toBe(127);
  });

  it("survives a word whose bit 31 makes it negative as int32", () => {
    // 1 << 31 e' -2147483648 come int32: e' il bit della nota 31.
    expect(topNote([1 << 31, 0, 0, 0])).toBe(31);
  });
});

describe("noteHz", () => {
  it("tunes A4 to 440 and octaves to powers of two", () => {
    expect(noteHz(69)).toBe(440);
    expect(noteHz(81)).toBeCloseTo(880);
    expect(noteHz(57)).toBeCloseTo(220);
  });
});
