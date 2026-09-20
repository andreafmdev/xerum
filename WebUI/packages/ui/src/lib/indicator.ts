import { useLayoutEffect, useRef, useState, type RefCallback, type RefObject } from "react";

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

export type UseSlidingIndicatorOptions = {
  /**
   * Quando false l'hook non fa nulla: nessuna misura, nessun `ResizeObserver` montato. Un hook
   * non si può chiamare a condizione (regola di React), ma il SUO lavoro sì — è la via corretta
   * per un indicatore condizionale (es. `Tabs` in variante `plate`, che non ne ha uno).
   * @default true
   */
  enabled?: boolean;
};

export type UseSlidingIndicatorResult<Container extends HTMLElement, Item extends HTMLElement> = {
  /** Da mettere sul contenitore (il gruppo/la tablist): l'origine di `box.x`. */
  containerRef: RefObject<Container | null>;
  /** Da mettere sull'elemento che scorre (lo `span` assoluto, `width: 1` come base dello `scaleX`). */
  indicatorRef: RefObject<HTMLSpanElement | null>;
  /** Ref callback per l'i-esimo elemento selezionabile (bottone, tab...). Sostituisce l'array di
      ref locale (`buttons.current[i]` / `triggers.current[i]`) che ciascun consumer teneva prima. */
  itemRef: (index: number) => RefCallback<Item>;
  /** `translateX(...) scaleX(...)`, pronto per `style={{ transform }}`. */
  transform: string;
  /** true solo dopo una misura reale (`box.width > 0`): in jsdom, senza layout, resta false per sempre. */
  measured: boolean;
};

/**
 * Wiring condiviso dietro ogni indicatore che scorre (`Segmented`, `Tabs` in variante `bar`):
 * misura l'elemento attivo dopo il layout, lo rimisura a ogni cambio di indice/conteggio o di
 * larghezza (`ResizeObserver` su contenitore ed elemento attivo), scollega tutto in cleanup, e
 * deriva `transform` dalle funzioni pure qui sopra.
 *
 * Prima di questo hook, lo stesso effect era duplicato riga per riga in `Segmented.tsx` e in
 * `Tabs.tsx`: le uniche righe toccate dai due bug Critical di Segmented (base letta con
 * `getBoundingClientRect` invece di `offsetWidth`, scale calcolato sulla base sbagliata)
 * vivevano in entrambe le copie, sincronizzate solo a mano. Ora c'è un solo posto da correggere
 * se quella storia si ripete.
 */
export function useSlidingIndicator<Container extends HTMLElement = HTMLElement, Item extends HTMLElement = HTMLElement>(
  activeIndex: number,
  itemCount: number,
  { enabled = true }: UseSlidingIndicatorOptions = {},
): UseSlidingIndicatorResult<Container, Item> {
  const containerRef = useRef<Container>(null);
  const indicatorRef = useRef<HTMLSpanElement>(null);
  const itemRefs = useRef<(Item | null)[]>([]);
  const [box, setBox] = useState<IndicatorBox>({ x: 0, width: 0 });
  // Larghezza renderizzata dell'indicatore stesso: la base vera dello `scaleX`, mai assunta
  // a 1px (un bordo o altro può cambiarla sotto i piedi — vedi `baseWidthOf` sopra).
  const [baseWidth, setBaseWidth] = useState(0);

  // Misura dopo il layout, e a ogni cambio di indice attivo, di conteggio degli elementi, o di
  // larghezza del contenitore/elemento attivo (un'etichetta più lunga, un cambio di font, può
  // farla crescere senza che il contenitore stesso cambi larghezza).
  useLayoutEffect(() => {
    if (!enabled) return;
    const container = containerRef.current;
    const item = itemRefs.current[activeIndex];
    if (!container || !item) return;
    const measure = () => {
      setBox(measureIndicator(container, item));
      // offsetWidth, non getBoundingClientRect: quest'ultimo leggerebbe il box già trasformato
      // (lo scaleX corrente), un ciclo di retroazione che si blocca a zero — vedi baseWidthOf.
      setBaseWidth(indicatorRef.current ? baseWidthOf(indicatorRef.current) : 0);
    };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(container);
    ro.observe(item);
    return () => ro.disconnect();
  }, [enabled, activeIndex, itemCount]);

  const itemRef = (index: number): RefCallback<Item> => {
    return (el) => {
      itemRefs.current[index] = el;
    };
  };

  return {
    containerRef,
    indicatorRef,
    itemRef,
    transform: indicatorTransform(box, baseWidth),
    measured: box.width > 0,
  };
}
