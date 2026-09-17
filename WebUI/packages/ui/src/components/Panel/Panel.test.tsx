import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { Panel } from "./Panel";

describe("Panel", () => {
  it("renders title and children", () => {
    render(<Panel title="Oscillator"><span>body</span></Panel>);
    expect(screen.getByText("Oscillator")).toBeInTheDocument();
    expect(screen.getByText("body")).toBeInTheDocument();
  });

  it("sets --tone on the root so children inherit it", () => {
    render(<Panel title="Filter" tone="filter">x</Panel>);
    expect(screen.getByTestId("panel").style.getPropertyValue("--tone")).toBe("var(--color-filter)");
  });

  it("leaves --tone unset without a tone prop", () => {
    render(<Panel>x</Panel>);
    expect(screen.getByTestId("panel").style.getPropertyValue("--tone")).toBe("");
  });

  it("renders actions in the header", () => {
    render(<Panel title="Env" actions={<button>reset</button>}>x</Panel>);
    expect(screen.getByRole("button", { name: "reset" })).toBeInTheDocument();
  });

  it("allows children to overflow the panel bounds", () => {
    render(<Panel>x</Panel>);
    expect(screen.getByTestId("panel")).toHaveClass("overflow-visible");
    expect(screen.getByTestId("panel")).not.toHaveClass("overflow-hidden");
  });
});
