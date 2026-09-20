import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Wheel } from "./Wheel";

describe("Wheel", () => {
  it("è uno slider verticale", () => {
    render(<Wheel value={0.5} onChange={() => {}} label="Pitch" />);
    const s = screen.getByRole("slider", { name: "Pitch" });
    expect(s).toHaveAttribute("aria-orientation", "vertical");
    expect(s).toHaveAttribute("aria-valuenow", "0.5");
  });

  it("il trascinamento verso l'alto aumenta il valore", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.5} onChange={onChange} label="Mod" />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerMove(s, { clientX: 0, clientY: 60, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("con springBack torna al riposo al rilascio", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.8} defaultValue={0.5} onChange={onChange} label="Pitch" springBack bipolar />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerUp(s, { clientX: 0, clientY: 100, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(0.5);
  });

  it("senza springBack resta dov'è al rilascio", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.8} onChange={onChange} label="Mod" />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerUp(s, { clientX: 0, clientY: 100, pointerId: 1 });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("bipolare: il riempimento parte dal centro", () => {
    render(<Wheel value={0.75} defaultValue={0.5} onChange={() => {}} label="Pitch" bipolar />);
    const fill = screen.getByTestId("wheel-fill");
    expect(fill.style.height).toBe("25%");
    expect(fill.style.bottom).toBe("50%");
  });

  it("unipolare: il riempimento parte dal basso", () => {
    render(<Wheel value={0.4} onChange={() => {}} label="Mod" />);
    const fill = screen.getByTestId("wheel-fill");
    expect(fill.style.height).toBe("40%");
    expect(fill.style.bottom).toBe("0%");
  });

  it("è inerte quando disabilitata", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.5} onChange={onChange} label="Mod" disabled />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("con springBack, ArrowUp da tastiera lascia il valore mosso", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.5} defaultValue={0.5} onChange={onChange} label="Pitch" springBack bipolar />);
    const s = screen.getByRole("slider");
    fireEvent.keyDown(s, { key: "ArrowUp" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.51, 5));
  });

  it("con springBack, il trascinamento seguito da pointerUp torna al riposo", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.8} defaultValue={0.5} onChange={onChange} label="Pitch" springBack bipolar />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerUp(s, { clientX: 0, clientY: 100, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(0.5);
  });

  it("con springBack, pointerCancel torna anch'esso al riposo", () => {
    const onChange = vi.fn();
    render(<Wheel value={0.8} defaultValue={0.5} onChange={onChange} label="Pitch" springBack bipolar />);
    const s = screen.getByRole("slider");
    fireEvent.pointerDown(s, { clientX: 0, clientY: 100, button: 0, pointerId: 1 });
    fireEvent.pointerCancel(s, { clientX: 0, clientY: 100, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(0.5);
  });
});

describe("Wheel motion", () => {
  // Round finale (Finding 2): il fill non anima niente. `bottom`/`height` (inline, calcolati da
  // `fill` sopra) sono l'unica cosa che cambia su questo nodo, e la whitelist delle proprieta'
  // animabili vieta di animare l'una e l'altra; `bg-(--tone)` e' statico per tutta la vita del
  // componente. Una versione precedente dichiarava `transition-[background-color] ease-settle`
  // spacciandola per il rimbalzo del ritorno a centro (springBack): non c'era nessun rimbalzo,
  // solo il nome di uno. Questo test blocca il suo ritorno silenzioso.
  it("il riempimento non dichiara nessuna transizione: non c'e' niente che questo nodo animi davvero", () => {
    render(<Wheel value={0.5} onChange={() => {}} label="Pitch" />);
    const cls = screen.getByTestId("wheel").querySelector('[data-part="fill"]')?.getAttribute("class") ?? "";
    expect(cls).not.toMatch(/transition|ease-/);
  });
});
