// Provider React che espone il Backend (JuceBackend o FakeBackend) via Context:
// gli hook in hooks.ts lo consumano senza sapere quale implementazione gira sotto.

import { createContext, use, type ReactNode } from "react";
import type { Backend } from "./backend";

const Ctx = createContext<Backend | null>(null);

export function BridgeProvider({ backend, children }: { backend: Backend; children: ReactNode }) {
  return <Ctx value={backend}>{children}</Ctx>;
}

export function useBackend(): Backend {
  const b = use(Ctx);
  if (!b) throw new Error("useBackend: manca <BridgeProvider>");
  return b;
}
