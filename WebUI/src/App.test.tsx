import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import App from "./App";
import { FakeBackend } from "./juce/fake-backend";
import { BridgeProvider } from "./juce/provider";

// `App.tsx` non contiene il bridge: e' `main.tsx` a montare `<BridgeProvider>` attorno ad `<App/>`.
// Senza, `useBackend` (usato in profondita' da `SynthWindow`) lancia "manca <BridgeProvider>".
function mount() {
  render(
    <BridgeProvider backend={new FakeBackend()}>
      <App />
    </BridgeProvider>,
  );
}

// Questi due test non provano che App.tsx avvolga lo chassis in LazyMotion (ne' che passi
// strict/domAnimation): quella verifica vive in App.lazymotion.test.tsx, con LazyMotion
// mockato per ispezionare le props ricevute. Qui si prova solo, rispettivamente, che l'app
// renderizzi lo chassis e che la libreria `motion` mantenga davvero la garanzia "strict" su
// cui l'intero wrapping fa affidamento.
describe("App", () => {
  it("renders the chassis", () => {
    mount();
    expect(screen.getByTestId("chassis")).toBeInTheDocument();
  });

  it("sanity check: LazyMotion in strict mode really throws on the full motion namespace", async () => {
    const { LazyMotion, domAnimation, motion } = await import("motion/react");
    // strict-mode-throws-test:start
    // `motion.div` qui e' l'oggetto del test, non una regressione: verifica che `strict`
    // faccia davvero fallire il namespace pieno, cosi' che altrove nel bundle resti vietato.
    expect(() =>
      render(
        <LazyMotion features={domAnimation} strict>
          <motion.div />
        </LazyMotion>,
      ),
    ).toThrow();
    // strict-mode-throws-test:end
  });
});
