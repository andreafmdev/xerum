import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { Meter, litSegments } from "./Meter";

describe("litSegments", () => {
  it("rounds level to the number of lit segments", () => {
    expect(litSegments(0, 24)).toBe(0);
    expect(litSegments(0.5, 24)).toBe(12);
    expect(litSegments(1, 24)).toBe(24);
    expect(litSegments(1.7, 24)).toBe(24);
    expect(litSegments(-1, 24)).toBe(0);
  });
});

describe("Meter", () => {
  it("is a meter element with a normalised value", () => {
    render(<Meter level={0.5} label="Output" />);
    const meter = screen.getByRole("meter", { name: "Output" });
    expect(meter).toHaveAttribute("aria-valuemin", "0");
    expect(meter).toHaveAttribute("aria-valuemax", "1");
    expect(meter).toHaveAttribute("aria-valuenow", "0.5");
  });

  it("lights the first segments up to the level", () => {
    render(<Meter level={0.5} label="Output" segments={8} />);
    const segs = screen.getAllByTestId("meter-segment");
    expect(segs).toHaveLength(8);
    expect(segs.map((s) => s.getAttribute("data-lit"))).toEqual(["true", "true", "true", "true", "false", "false", "false", "false"]);
  });

  it("marks the top of the scale as warning and clip zones", () => {
    render(<Meter level={1} label="Output" segments={10} />);
    const zones = screen.getAllByTestId("meter-segment").map((s) => s.getAttribute("data-zone"));
    expect(zones).toEqual(["ok", "ok", "ok", "ok", "ok", "ok", "ok", "ok", "warn", "clip"]);
  });

  it("sets --tone", () => {
    render(<Meter level={0.2} label="In" tone="osc" />);
    expect(screen.getByRole("meter").style.getPropertyValue("--tone")).toBe("var(--color-osc)");
  });
});
