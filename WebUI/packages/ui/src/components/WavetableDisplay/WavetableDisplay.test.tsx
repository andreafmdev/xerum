import { beforeEach, describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { frameIndex, WavetableDisplay } from "./WavetableDisplay";

const sine = (n = 64) => Float32Array.from({ length: n }, (_, i) => Math.sin((i / n) * Math.PI * 2));

describe("frameIndex", () => {
  it("maps 0..1 to the nearest frame", () => {
    expect(frameIndex(0, 8)).toBe(0);
    expect(frameIndex(1, 8)).toBe(7);
    expect(frameIndex(0.5, 8)).toBe(4);
    expect(frameIndex(0.5, 0)).toBe(0);
  });
});

describe("WavetableDisplay", () => {
  const ctx = {
    clearRect: vi.fn(), beginPath: vi.fn(), moveTo: vi.fn(), lineTo: vi.fn(), stroke: vi.fn(),
    setTransform: vi.fn(), scale: vi.fn(), save: vi.fn(), restore: vi.fn(),
    lineWidth: 0, strokeStyle: "", globalAlpha: 1,
  };

  beforeEach(() => {
    vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(ctx as unknown as CanvasRenderingContext2D);
    ctx.stroke.mockClear();
    ctx.moveTo.mockClear();
  });

  it("renders a canvas with an accessible label", () => {
    render(<WavetableDisplay frames={[sine()]} position={0} />);
    expect(screen.getByRole("img", { name: "Wavetable" })).toBeInstanceOf(HTMLCanvasElement);
  });

  it("draws a flat line with no frames", () => {
    render(<WavetableDisplay frames={[]} position={0} />);
    expect(ctx.stroke).toHaveBeenCalled();
  });

  it("draws the current frame plus neighbours", () => {
    render(<WavetableDisplay frames={[sine(), sine(), sine(), sine(), sine()]} position={0.5} />);
    // frame corrente + 2 vicini per lato = 5 stroke
    expect(ctx.stroke).toHaveBeenCalledTimes(5);
  });

  it("sets --tone", () => {
    render(<WavetableDisplay frames={[sine()]} position={0} tone="osc" />);
    expect(screen.getByTestId("wavetable").style.getPropertyValue("--tone")).toBe("var(--color-osc)");
  });

  it("draws neighbours symmetrically (same depth = same horizontal inset)", () => {
    render(<WavetableDisplay frames={[sine(), sine(), sine(), sine(), sine()]} position={0.5} />);
    // Ordine di disegno: fwd(d=2), back(d=2), fwd(d=1), back(d=1), corrente(d=0).
    // Ogni frame inizia con moveTo(x0, ...) dove x0 = inset + depth * 6 (il campione i=0 di sine() è 0).
    const x0 = (call: unknown[]) => call[0];
    const calls = ctx.moveTo.mock.calls;
    expect(calls).toHaveLength(5);
    expect(x0(calls[0])).toBe(x0(calls[1])); // fwd d=2 vs back d=2: stesso inset
    expect(x0(calls[2])).toBe(x0(calls[3])); // fwd d=1 vs back d=1: stesso inset
    expect(x0(calls[0])).not.toBe(x0(calls[2])); // profondità diverse restano distinguibili
  });
});
