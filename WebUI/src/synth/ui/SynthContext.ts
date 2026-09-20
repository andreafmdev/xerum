// Contesto della finestra: lo stato condiviso del bridge (mod matrix e step
// dell'arp) più il flag "preset modificato". Una sola istanza di useBridgeState
// vive in SynthWindow e passa di qui, così non ci sono copie che si rincorrono.
// I meter NON stanno qui: cambiano 30 volte al secondo, vedi useMeterValue in juce/hooks.ts.

import { createContext, use, useCallback } from "react";
import type { ModAssignment, ModSource } from "../../juce/backend";
import type { ParamId } from "../params.generated";

export type SynthCtx = {
  mods: ModAssignment[];
  arpSteps: number[];
  addMod: (src: ModSource, target: ParamId) => void;
  setDepth: (i: number, depth: number) => void;
  removeMod: (i: number) => void;
  setArpSteps: (steps: number[]) => void;
  markDirty: () => void;
};

export const SynthContext = createContext<SynthCtx | null>(null);

export function useSynthCtx(): SynthCtx {
  const c = use(SynthContext);
  if (!c) throw new Error("SynthContext mancante");
  return c;
}

/**
 * Avvolge il setter di un controllo perché la modifica dell'utente sporchi il
 * preset. Gli echo dell'host passano dai relay e non da qui, quindi non sporcano.
 */
export function useDirty(): <A extends unknown[]>(fn: (...a: A) => void) => (...a: A) => void {
  const { markDirty } = useSynthCtx();
  return useCallback(
    <A extends unknown[]>(fn: (...a: A) => void) =>
      (...a: A) => {
        fn(...a);
        markDirty();
      },
    [markDirty],
  );
}
