import { describe, expect, it } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { MetersProvider } from "./MetersContext";
import { BottomStrip } from "./BottomStrip";

function mount() {
  const backend = new FakeBackend();
  render(
    <BridgeProvider backend={backend}>
      <MetersProvider>
        <BottomStrip />
      </MetersProvider>
    </BridgeProvider>,
  );
  return backend;
}

const firstKey = () => screen.getAllByTestId("key-white")[0]!;

describe("BottomStrip", () => {
  it("parte da C2 e mostra quattro ottave", () => {
    mount();
    expect(firstKey()).toHaveAttribute("data-note", "36");
    expect(screen.getAllByTestId("key-white")).toHaveLength(28);
  });

  it("suonare un tasto arriva al backend", async () => {
    const backend = mount();
    fireEvent.pointerDown(firstKey(), { button: 0, pointerId: 1 });
    await Promise.resolve();
    expect([...backend.playing]).toEqual([36]);

    fireEvent.pointerUp(firstKey(), { pointerId: 1 });
    await Promise.resolve();
    expect(backend.playing.size).toBe(0);
  });

  it("i bottoni d'ottava spostano i tasti", () => {
    mount();
    fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
    expect(firstKey()).toHaveAttribute("data-note", "48");
    fireEvent.click(screen.getByRole("button", { name: "Octave down" }));
    expect(firstKey()).toHaveAttribute("data-note", "36");
  });

  it("l'ottava non esce dal range MIDI", () => {
    mount();
    for (let i = 0; i < 8; i++) fireEvent.click(screen.getByRole("button", { name: "Octave down" }));
    expect(Number(firstKey().getAttribute("data-note"))).toBeGreaterThanOrEqual(0);

    for (let i = 0; i < 16; i++) fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
    const highest = Math.max(...screen.getAllByTestId("key-white").map((k) => Number(k.getAttribute("data-note"))));
    expect(highest).toBeLessThanOrEqual(127);

    // Amendment 2: lo shift e' un no-op fuori range, non un clamp. firstNote deve restare
    // sempre un multiplo di 12 di distanza dal punto di partenza (36), altrimenti la tastiera
    // partirebbe da una nota che non e' un Do e i tasti neri finirebbero nel posto sbagliato.
    expect(Number(firstKey().getAttribute("data-note")) % 12).toBe(0);
  });

  it("muovere la mod wheel chiama setWheel", async () => {
    const backend = mount();
    const wheel = screen.getByRole("slider", { name: "MW" });
    fireEvent.pointerDown(wheel, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerMove(wheel, { clientX: 0, clientY: 60, pointerId: 1 });
    await Promise.resolve();
    expect(backend.wheels.mod).toBeCloseTo(0.2, 5);
  });

  it("la pitch wheel torna al centro al rilascio", async () => {
    const backend = mount();
    const wheel = screen.getByRole("slider", { name: "PB" });
    fireEvent.pointerDown(wheel, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerMove(wheel, { clientX: 0, clientY: 60, pointerId: 1 });
    await Promise.resolve();
    expect(backend.wheels.pitch).toBeGreaterThan(0.5);

    fireEvent.pointerUp(wheel, { clientX: 0, clientY: 60, pointerId: 1 });
    await Promise.resolve();
    expect(backend.wheels.pitch).toBe(0.5);
  });
});
