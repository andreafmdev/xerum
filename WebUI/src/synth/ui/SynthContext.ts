// Contesto della finestra: quello che i knob condividono ma non arriva da un
// singolo parametro (mod matrix, livelli delle sorgenti, "preset modificato").
// Evita di far passare un oggetto `ctx` di pannello in pannello.

import { createContext, useContext } from "react";
import type { ModAssignment, ModSource } from "../../juce/backend";
import type { ParamId } from "../params.generated";
import type { SourceLevels } from "../mod";

export type SynthCtx = {
  mods: ModAssignment[];
  addMod: (src: ModSource, target: ParamId) => void;
  sources: SourceLevels;
  markDirty: () => void;
};

export const SynthContext = createContext<SynthCtx | null>(null);

export function useSynthCtx(): SynthCtx {
  const c = useContext(SynthContext);
  if (!c) throw new Error("SynthContext mancante");
  return c;
}
