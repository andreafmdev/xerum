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
});
