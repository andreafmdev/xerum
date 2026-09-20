import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { MetersProvider } from "./MetersContext";
import { PerformanceBar } from "./PerformanceBar";

function mount(props: Partial<React.ComponentProps<typeof PerformanceBar>> = {}) {
  const all = {
    firstNote: 36,
    onOctaveDown: () => {},
    onOctaveUp: () => {},
    velocity: 80,
    onVelocityChange: () => {},
    ...props,
  };
  render(
    <BridgeProvider backend={new FakeBackend()}>
      <MetersProvider>
        <PerformanceBar {...all} />
      </MetersProvider>
    </BridgeProvider>,
  );
}

describe("PerformanceBar", () => {
  it("mostra la nota piu' bassa visibile", () => {
    mount({ firstNote: 36 });
    expect(screen.getByTestId("octave-readout")).toHaveTextContent("C2");
  });

  it("nomina bene anche le ottave negative", () => {
    mount({ firstNote: 0 });
    expect(screen.getByTestId("octave-readout")).toHaveTextContent("C-1");
  });

  it("i due bottoni d'ottava chiamano i loro callback", () => {
    const onOctaveDown = vi.fn();
    const onOctaveUp = vi.fn();
    mount({ onOctaveDown, onOctaveUp });
    fireEvent.click(screen.getByRole("button", { name: "Octave down" }));
    fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
    expect(onOctaveDown).toHaveBeenCalledTimes(1);
    expect(onOctaveUp).toHaveBeenCalledTimes(1);
  });

  it("la velocity si legge e si cambia", () => {
    const onVelocityChange = vi.fn();
    mount({ velocity: 80, onVelocityChange });
    const spin = screen.getByRole("spinbutton", { name: "Velocity" });
    expect(spin).toHaveAttribute("aria-valuenow", "80");
    fireEvent.keyDown(spin, { key: "ArrowUp" });
    expect(onVelocityChange).toHaveBeenCalledWith(81);
  });

  it("mostra la sorgente MIDI e una spia spenta senza note", () => {
    mount();
    expect(screen.getByTestId("midi-source")).toHaveTextContent("HOST");
    expect(screen.getByTestId("midi-activity")).toHaveAttribute("data-on", "false");
  });

  it("tiene i due meter ereditati dal footer", () => {
    mount();
    expect(screen.getByRole("meter", { name: "Input" })).toBeInTheDocument();
    expect(screen.getByRole("meter", { name: "Output" })).toBeInTheDocument();
  });

  it("non mostra piu' VOICES ne' CPU", () => {
    // Erano numeri finti — lo diceva il commento del vecchio Footer — e non tornano finche'
    // l'host non li espone davvero.
    mount();
    expect(screen.queryByText(/VOICES/)).toBeNull();
    expect(screen.queryByText(/CPU/)).toBeNull();
  });
});
