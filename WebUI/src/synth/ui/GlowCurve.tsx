/**
 * Una curva su uno schermo: area riempita sotto e tratto luminoso del colore di sezione.
 * La usano la risposta del filtro e l'inviluppo; `close` e' il tratto che chiude l'area
 * (ogni schermo ha il suo bordo).
 */
export function GlowCurve({ d, close, strokeWidth = 1.8 }: { d: string; close: string; strokeWidth?: number }) {
  return (
    <>
      <path d={`${d} ${close}`} className="fill-(--tone)" opacity={0.12} />
      <path d={d} className="fill-none stroke-(--tone) [filter:var(--glow)]" strokeWidth={strokeWidth} />
    </>
  );
}
