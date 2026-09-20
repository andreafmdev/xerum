import type { ReactNode } from "react";
import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";

// `App.test.tsx` prova che lo chassis esiste e che `strict` fa davvero lanciare `motion.div` —
// ma nessuna delle due dice che App.tsx avvolga lo chassis in `LazyMotion`: `SynthWindow`
// renderizza il testid "chassis" con o senza quel provider attorno, e il secondo test costruisce
// il proprio `LazyMotion` locale senza mai toccare `App`. Qui si sostituisce `LazyMotion` con una
// spia che renderizza un marker DOM che porta le props ricevute, cosi' da verificare il wrapper
// vero — non solo la sua presenza nel documento, ma che lo chassis sia AL SUO INTERNO, e che
// `strict`/`features` siano esattamente quelli attesi. Il resto del modulo (`domAnimation`, `m`,
// `AnimatePresence`, `useReducedMotion`) resta quello vero: qui interessa solo la wrapping.
vi.mock("motion/react", async (importOriginal) => {
  const actual = await importOriginal<typeof import("motion/react")>();
  const LazyMotion = ({
    strict,
    features,
    children,
  }: {
    strict?: boolean;
    features: unknown;
    children: ReactNode;
  }) => (
    <div
      data-testid="lazymotion-boundary"
      data-strict={String(strict === true)}
      data-features={features === actual.domAnimation ? "domAnimation" : "other"}
    >
      {children}
    </div>
  );
  return { ...actual, LazyMotion };
});

const { default: App } = await import("./App");
const { FakeBackend } = await import("./juce/fake-backend");
const { BridgeProvider } = await import("./juce/provider");

function mount() {
  render(
    <BridgeProvider backend={new FakeBackend()}>
      <App />
    </BridgeProvider>,
  );
}

describe("App wraps the chassis in LazyMotion", () => {
  it("mounts the boundary with the chassis inside it, not merely present somewhere in the document", () => {
    mount();
    const boundary = screen.getByTestId("lazymotion-boundary");
    expect(boundary).toContainElement(screen.getByTestId("chassis"));
  });

  it("passes strict: true — the guarantee that forces m.* and keeps domAnimation the only feature set", () => {
    mount();
    expect(screen.getByTestId("lazymotion-boundary")).toHaveAttribute("data-strict", "true");
  });

  it("passes the domAnimation feature set, not another one", () => {
    mount();
    expect(screen.getByTestId("lazymotion-boundary")).toHaveAttribute("data-features", "domAnimation");
  });
});
