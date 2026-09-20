import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Tabs } from "./Tabs";

const items = [
  { value: "osc", label: "OSC" },
  { value: "filter", label: "FILTER" },
  { value: "env", label: "ENV" },
];

describe("Tabs", () => {
  it("renders a tablist with the active tab selected", () => {
    render(<Tabs value="filter" onChange={() => {}} items={items} />);
    expect(screen.getByRole("tablist")).toBeInTheDocument();
    expect(screen.getByRole("tab", { name: "FILTER" })).toHaveAttribute("aria-selected", "true");
    expect(screen.getByRole("tab", { name: "OSC" })).toHaveAttribute("aria-selected", "false");
  });

  it("calls onChange on click", async () => {
    const onChange = vi.fn();
    render(<Tabs value="osc" onChange={onChange} items={items} />);
    await userEvent.click(screen.getByRole("tab", { name: "ENV" }));
    expect(onChange).toHaveBeenCalledWith("env");
  });

  it("moves with arrow keys", async () => {
    const onChange = vi.fn();
    render(<Tabs value="osc" onChange={onChange} items={items} />);
    screen.getByRole("tab", { name: "OSC" }).focus();
    await userEvent.keyboard("{ArrowRight}");
    expect(onChange).toHaveBeenCalledWith("filter");
  });

  it("sets --tone", () => {
    render(<Tabs value="osc" onChange={() => {}} items={items} tone="osc" />);
    expect(screen.getByTestId("tabs").style.getPropertyValue("--tone")).toBe("var(--color-osc)");
  });
});

describe("Tabs per-item tone", () => {
  it("sets --tone on a tab from its item tone", () => {
    render(
      <Tabs
        value="env"
        onChange={() => {}}
        items={[
          { value: "env", label: "ENV", tone: "env" },
          { value: "lfo", label: "LFO", tone: "lfo" },
        ]}
      />,
    );
    expect(screen.getByRole("tab", { name: "ENV" }).style.getPropertyValue("--tone")).toBe("var(--color-env)");
    expect(screen.getByRole("tab", { name: "LFO" }).style.getPropertyValue("--tone")).toBe("var(--color-lfo)");
  });
});

describe("Tabs bar variant", () => {
  it("exposes the variant on the root and stretches the list", () => {
    render(<Tabs value="osc" onChange={() => {}} items={items} variant="bar" />);
    expect(screen.getByTestId("tabs")).toHaveAttribute("data-variant", "bar");
    expect(screen.getByRole("tablist")).toHaveClass("w-full");
  });
});

describe("Tabs motion", () => {
  const items = [
    { value: "env", label: "ENV" },
    { value: "lfo", label: "LFO" },
  ];

  it("slides a single underline in the bar variant", () => {
    render(<Tabs variant="bar" value="env" onChange={() => {}} items={items} />);
    const ind = screen.getByTestId("tabs-indicator");
    // Fix round 1 / Finding 2: la larghezza deve scorrere insieme alla posizione (scaleX +
    // translateX via transform, misurati in JS come in Segmented), non scattare all'istante da
    // una lunghezza CSS di Base UI — vedi lib/indicator.ts. Un solo `transform` transizionato,
    // mai `translate`/`scale` separati né, tantomeno, `width`.
    expect(ind.className).toContain("transition-transform");
    expect(ind.className).toContain("duration-(--dur-state)");
    expect(ind.className).toContain("ease-glass");
  });

  it("has no indicator in the plate variant, where the active plate is pressed in", () => {
    render(<Tabs variant="plate" value="env" onChange={() => {}} items={items} />);
    expect(screen.queryByTestId("tabs-indicator")).toBeNull();
  });

  it("hides the indicator until there is a layout to measure", () => {
    render(<Tabs variant="bar" value="env" onChange={() => {}} items={items} />);
    // jsdom non fa layout: larghezza 0, quindi l'indicatore resta nascosto invece di scattare
    // a un box 0×0 in un angolo (stesso pattern di Segmented).
    expect(screen.getByTestId("tabs-indicator")).toHaveAttribute("data-measured", "false");
  });
});
