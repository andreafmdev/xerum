/** Posizione e larghezza dell'indicatore, relative al suo contenitore. */
export type IndicatorBox = { x: number; width: number };

/** La misura da cui nasce un `translateX`/`scaleX`: transform puro, nessun reflow.
    È il sostituto di `layoutId`, che il sistema di motion vieta. */
export function measureIndicator(container: HTMLElement, item: HTMLElement): IndicatorBox {
  const c = container.getBoundingClientRect();
  const i = item.getBoundingClientRect();
  return { x: i.left - c.left, width: i.width };
}

/** Larghezza di base dell'indicatore, prima di qualunque `scaleX`: usa `offsetWidth`, MAI
    `getBoundingClientRect()`. `getBoundingClientRect` riporta il box DOPO il transform
    corrente; se quel transform è già uno `scaleX` (0 al primo render, perché la base parte a
    0), la misura letta è il risultato dello scale precedente, non la larghezza di layout — un
    ciclo di retroazione con punto fisso a zero: `scaleX(0)` rende un box largo 0, la lettura
    torna 0, lo scale factor per una base 0 è 0 per definizione (vedi `indicatorScale`), e non
    si esce mai da lì: l'indicatore resta invisibile per sempre. `offsetWidth` riporta invece il
    box di LAYOUT, ignora qualunque transform applicato, e arrotonda a intero — esattamente
    l'invariante che serve qui (2 quando un bordo impedisce a un `width: 1px` dichiarato di
    scendere sotto i 2px, 1 quando non lo impedisce), qualunque sia lo scale corrente. */
export function baseWidthOf(el: HTMLElement): number {
  return el.offsetWidth;
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
