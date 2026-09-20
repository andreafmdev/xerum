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

  // Finding 3 (revisione di branch): il crossfade dei Tabs usa mode="wait", quindi la durata
  // composta e' la somma di entrata e uscita, non la sola entrata. T.stateIn/stateOut derivano
  // dallo stesso DUR.state (120ms) e dalla stessa asimmetria di T.layerIn/layerOut, non da un
  // valore nuovo scritto a mano.
  it("derives the state exit from the enter with the same asymmetry ratio", () => {
    expect(T.stateOut.duration).toBeCloseTo(T.stateIn.duration * EXIT_RATIO, 5);
  });

  it("keeps the composed tab switch (mode=\"wait\": enter + exit in sequence) close to the indicator's own --dur-state instead of tripling it", () => {
    const composedState = T.stateIn.duration + T.stateOut.duration;
    const composedLayer = T.layerIn.duration + T.layerOut.duration;
    // 320ms (layer + layer*0.6) era quasi il triplo dei 120ms dell'indicatore sotto; 192ms
    // (state + state*0.6) ci resta vicino.
    expect(composedState).toBeCloseTo((DUR.state + DUR.state * EXIT_RATIO) / 1000, 5);
    expect(composedState).toBeLessThan(composedLayer);
  });
});
