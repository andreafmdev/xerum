import { useEffect, useState } from "react";

/** Tempo in secondi, aggiornato a ~30 fps. Fermo a 0 quando `run` è falso. */
export function useClock(run: boolean): number {
  const [t, setT] = useState(0);
  useEffect(() => {
    if (!run) return;
    let id = 0;
    let last = 0;
    const tick = (now: number) => {
      if (now - last > 33) {
        last = now;
        setT(now / 1000);
      }
      id = requestAnimationFrame(tick);
    };
    id = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(id);
  }, [run]);
  return t;
}
