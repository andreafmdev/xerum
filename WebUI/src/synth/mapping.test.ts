import { describe, expect, it } from "vitest";
import { PARAM_SPECS } from "./params.generated";
import { denormalise, normalise, formatValue, toIndex, fromIndex, toInt, fromInt, paramLabel } from "./mapping";

const S = PARAM_SPECS;

describe("denormalise", () => {
  it("linear", () => expect(denormalise(S.wtpos, 0.32)).toBeCloseTo(21.16));
  it("log 20..20k", () => {
    expect(denormalise(S.cutoff, 0)).toBeCloseTo(20);
    expect(denormalise(S.cutoff, 0.5)).toBeCloseTo(632.46, 1);
    expect(denormalise(S.cutoff, 1)).toBeCloseTo(20000);
  });
  it("db with offset", () => {
    expect(denormalise(S.volume, 1)).toBeCloseTo(6);
    expect(denormalise(S.level, 0.5)).toBeCloseTo(-6.02, 1);
    expect(denormalise(S.level, 0)).toBe(-Infinity);
  });
  it("ms-squared", () => {
    expect(denormalise(S.att, 0)).toBe(1);
    expect(denormalise(S.att, 1)).toBe(8001);
    expect(denormalise(S.glide, 0.5)).toBe(500);
  });
  it("normalise inverts denormalise", () => {
    for (const id of ["wtpos", "cutoff", "level", "att"] as const)
      for (const v of [0.1, 0.5, 0.9]) expect(normalise(S[id], denormalise(S[id], v))).toBeCloseTo(v, 6);
  });
});

describe("formatValue", () => {
  it("unit + decimals", () => {
    expect(formatValue(S.warp, 0.1)).toBe("10 %");
    expect(formatValue(S.wtpos, 0.32)).toBe("21.2");
    expect(formatValue(S.drive, 0.5)).toBe("12.0 dB");
    expect(formatValue(S.fine, 1)).toBe("100 ct");
    expect(formatValue(S.fine, 0)).toBe("-100 ct");
    expect(formatValue(S.lphase, 0.5)).toBe("180°");   // i gradi restano attaccati
  });
  it("db -inf", () => {
    expect(formatValue(S.level, 0)).toBe("-inf");
    expect(formatValue(S.volume, 0.8)).toBe("4.1 dB");
  });
  it("hz switches to kHz", () => {
    expect(formatValue(S.cutoff, 0)).toBe("20 Hz");
    expect(formatValue(S.cutoff, 0.62)).toBe("1.45 kHz");
  });
  it("time switches to seconds", () => {
    expect(formatValue(S.att, 0)).toBe("1 ms");
    expect(formatValue(S.att, 1)).toBe("8.00 s");
    expect(formatValue(S.att, 0.12)).toBe("116 ms");
  });
  it("pan", () => {
    expect(formatValue(S.pan, 0.5)).toBe("C");
    expect(formatValue(S.pan, 0)).toBe("50 L");
    expect(formatValue(S.pan, 1)).toBe("50 R");
  });
  it("signed", () => {
    expect(formatValue(S.envCurve, 0.5)).toBe("0");
    expect(formatValue(S.envCurve, 1)).toBe("+100");
  });
  it("arp-rate divisions", () => {
    expect(formatValue(S.arpRate, 0)).toBe("1/32");
    expect(formatValue(S.arpRate, 1)).toBe("1/4");
  });
  it("choice and bool", () => {
    // Nota: S.ftype ha 3 opzioni (LP, HP, BP); a v=1 l'indice risultante da
    // toIndex è round(1 * (3-1)) = 2, cioè "BP". Il brief indicava "HP" per
    // errore (valore atteso a v=1 con sole 2 opzioni, non 3): corretto qui
    // perché la formula del brief per toIndex produce inequivocabilmente "BP".
    expect(formatValue(S.ftype, 1)).toBe("BP");
    expect(formatValue(S.oscOn, 1)).toBe("On");
  });
  it("int", () => expect(formatValue(S.oct, 1)).toBe("+3"));
});

describe("discrete helpers", () => {
  it("choice index ↔ normalised", () => {
    expect(toIndex(S.ftype, 0.5)).toBe(1);
    expect(fromIndex(S.ftype, 2)).toBe(1);
    expect(fromIndex(S.slope, 1)).toBe(1);
  });
  it("int ↔ normalised", () => {
    expect(toInt(S.oct, 0.5)).toBe(0);
    expect(fromInt(S.semi, -12)).toBe(0);
    expect(fromInt(S.semi, 12)).toBe(1);
  });
  it("paramLabel", () => expect(paramLabel(S.cutoff)).toBe("Filter · Cutoff"));
});
