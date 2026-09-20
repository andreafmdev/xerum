/** Posizione e larghezza dell'indicatore, relative al suo contenitore. */
export type IndicatorBox = { x: number; width: number };

/** La misura da cui nasce un `translateX`/`scaleX`: transform puro, nessun reflow.
    È il sostituto di `layoutId`, che il sistema di motion vieta. */
export function measureIndicator(container: HTMLElement, item: HTMLElement): IndicatorBox {
  const c = container.getBoundingClientRect();
  const i = item.getBoundingClientRect();
  return { x: i.left - c.left, width: i.width };
}

/** Fattore di `scaleX` per portare l'indicatore alla larghezza del box misurato, a partire
    dalla sua larghezza renderizzata reale (`baseWidth`), non da un valore assunto: un bordo,
    un padding, o qualunque altra cosa che box-sizing possa far vincere sulla `width` dichiarata
    cambia quella base sotto i piedi, e assumerla sbagliata scala tutto l'indicatore di un
    fattore sbagliato (è così che un bordo di 1px per lato raddoppiava l'indicatore: la base
    reale era 2px, non 1px). Nessuna divisione per zero quando il layout non esiste ancora
    (jsdom, o il primo render). */
export function indicatorScale(box: IndicatorBox, baseWidth: number): number {
  return baseWidth > 0 ? box.width / baseWidth : 0;
}

/** La stringa `transform` dell'indicatore: `translateX` per la posizione, `scaleX` per la
    larghezza. Mai `width`, che il sistema di motion vieta di animare (reflow nella WebView). */
export function indicatorTransform(box: IndicatorBox, baseWidth: number): string {
  return `translateX(${box.x}px) scaleX(${indicatorScale(box, baseWidth)})`;
}
