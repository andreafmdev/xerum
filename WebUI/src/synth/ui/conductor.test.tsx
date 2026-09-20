import { useRef } from "react";
import { describe, expect, it } from "vitest";
import { render } from "@testing-library/react";
import { ZERO_METERS } from "../../juce/backend";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { useMeterConductor, writeMeterVars } from "./conductor";

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

describe("useMeterConductor", () => {
  it("scrive le custom property a ogni frame senza far ri-renderizzare il componente React", async () => {
    // `FakeBackend()` senza `demo: true` non parte con il proprio rAF: i frame arrivano solo
    // quando questo test li spinge con `emitMeters`, così il test guida lo store direttamente
    // invece di litigare con il timer del clock finto (vedi il commento su startDemo in
    // fake-backend.ts).
    const backend = new FakeBackend();
    let renders = 0;

    function Probe() {
      renders++;
      const ref = useRef<HTMLDivElement>(null);
      useMeterConductor(ref);
      return <div ref={ref} data-testid="target" />;
    }

    const { getByTestId } = render(
      <BridgeProvider backend={backend}>
        <Probe />
      </BridgeProvider>,
    );
    const el = getByTestId("target");
    const rendersAfterMount = renders;

    // Trenta frame, uno per componente selettore che ri-renderizzerebbe a 30 Hz se il
    // conductor passasse da React invece che dal DOM direttamente.
    for (let i = 0; i < 30; i++) {
      backend.emitMeters({ ...ZERO_METERS, out: i / 29, env: 0.4, lfo: 0.6 });
    }

    // Il conductor coalizza i frame in un solo rAF: aspettarne uno vero (jsdom lo implementa
    // per davvero, non è uno stub) basta perché flush() giri.
    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));

    // La prova che il conductor ha fatto qualcosa: senza questa asserzione un conduttore rotto
    // che non scrive mai nulla passerebbe la prova "nessun re-render" per pura inerzia.
    expect(el.style.getPropertyValue("--m-out")).toBe("1");
    // E la prova che serve davvero: zero commit React per trenta frame di meter.
    expect(renders).toBe(rendersAfterMount);
  });
});
