// I meter arrivano a 30 Hz: se il loro valore stesse nel contesto della finestra
// ogni frame ridisegnerebbe tutti i pannelli. Qui vivono in un contesto a parte,
// e il provider riceve i figli come prop: rirenderizzandosi non li tocca, così
// si aggiornano solo i componenti che leggono davvero questo contesto.

import { createContext, useContext, type ReactNode } from "react";
import type { MeterFrame } from "../../juce/backend";
import { useMeters } from "../../juce/hooks";
import type { SourceLevels } from "../mod";

const MetersContext = createContext<MeterFrame | null>(null);

export function MetersProvider({ children }: { children: ReactNode }) {
  const meters = useMeters();
  return <MetersContext.Provider value={meters}>{children}</MetersContext.Provider>;
}

export function useMeterFrame(): MeterFrame {
  const m = useContext(MetersContext);
  if (!m) throw new Error("MetersContext mancante");
  return m;
}

/**
 * I livelli delle cinque sorgenti, presi dal frame che arriva dall'host.
 *
 * Qui c'erano quattro costanti — env 0.6, env2 0.5, vel 0.7, mw 0.5 — perché MeterFrame portava
 * il solo LFO. L'anello attorno a un knob modulato nasceva quindi con la profondità giusta e non
 * si muoveva mai: su una route env → cutoff restava fermo mentre il filtro si apriva. Adesso i
 * cinque livelli li pubblica il motore (Source/engine/SynthEngine.h).
 */
export function useSourceLevels(): SourceLevels {
  const { lfo, env, env2, vel, mw } = useMeterFrame();
  return { lfo, env, env2, vel, mw };
}
