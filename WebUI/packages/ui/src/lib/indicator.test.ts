import { describe, expect, it } from "vitest";
import { indicatorTransform, measureIndicator } from "./indicator";

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

describe("indicatorTransform", () => {
  it("scales relative to the element's own measured base width, not an assumed 1px", () => {
    // Regressione: prima il fattore di scala era `box.width` puro, cioè assumeva una base di
    // 1px. Un bordo di 1px per lato con box-sizing: border-box impedisce a un `width: 1px`
    // dichiarato di scendere sotto 2px di larghezza resa: la base reale era 2, non 1, e
    // l'indicatore risultava esattamente doppio. Qui la base (2) è passata esplicitamente e
    // deve dividere lo scale factor, non essere ignorata.
    expect(indicatorTransform({ x: 60, width: 24.5 }, 2)).toBe("translateX(60px) scaleX(12.25)");
  });

  it("scales 1:1 when the measured base width is already 1px", () => {
    expect(indicatorTransform({ x: 0, width: 18.6 }, 1)).toBe("translateX(0px) scaleX(18.6)");
  });

  it("falls back to a zero scale when the base width is not yet measurable", () => {
    // jsdom, o il primo render prima che il layout esista: nessuna divisione per zero.
    expect(indicatorTransform({ x: 0, width: 0 }, 0)).toBe("translateX(0px) scaleX(0)");
  });
});
