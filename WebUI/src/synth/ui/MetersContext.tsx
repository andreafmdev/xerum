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

/** Livelli istantanei delle sorgenti: l'host manda solo l'LFO, le altre restano valori di comodo. */
export function useSourceLevels(): SourceLevels {
  const { lfo } = useMeterFrame();
  return { lfo, env: 0.6, vel: 0.7, mw: 0.5 };
}
