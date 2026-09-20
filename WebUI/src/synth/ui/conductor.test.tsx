import { useRef } from "react";
import { afterEach, describe, expect, it, vi } from "vitest";
import { render } from "@testing-library/react";
import { ZERO_METERS } from "../../juce/backend";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { useMeterConductor, writeMeterVars } from "./conductor";

/** Componente minimo per gli hook: conta i propri render e monta il conductor sul suo ref. */
function Probe({ onRender }: { onRender?: () => void }) {
  onRender?.();
  const ref = useRef<HTMLDivElement>(null);
  useMeterConductor(ref);
  return <div ref={ref} data-testid="target" />;
}

describe("writeMeterVars", () => {
  it("writes --m-out as a unitless number", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, out: 0.7 });
    expect(el.style.getPropertyValue("--m-out")).toBe("0.7");
  });

  it("clamps into 0..1, so a rogue frame cannot blow the brightness out", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, out: 4 });
    expect(el.style.getPropertyValue("--m-out")).toBe("1");
  });

  // Round 2, finding 2: Infinity/-Infinity sono il caso che un calcolo rotto nel bridge (una
  // divisione per zero, tipicamente) produrrebbe davvero — a differenza di NaN, la conseguenza
  // sarebbe un'interfaccia accecata, non spenta, se il clamp non li intercettasse. `unit()`
  // li tratta come qualunque altro valore non finito: zero, non il tetto 1. Il test blocca
  // questo comportamento così com'è, non quello che sembrerebbe "più giusto" a occhio.
  it("tratta Infinity e -Infinity come un valore non finito, non come un valore da tagliare a 1", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, out: Infinity });
    expect(el.style.getPropertyValue("--m-out")).toBe("0");
  });

  // Round finale, finding 1: --m-env e --m-lfo non hanno mai avuto un consumatore CSS (il glow
  // dei section header e il respiro del LED dell'LFO non sono mai stati costruiti) e sono stati
  // rimossi da writeMeterVars. Questo test blocca il ritorno silenzioso di scritture morte: se
  // qualcuno le riaggiunge senza costruire prima il consumatore, il conteggio delle setProperty
  // nel test di coalescenza qui sotto lo tradisce.
  it("scrive solo --m-out, non --m-env/--m-lfo che non hanno consumatori", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, out: 0.5, env: 0.9, lfo: 0.9 });
    expect(el.style.getPropertyValue("--m-env")).toBe("");
    expect(el.style.getPropertyValue("--m-lfo")).toBe("");
  });
});

describe("useMeterConductor", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("scrive le custom property a ogni frame senza far ri-renderizzare il componente React", async () => {
    // `FakeBackend()` senza `demo: true` non parte con il proprio rAF: i frame arrivano solo
    // quando questo test li spinge con `emitMeters`, così il test guida lo store direttamente
    // invece di litigare con il timer del clock finto (vedi il commento su startDemo in
    // fake-backend.ts).
    const backend = new FakeBackend();
    let renders = 0;

    const { getByTestId } = render(
      <BridgeProvider backend={backend}>
        <Probe onRender={() => renders++} />
      </BridgeProvider>,
    );
    const el = getByTestId("target");
    const rendersAfterMount = renders;

    // Trenta frame, uno per componente selettore che ri-renderizzerebbe a 30 Hz se il
    // conductor passasse da React invece che dal DOM direttamente.
    for (let i = 0; i < 30; i++) {
      backend.emitMeters({ ...ZERO_METERS, out: i / 29 });
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

  // Round 2, finding 1: il test sopra non distingue una vera coalescenza da trenta rAF separati
  // che finiscono tutti per leggere lo stesso frame finale (i trenta `emitMeters` sono sincroni,
  // quindi store.get() è già al valore ultimo quando un rAF qualunque gira). Qui si conta
  // direttamente `requestAnimationFrame` — la chiamata che il guard `if (!raf)` deve limitare a
  // una sola per vsync — e `el.style.setProperty`, la scrittura vera e propria: un'implementazione
  // che perdesse il guard chiamerebbe l'una e l'altra trenta volte, non una.
  it("coalizza trenta frame sincroni in un solo rAF schedulato e una sola scrittura", async () => {
    const backend = new FakeBackend();
    const rafSpy = vi.spyOn(window, "requestAnimationFrame");

    const { getByTestId } = render(
      <BridgeProvider backend={backend}>
        <Probe />
      </BridgeProvider>,
    );
    const el = getByTestId("target");
    const setPropertySpy = vi.spyOn(el.style, "setProperty");

    for (let i = 0; i < 30; i++) {
      backend.emitMeters({ ...ZERO_METERS, out: i / 29 });
    }

    // Già qui, prima ancora che un frame giri: trenta `emitMeters` sincroni devono aver
    // schedulato un solo rAF, non uno per emissione.
    expect(rafSpy).toHaveBeenCalledTimes(1);

    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));

    // Una sola esecuzione di flush() scrive l'unica custom property rimasta (--m-out, dopo la
    // rimozione di --m-env/--m-lfo senza consumatori — finding 1): 1 chiamata a setProperty in
    // tutto, non 1×30.
    expect(setPropertySpy).toHaveBeenCalledTimes(1);
  });

  // Round 2, finding 3: il guard `if (el)` dentro flush() e la cancellazione nel cleanup
  // dell'effetto proteggono un nodo che React ha già smontato. Senza un test, un refactor che
  // togliesse `cancelAnimationFrame` o l'unsubscribe passerebbe inosservato finché qualcuno non
  // vedesse un frame scrivere su un elemento sparito.
  it("allo smontaggio annulla il rAF pendente e si stacca dallo store", async () => {
    const backend = new FakeBackend();
    const rafSpy = vi.spyOn(window, "requestAnimationFrame");
    const cancelSpy = vi.spyOn(window, "cancelAnimationFrame");

    const { getByTestId, unmount } = render(
      <BridgeProvider backend={backend}>
        <Probe />
      </BridgeProvider>,
    );
    const el = getByTestId("target");
    const setPropertySpy = vi.spyOn(el.style, "setProperty");

    // Un frame in sospeso: senza questo, cancelAnimationFrame non avrebbe nulla da annullare e
    // il test passerebbe anche se il cleanup non chiamasse mai cancelAnimationFrame.
    backend.emitMeters({ ...ZERO_METERS, out: 0.9 });
    expect(rafSpy).toHaveBeenCalledTimes(1);
    expect(backend.meterListeners()).toBe(1);

    unmount();

    expect(cancelSpy).toHaveBeenCalledTimes(1);
    expect(backend.meterListeners()).toBe(0);

    // Anche aspettando un frame vero dopo lo smontaggio, la scrittura non deve avvenire: il rAF
    // pendente è stato annullato, quindi flush() non gira affatto sul nodo smontato.
    await new Promise<void>((resolve) => requestAnimationFrame(() => resolve()));
    expect(setPropertySpy).not.toHaveBeenCalled();
  });
});
