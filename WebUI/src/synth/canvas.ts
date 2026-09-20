/** Una variabile CSS di colore letta dall'elemento, o il fallback. */
export const cssColor = (el: Element, name: string, fallback = "currentColor"): string =>
  getComputedStyle(el).getPropertyValue(name).trim() || fallback;

/**
 * Contesto 2D di un canvas con il backing store alla scala davvero applicata.
 *
 * Lo chassis e' scalato con `zoom` o `transform` (SynthWindow.tsx), e nessuno dei due tocca il
 * backing store del canvas: senza moltiplicarlo per la scala, questo resterebbe a risoluzione
 * 1x mentre tutto il resto viene ingrandito — sfocato solo lo schermo dell'onda. Il rapporto
 * rect/clientWidth misura la scala davvero applicata, e regge in entrambi i modi.
 *
 * Ritorna null se il canvas non c'e', non ha contesto (jsdom) o non ha ancora un layout.
 * Il canvas torna pulito e con la trasformazione impostata: si disegna in coordinate CSS.
 */
export function canvas2d(cv: HTMLCanvasElement | null): { ctx: CanvasRenderingContext2D; W: number; H: number } | null {
  const ctx = cv?.getContext("2d");
  if (!cv || !ctx) return null;
  const W = cv.clientWidth;
  const H = cv.clientHeight;
  if (!W || !H) return null;
  const rect = cv.getBoundingClientRect();
  const dpr = (window.devicePixelRatio || 1) * (rect.width > 0 ? rect.width / W : 1);
  const bw = Math.round(W * dpr);
  const bh = Math.round(H * dpr);
  if (cv.width !== bw || cv.height !== bh) {
    cv.width = bw;
    cv.height = bh;
  }
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, W, H);
  return { ctx, W, H };
}
