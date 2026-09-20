import { describe, expect, it, vi } from "vitest";
import { act, fireEvent, render, screen } from "@testing-library/react";
import { ZERO_METERS } from "../../juce/backend";
import { FakeBackend } from "../../juce/fake-backend";
import { BridgeProvider } from "../../juce/provider";
import { MetersProvider } from "./MetersContext";
import { BottomStrip } from "./BottomStrip";

let unmountAll = () => {};

function mount() {
  const backend = new FakeBackend();
  const view = render(
    <BridgeProvider backend={backend}>
      <MetersProvider>
        <BottomStrip />
      </MetersProvider>
    </BridgeProvider>,
  );
  unmountAll = view.unmount;
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

  describe("suonare dalla tastiera del computer", () => {
    // La striscia nativa lo faceva con setKeyPressBaseOctave(5); qui e' un keydown sulla
    // finestra. Gli eventi si sparano su document.body: bollono fino a window, che e' dove
    // BottomStrip ascolta.
    const press = (code: string, init: KeyboardEventInit = {}) =>
      fireEvent.keyDown(document.body, { code, ...init });
    const lift = (code: string) => fireEvent.keyUp(document.body, { code });

    it("la fila bassa copre un'ottava dal primo tasto visibile", async () => {
      const backend = mount();
      press("KeyA");
      await Promise.resolve();
      expect([...backend.playing]).toEqual([36]);

      lift("KeyA");
      await Promise.resolve();
      expect(backend.playing.size).toBe(0);
    });

    it("i diesis stanno sulla fila sopra, e la seconda fila continua oltre l'ottava", async () => {
      const backend = mount();
      press("KeyW"); // DO diesis
      press("KeyK"); // il DO sopra
      press("Semicolon"); // MI sopra
      await Promise.resolve();
      expect([...backend.playing].sort((a, b) => a - b)).toEqual([37, 48, 52]);
    });

    it("manda la velocity della barra, come il puntatore", async () => {
      const backend = mount();
      const noteOn = vi.spyOn(backend, "noteOn");
      press("KeyA");
      expect(noteOn).toHaveBeenCalledWith(36, 80 / 127);
    });

    it("un tasto tenuto manda una nota sola, auto-ripetizione compresa", async () => {
      // Due forme dello stesso keydown di troppo: quello con `repeat` che manda il sistema
      // mentre il tasto resta giu', e uno senza. Nessuno dei due deve ritriggerare.
      const backend = mount();
      const noteOn = vi.spyOn(backend, "noteOn");
      press("KeyA");
      press("KeyA", { repeat: true });
      press("KeyA");
      expect(noteOn).toHaveBeenCalledTimes(1);

      lift("KeyA");
      press("KeyA");
      expect(noteOn).toHaveBeenCalledTimes(2);
    });

    it("scrivere in un campo di testo non suona", async () => {
      const backend = mount();
      const input = document.createElement("input");
      document.body.appendChild(input);
      fireEvent.keyDown(input, { code: "KeyA" });
      await Promise.resolve();
      expect(backend.playing.size).toBe(0);
      input.remove();
    });

    it("le scorciatoie con modificatori non suonano", async () => {
      const backend = mount();
      press("KeyA", { metaKey: true });
      press("KeyS", { ctrlKey: true });
      await Promise.resolve();
      expect(backend.playing.size).toBe(0);
    });

    it("il blur della finestra spegne tutto e libera i tasti tenuti", async () => {
      const backend = mount();
      press("KeyA");
      await Promise.resolve();
      expect(backend.playing.size).toBe(1);

      // Il blur non manda keyup: senza `typed.clear()` la mappa resterebbe convinta che "A" sia
      // ancora giu' e quel tasto non suonerebbe mai piu'. Il silenzio subito dopo il blur lo
      // garantirebbe gia' il panic di Keybed, quindi e' la ripressione a essere la prova.
      fireEvent(window, new Event("blur"));
      await Promise.resolve();
      expect(backend.playing.size).toBe(0);

      press("KeyA");
      await Promise.resolve();
      expect([...backend.playing]).toEqual([36]);
    });

    it("cambiare ottava con un tasto premuto non lascia la nota appesa", async () => {
      const backend = mount();
      press("KeyA");
      fireEvent.click(screen.getByRole("button", { name: "Octave up" }));
      lift("KeyA");
      await Promise.resolve();
      expect(backend.playing.size).toBe(0);
    });

    it("smontata, la striscia non ascolta piu' la tastiera", async () => {
      const backend = mount();
      const noteOn = vi.spyOn(backend, "noteOn");
      unmountAll();
      press("KeyA");
      expect(noteOn).not.toHaveBeenCalled();
    });
  });

  describe("la mod wheel disegnata e quella del motore", () => {
    const mw = () => screen.getByRole("slider", { name: "MW" });
    const frame = (value: number) => ({ ...ZERO_METERS, mw: value });

    it("parte dalla posizione che il motore ha gia'", async () => {
      // `uiModWheel_` sta nel processor e sopravvive all'editor: riaprendo la finestra con la
      // rotella alzata, il primo frame `meters` dice dov'e' davvero.
      const backend = mount();
      expect(mw()).toHaveAttribute("aria-valuenow", "0");
      act(() => backend.emitMeters(frame(0.7)));
      expect(mw()).toHaveAttribute("aria-valuenow", "0.7");
    });

    it("solo il primo frame: dopo, i frame non toccano piu' la rotella", async () => {
      // Se leggesse `mw` in continuo, ogni frame riporterebbe la rotella al valore del motore
      // mentre l'utente la sta trascinando.
      const backend = mount();
      act(() => backend.emitMeters(frame(0.7)));
      act(() => backend.emitMeters(frame(0.2)));
      expect(mw()).toHaveAttribute("aria-valuenow", "0.7");
    });

    it("al mount non spinge niente al backend", async () => {
      // Spingere `0` al mount calpesterebbe un CC 1 arrivato da una rotella hardware.
      const backend = mount();
      const setWheel = vi.spyOn(backend, "setWheel");
      act(() => backend.emitMeters(frame(0.7)));
      expect(setWheel).not.toHaveBeenCalled();
    });
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
