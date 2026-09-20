import { describe, expect, it, vi } from "vitest";
import { act, fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { PerformanceBar } from "./PerformanceBar";

function mount(props: Partial<React.ComponentProps<typeof PerformanceBar>> = {}, backend = new FakeBackend()) {
  const all = {
    firstNote: 36,
    onOctaveDown: () => {},
    onOctaveUp: () => {},
    velocity: 80,
    onVelocityChange: () => {},
    ...props,
  };
  render(
    <BridgeProvider backend={backend}>
      <PerformanceBar {...all} />
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

  describe("sorgente MIDI nello Standalone", () => {
    const standalone = () => new FakeBackend({ midiInputs: { host: false, devices: [
      { id: "id-a", name: "Keystation", enabled: true },
      { id: "id-b", name: "Launchkey", enabled: false },
    ] } });

    it("mostra un selettore con l'ingresso attivo al posto della scritta HOST", async () => {
      mount({}, standalone());
      await act(async () => {});
      expect(screen.queryByTestId("midi-source")).toBeNull();
      expect(screen.getByRole("combobox", { name: "MIDI input" })).toHaveTextContent("Keystation");
    });

    it("scegliere un ingresso abilita quello e spegne gli altri", async () => {
      const b = standalone();
      mount({}, b);
      await act(async () => {});
      await userEvent.click(screen.getByRole("combobox", { name: "MIDI input" }));
      await userEvent.click(await screen.findByRole("option", { name: "Launchkey" }));
      expect(b.midiLog).toEqual([["id-b", true], ["id-a", false]]);
    });

    it("\"Tutti gli ingressi\" li abilita tutti", async () => {
      const b = standalone();
      mount({}, b);
      await act(async () => {});
      await userEvent.click(screen.getByRole("combobox", { name: "MIDI input" }));
      await userEvent.click(await screen.findByRole("option", { name: "Tutti gli ingressi" }));
      expect(b.midiLog).toEqual([["id-a", true], ["id-b", true]]);
    });

    it("l'evento midiInputsChanged aggiorna lista e selezione", async () => {
      const b = standalone();
      mount({}, b);
      await act(async () => {});
      act(() => b.emitMidiInputsChanged({ host: false, devices: [
        { id: "id-a", name: "Keystation", enabled: false },
        { id: "id-c", name: "nanoKEY", enabled: true },
      ] }));
      expect(screen.getByRole("combobox", { name: "MIDI input" })).toHaveTextContent("nanoKEY");
    });

    it("con piu' ingressi abilitati mostra \"Tutti gli ingressi\"", async () => {
      mount({}, new FakeBackend({ midiInputs: { host: false, devices: [
        { id: "id-a", name: "Keystation", enabled: true },
        { id: "id-b", name: "Launchkey", enabled: true },
      ] } }));
      await act(async () => {});
      expect(screen.getByRole("combobox", { name: "MIDI input" })).toHaveTextContent("Tutti gli ingressi");
    });
  });
});
