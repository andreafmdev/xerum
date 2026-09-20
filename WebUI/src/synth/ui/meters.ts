import { useMeterValue } from "../../juce/hooks";
import { liveValue, type ModAssignment } from "../mod";

/**
 * Il valore modulato istantaneo di un parametro: base più depth × livello di ogni sorgente.
 * Selezionato direttamente dal frame dei meter, quindi il componente ri-renderizza solo quando
 * il risultato cambia — non per un frame che muove una sorgente non assegnata a questo knob.
 */
export function useLiveValue(value: number, mods: ModAssignment[]): number {
  return useMeterValue((f) => liveValue(value, mods, f));
}
