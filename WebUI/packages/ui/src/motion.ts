/** Vocabolario di motion: sorgente unica dei valori. `theme.css` li rispecchia come custom
    properties e `motion.test.ts` verifica che le due copie non divergano. */

/** Durate in millisecondi. */
export const DUR = { press: 70, state: 120, layer: 200, scene: 320 } as const;

export type Bezier = readonly [number, number, number, number];

/** Curve. `snap` è il default storico; `glass` è un expo-out (la luce arriva e si posa);
    `settle` ha l'unico overshoot autorizzato, per ciò che ha massa; `exit` è un ease-in. */
export const EASE = {
  snap: [0.2, 0.9, 0.3, 1],
  glass: [0.16, 1, 0.3, 1],
  settle: [0.34, 1.26, 0.64, 1],
  exit: [0.4, 0, 1, 1],
} as const satisfies Record<string, Bezier>;

/** Asimmetria del vetro: si illumina con calma, si spegne subito. */
export const EXIT_RATIO = 0.6;

export const bezier = (e: Bezier) => `cubic-bezier(${e.join(", ")})`;

export type Transition = { duration: number; ease: Bezier };

/** Transizioni pronte per `motion` (che vuole secondi e array, non `var(--…)`). */
export const T = {
  layerIn: { duration: DUR.layer / 1000, ease: EASE.glass },
  layerOut: { duration: (DUR.layer * EXIT_RATIO) / 1000, ease: EASE.exit },
  /** Crossfade del contenuto delle Tabs (src/synth/ui/Tabs.tsx): stessa asimmetria di
      layerIn/layerOut, ma su DUR.state invece di DUR.layer. Con `AnimatePresence mode="wait"`
      l'uscita e l'entrata sono in sequenza, non sovrapposte (le pagine sono in normal flow: farle
      sovrapporre le impilerebbe), quindi la durata composta è la somma delle due — usare il
      token del layer (200 + 120 = 320ms) la porta a quasi il triplo dei 120ms con cui scorre
      l'indicatore sotto, lasciando un buco morto in mezzo. DUR.state è lo stesso token che guida
      già quell'indicatore (vedi Tabs.tsx/Segmented.tsx in @xerum/ui): la somma resta 192ms, non
      120, ma è vicina invece che tripla. */
  stateIn: { duration: DUR.state / 1000, ease: EASE.glass },
  stateOut: { duration: (DUR.state * EXIT_RATIO) / 1000, ease: EASE.exit },
} as const satisfies Record<string, Transition>;
