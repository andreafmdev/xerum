/** Posizione e larghezza dell'indicatore, relative al suo contenitore. */
export type IndicatorBox = { x: number; width: number };

/** La misura da cui nasce un `translateX`/`scaleX`: transform puro, nessun reflow.
    È il sostituto di `layoutId`, che il sistema di motion vieta. */
export function measureIndicator(container: HTMLElement, item: HTMLElement): IndicatorBox {
  const c = container.getBoundingClientRect();
  const i = item.getBoundingClientRect();
  return { x: i.left - c.left, width: i.width };
}
