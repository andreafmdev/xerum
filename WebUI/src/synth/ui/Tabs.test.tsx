import { describe, expect, it } from "vitest";
import { render, screen, within } from "@testing-library/react";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { SynthWindow } from "./SynthWindow";

HTMLCanvasElement.prototype.getContext = (() => null) as unknown as HTMLCanvasElement["getContext"];

const mountFx = () =>
  render(
    <BridgeProvider backend={new FakeBackend()}>
      <SynthWindow initialTab="fx" />
    </BridgeProvider>,
  );

// jsdom non fa layout, quindi qui non si misura niente: si verificano le due condizioni da cui
// dipende il conto fatto nel browser (plate da 144 px, readout su una riga sola). I numeri veri
// stanno nei commenti di SynthWindow.tsx e Tabs.tsx.
describe("FxTab", () => {
  it("keeps every readout: they are exactly what the plate was cutting off", () => {
    mountFx();
    expect(within(screen.getByTestId("fx-slot-Chorus")).getAllByTestId("knob-readout")).toHaveLength(4);
    expect(within(screen.getByTestId("fx-slot-Delay")).getAllByTestId("knob-readout")).toHaveLength(4);
    expect(within(screen.getByTestId("fx-slot-Reverb")).getAllByTestId("knob-readout")).toHaveLength(5);
  });

  it("holds every fx readout on one line: a wrapped unit adds 16px and the slot stops fitting", () => {
    mountFx();
    const knobs = [...screen.getByTestId("fx-slot-Chorus").querySelectorAll('[data-testid="knob"]')];
    expect(knobs).toHaveLength(4);
    for (const k of knobs) expect(k.className).toContain("[&_[data-part=readout]]:whitespace-nowrap");
  });

  it("widens only the two knobs whose unit does not fit in 38px", () => {
    mountFx();
    // "1.60 Hz" misura 46 px e "376 ms" 40: senza min-width vanno a capo. Gli altri undici
    // stanno sotto i 33 e allargarli si prenderebbe 110 px di riga che la tab non ha.
    const wide = (slot: string) =>
      [...screen.getByTestId(slot).querySelectorAll('[data-testid="knob"]')].filter((k) => k.className.includes("min-w-12"));
    expect(wide("fx-slot-Chorus")).toHaveLength(1);
    expect(wide("fx-slot-Delay")).toHaveLength(1);
    expect(wide("fx-slot-Reverb")).toHaveLength(0);
  });

  it("keeps the delay's sync and ping-pong switches in the slot head", () => {
    mountFx();
    const head = within(screen.getByTestId("fx-slot-Delay")).getByTestId("fx-slot-head");
    expect(within(head).getByRole("switch", { name: "Sync" })).toBeInTheDocument();
    expect(within(head).getByRole("switch", { name: "Ping" })).toBeInTheDocument();
  });
});
