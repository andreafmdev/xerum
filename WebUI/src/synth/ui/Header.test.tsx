import { describe, expect, it } from "vitest";
import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Header } from "./Header";
import { PRESETS } from "../presets";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { SynthContext, type SynthCtx } from "./SynthContext";

type Extra = { chrome?: { trafficLightWidth: number }; scale?: number };

// Header usa useDirty() per il bottone Bypass, che richiede <SynthContext>: il brief non lo
// mette nell'helper di test (ne' PerformanceBar.test.tsx, il modello, ne ha bisogno perché
// PerformanceBar non tocca il preset), ma senza di lui il render lancia "SynthContext mancante".
const noopCtx: SynthCtx = { mods: [], arpSteps: [], addMod: () => {}, setDepth: () => {}, removeMod: () => {}, setArpSteps: () => {}, markDirty: () => {} };

const headerWith = (backend: FakeBackend, extra: Extra = {}) => (
  <BridgeProvider backend={backend}>
    <SynthContext value={noopCtx}>
      <Header
        preset={PRESETS[0]!}
        dirty={false}
        onPrev={() => {}}
        onNext={() => {}}
        onBrowse={() => {}}
        onSettings={() => {}}
        scale={extra.scale ?? 1}
        trafficLightWidth={extra.chrome?.trafficLightWidth ?? 0}
      />
    </SynthContext>
  </BridgeProvider>
);

const renderHeader = (backend: FakeBackend, extra: Extra = {}) => render(headerWith(backend, extra));

describe("Header", () => {
  it("il mousedown sull'header trascina la finestra", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.pointer({ target: screen.getByRole("banner"), keys: "[MouseLeft>]" });
    expect(backend.windowDrags).toBe(1);
  });

  it("il mousedown su un controllo NON trascina la finestra", async () => {
    // Senza questa esclusione la finestra si sposterebbe mentre giri una manopola: l'errore
    // che renderebbe la UI inusabile.
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.pointer({ target: screen.getByLabelText("Settings"), keys: "[MouseLeft>]" });
    expect(backend.windowDrags).toBe(0);
  });

  it("lo spazio per il semaforo c'è solo in Standalone ed è diviso per lo scale", () => {
    // Il semaforo lo disegna macOS in coordinate di finestra; l'header vive dentro lo chassis
    // scalato. A scala 1.5 un padding di 78 px CSS ne occuperebbe 117 sulla finestra.
    const { rerender } = renderHeader(new FakeBackend(), { chrome: { trafficLightWidth: 78 }, scale: 1.5 });
    expect(screen.getByRole("banner")).toHaveStyle({ paddingLeft: "52px" });
    rerender(headerWith(new FakeBackend(), { chrome: { trafficLightWidth: 0 }, scale: 1.5 }));
    expect(screen.getByRole("banner")).toHaveStyle({ paddingLeft: "0px" });
  });

  it("se il trascinamento nativo non parte, il mousemove che segue chiama moveWindowBy", async () => {
    // Il ripiego non è codice morto: è la strada presa su ogni versione di macOS dove
    // l'evento del mousedown non è più valido quando il messaggio della bridge arriva.
    const backend = new FakeBackend();
    backend.nextDragStarts = false;
    renderHeader(backend);
    const header = screen.getByRole("banner");
    await userEvent.pointer([
      { target: header, coords: { clientX: 100, clientY: 100 }, keys: "[MouseLeft>]" },
      { target: header, coords: { clientX: 130, clientY: 84 } },
    ]);
    await waitFor(() => expect(backend.moves.length).toBeGreaterThan(0));
    expect(backend.moves[0]).toEqual([30, -16]);
  });

  it("se il trascinamento nativo parte, il mousemove che segue NON chiama moveWindowBy", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    const header = screen.getByRole("banner");
    await userEvent.pointer([
      { target: header, coords: { clientX: 100, clientY: 100 }, keys: "[MouseLeft>]" },
      { target: header, coords: { clientX: 130, clientY: 84 } },
    ]);
    expect(backend.moves).toHaveLength(0);
  });

  it("il doppio clic sull'header attiva lo zoom nativo", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.dblClick(screen.getByRole("banner"));
    expect(backend.zoomToggles).toBe(1);
  });

  it("il doppio clic su un controllo NON attiva lo zoom nativo", async () => {
    const backend = new FakeBackend();
    renderHeader(backend);
    await userEvent.dblClick(screen.getByLabelText("Settings"));
    expect(backend.zoomToggles).toBe(0);
  });
});
