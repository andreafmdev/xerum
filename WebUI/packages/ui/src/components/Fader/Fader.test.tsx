import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Fader } from "./Fader";

describe("Fader", () => {
  it("is a vertical slider by default", () => {
    render(<Fader value={0.5} onChange={() => {}} label="Level" />);
    const s = screen.getByRole("slider", { name: "Level" });
    expect(s).toHaveAttribute("aria-orientation", "vertical");
    expect(s).toHaveAttribute("aria-valuenow", "0.5");
  });

  it("supports horizontal orientation and x-axis drag", () => {
    const onChange = vi.fn();
    render(<Fader value={0.5} onChange={onChange} label="Level" orientation="horizontal" />);
    const s = screen.getByRole("slider");
    expect(s).toHaveAttribute("aria-orientation", "horizontal");
    fireEvent.pointerDown(s, { clientX: 100, clientY: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(s, { clientX: 140, clientY: 0, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("resets on double click", () => {
    const onChange = vi.fn();
    render(<Fader value={0.8} defaultValue={0.5} onChange={onChange} label="Level" />);
    fireEvent.doubleClick(screen.getByRole("slider"));
    expect(onChange).toHaveBeenCalledWith(0.5);
  });

  it("sizes the fill from the value", () => {
    render(<Fader value={0.25} onChange={() => {}} label="Level" />);
    expect(screen.getByTestId("fader-fill").style.height).toBe("25%");
  });

  it("shows the formatted readout", () => {
    render(<Fader value={0.5} onChange={() => {}} label="Out" format={(v) => `${(v * 12 - 6).toFixed(1)} dB`} />);
    expect(screen.getByTestId("fader-readout")).toHaveTextContent("0.0 dB");
  });

  it("shows the label beside the value at rest", () => {
    render(<Fader value={0.25} onChange={() => {}} label="Attack" />);
    expect(screen.getByText("Attack")).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Attack" })).toBeInTheDocument();
    expect(screen.getByTestId("fader-readout")).toHaveTextContent("25%");
  });

  it("is inert when disabled", () => {
    const onChange = vi.fn();
    render(<Fader value={0.5} onChange={onChange} label="Level" disabled />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("marks the root as dragging while the pointer is down", () => {
    render(<Fader value={0.5} onChange={() => {}} label="Level" />);
    const slider = screen.getByRole("slider");
    const root = slider.parentElement as HTMLElement;
    fireEvent.pointerDown(slider, { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(root).toHaveAttribute("data-dragging", "true");
    fireEvent.pointerUp(slider, { clientY: 0, clientX: 0, pointerId: 1 });
    expect(root).toHaveAttribute("data-dragging", "false");
  });
});

describe("Fader motion", () => {
  // Round finale (Finding 2): il fill non anima niente. `height`/`width` (inline, da `pct`)
  // sono l'unica cosa che cambia su questo nodo, e la whitelist delle proprieta' animabili
  // vieta di animare l'una e l'altra; `bg-(--tone)` e' statico per tutta la vita del
  // componente. Una versione precedente dichiarava `transition-[background-color] ease-glass`
  // che non aveva mai niente da animare — CSS morto sotto un commento che ne spiegava la
  // fisica. Questo test blocca il suo ritorno silenzioso.
  it("il riempimento non dichiara nessuna transizione: non c'e' niente che questo nodo animi davvero", () => {
    render(<Fader value={0.3} onChange={() => {}} label="Level" />);
    const fill = screen.getByTestId("fader-fill");
    expect(fill.getAttribute("class")).not.toMatch(/transition|ease-/);
  });

  it("never disables the thumb's own press feedback while settling", () => {
    render(<Fader value={0.3} onChange={() => {}} label="Level" />);
    const thumb = screen.getByTestId("fader-thumb");
    expect(thumb.getAttribute("class")).toContain("ease-snap");
    expect(thumb.getAttribute("class")).not.toContain("ease-glass");
  });
});
