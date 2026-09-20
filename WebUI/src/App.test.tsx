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

describe("App", () => {
  it("renders the chassis inside the motion provider", () => {
    mount();
    expect(screen.getByTestId("chassis")).toBeInTheDocument();
  });

  it("refuses the full motion namespace: LazyMotion runs in strict mode", async () => {
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
