// L'ambient audio non passa da React: a 30 Hz si scrivono tre custom properties sul chassis
// e il CSS fa il resto. Far ri-renderizzare N componenti trenta volte al secondo dentro una
// WebView che divide la CPU con il motore audio è esattamente ciò che meterStore evita
// (vedi il commento in ../../juce/meters.ts).
import { useEffect, type RefObject } from "react";
import { meterStore } from "../../juce/meters";
import { useBackend } from "../../juce/provider";
import type { MeterFrame } from "../../juce/backend";

/** Un numero finito, portato in 0..1: un NaN o un valore fuori range dal ponte non deve
    passare a opacity/brightness, che lo mostrerebbero come un buco o un lampo. */
const unit = (v: number) => (Number.isFinite(v) ? Math.min(1, Math.max(0, Math.abs(v))) : 0);

/** L'unica variabile da cui dipende l'ambient oggi. Pura, così è testabile senza rAF.
    --m-env e --m-lfo (il glow dei section header e il respiro del LED dell'LFO, sezione 5
    dello spec) non sono mai stati costruiti: scriverli comunque a 30 Hz era un costo per due
    letture che non esistono in nessuna variante del chassis (verificato con un grep dell'intero
    repo). Se quei due consumatori nasceranno, la scrittura torna qui accanto a --m-out, non
    prima. */
export function writeMeterVars(el: HTMLElement, f: MeterFrame) {
  el.style.setProperty("--m-out", String(unit(f.out)));
}

/** Un solo rAF per tutta la finestra, coalescente: più frame nello stesso vsync ne scrivono uno. */
export function useMeterConductor(ref: RefObject<HTMLElement | null>) {
  const backend = useBackend();
  useEffect(() => {
    const store = meterStore(backend);
    let raf = 0;
    const flush = () => {
      raf = 0;
      const el = ref.current;
      if (el) writeMeterVars(el, store.get());
    };
    const off = store.subscribe(() => {
      if (!raf) raf = requestAnimationFrame(flush);
    });
    return () => {
      if (raf) cancelAnimationFrame(raf);
      off();
    };
  }, [backend, ref]);
}
