import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import { FakeBackend } from "./juce/fake-backend";
import { createJuceBackend, hasJuce } from "./juce/juce-backend";
import { BridgeProvider } from "./juce/provider";
import "./styles.css";

/** Dentro la WebView del plugin parla con JUCE; nel browser gira il backend finto in demo. */
async function boot() {
  // Se l'aggancio a JUCE fallisce si riparte sul backend finto: meglio una UI
  // inerte ma visibile di una finestra bianca.
  const backend = hasJuce()
    ? await createJuceBackend().catch((e) => { console.error("[bridge] boot fallito", e); return new FakeBackend(); })
    : (console.info("[bridge] nessun host JUCE: FakeBackend demo"), new FakeBackend({ demo: true }));
  createRoot(document.getElementById("root")!).render(
    <StrictMode>
      <BridgeProvider backend={backend}>
        <App />
      </BridgeProvider>
    </StrictMode>,
  );
}

void boot();
