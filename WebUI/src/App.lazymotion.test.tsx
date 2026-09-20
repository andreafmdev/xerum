import type { ReactNode } from "react";
import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";

// `App.test.tsx` prova che lo chassis esiste e che `strict` fa davvero lanciare `motion.div` —
// ma nessuna delle due dice che App.tsx avvolga lo chassis in `LazyMotion`: `SynthWindow`
// renderizza il testid "chassis" con o senza quel provider attorno, e il secondo test costruisce
// il proprio `LazyMotion` locale senza mai toccare `App`. Qui si sostituisce `LazyMotion` con una
// spia che renderizza un marker DOM che porta le props ricevute, cosi' da verificare il wrapper
// vero — non solo la sua presenza nel documento, ma che lo chassis sia AL SUO INTERNO, e che
// `strict`/`features` siano esattamente quelli attesi. Lo stesso vale per `MotionConfig`, sotto:
// una seconda spia, stesso principio, per verificare che `reducedMotion="user"` sia davvero
// applicato all'albero e non solo scritto nel sorgente. Il resto del modulo (`domAnimation`,
// `m`, `AnimatePresence`, `useReducedMotion`) resta quello vero: qui interessa solo la wrapping.
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
  const MotionConfig = ({ reducedMotion, children }: { reducedMotion?: string; children: ReactNode }) => (
    <div data-testid="motionconfig-boundary" data-reduced-motion={String(reducedMotion)}>
      {children}
    </div>
  );
  return { ...actual, LazyMotion, MotionConfig };
});

const { default: App } = await import("./App");
const { FakeBackend } = await import("./juce/fake-backend");
const { BridgeProvider } = await import("./juce/provider");
// Stesso stub condiviso di @xerum/ui (vedi packages/ui/src/test/setup.ts): qui serve per
// dimostrare che il wrapper e' configurato per ascoltare la preferenza di sistema, non solo che
// esiste nel sorgente — coerente con come il resto del piano di motion prova la regola CSS.
const { setReducedMotion } = await import("../packages/ui/src/test/setup");

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

describe("App configures MotionConfig so every m.* honours reduced motion", () => {
  // matchMedia riporta "reduce" PRIMA del mount: se il wrapper mancasse, questo test non
  // avrebbe nessun elemento da leggere (getByTestId lancia) — non passerebbe per caso con
  // un'asserzione debole. Il valore di matchMedia non cambia cosa viene passato a MotionConfig
  // (reducedMotion="user" e' statico, non condizionato dalla preferenza corrente): e' la libreria
  // stessa a decidere, dentro, se applicarla o no. Il punto qui è che il wrapper è presente e
  // configurato per ascoltarla anche quando la preferenza e' gia' attiva, non solo a preferenza
  // di default.
  it("wraps the chassis in MotionConfig with reducedMotion: 'user', with the system preference already set to reduce", () => {
    setReducedMotion(true);
    mount();
    const boundary = screen.getByTestId("motionconfig-boundary");
    expect(boundary).toContainElement(screen.getByTestId("chassis"));
    expect(boundary).toHaveAttribute("data-reduced-motion", "user");
  });

  it("sits inside the LazyMotion boundary", () => {
    setReducedMotion(true);
    mount();
    expect(screen.getByTestId("lazymotion-boundary")).toContainElement(screen.getByTestId("motionconfig-boundary"));
  });
});
