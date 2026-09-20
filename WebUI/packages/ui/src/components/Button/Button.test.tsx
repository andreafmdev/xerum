import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Button } from "./Button";

describe("Button", () => {
  it("renders a button and fires onClick", async () => {
    const onClick = vi.fn();
    render(<Button onClick={onClick}>Init</Button>);
    await userEvent.click(screen.getByRole("button", { name: "Init" }));
    expect(onClick).toHaveBeenCalledTimes(1);
  });

  it("applies the tone as --tone and uses it for the default variant", () => {
    render(<Button tone="env">Env</Button>);
    const b = screen.getByRole("button");
    expect(b.style.getPropertyValue("--tone")).toBe("var(--color-env)");
    expect(b.className).toContain("bg-(--tone)");
  });

  it("does not recolor non-default variants", () => {
    render(<Button tone="env" variant="ghost">Env</Button>);
    expect(screen.getByRole("button").className).not.toContain("bg-(--tone)");
  });

  it("supports disabled", () => {
    render(<Button disabled>Off</Button>);
    expect(screen.getByRole("button")).toBeDisabled();
  });
});

describe("Button motion", () => {
  it("transitions a named, complete list of properties, never all of them", () => {
    render(<Button>Load</Button>);
    const el = screen.getByRole("button", { name: "Load" });
    expect(el).not.toHaveClass("transition-all");
    // Assertiamo la stringa esatta, non un sottoinsieme: ghost/outline animano il testo
    // (hover:text-foreground, aria-expanded:text-foreground) e disabled anima l'opacità,
    // quindi color e opacity devono comparire quanto transform. Un toContain su un
    // singolo nome non avrebbe intercettato l'omissione di color/opacity la prima volta;
    // la lista esatta obbliga ogni futura modifica a essere deliberata.
    const match = el.className.match(/transition-\[([^\]]*)\]/);
    expect(match?.[1]).toBe("transform,box-shadow,background-color,border-color,color,opacity");
  });

  it("presses instantly and releases on the press duration", () => {
    render(<Button>Load</Button>);
    const el = screen.getByRole("button", { name: "Load" });
    // Il dito è più veloce della molla: la discesa non ha durata, la risalita sì.
    expect(el.className).toContain("duration-(--dur-press)");
    expect(el.className).toContain("active:duration-0");
    expect(el.className).toContain("ease-snap");
  });

  it("gives the focus ring its own entrance on the state duration", () => {
    render(<Button>Load</Button>);
    const el = screen.getByRole("button", { name: "Load" });
    expect(el.className).toContain("focus-visible:duration-(--dur-state)");
  });
});
