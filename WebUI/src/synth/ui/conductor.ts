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

/** Le tre variabili da cui dipende l'ambient. Pura, così è testabile senza rAF. */
export function writeMeterVars(el: HTMLElement, f: MeterFrame) {
  el.style.setProperty("--m-out", String(unit(f.out)));
  el.style.setProperty("--m-env", String(unit(f.env)));
  el.style.setProperty("--m-lfo", String(unit(f.lfo)));
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
