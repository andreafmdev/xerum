# Sistema di motion: vocabolario, controlli, layer, ambient

Data: 2026-09-20. Stato: approvato in chat, in attesa del piano.

## Obiettivo

La WebUI oggi ha due token di motion (`--ease-snap` e `--default-transition-duration: 120ms` in `packages/ui/src/theme.css:97-98`) e una manciata di transizioni scritte caso per caso. Il risultato è un'interfaccia corretta ma inerte: i controlli non hanno fisica, i layer appaiono e spariscono di colpo, il click non conferma niente, e la reattività audio che il `meterStore` già fornisce non arriva a nessun pixel che non sia un meter.

Questo documento definisce un **vocabolario di motion unico** — durate, curve, regole di asimmetria — tokenizzato in `@xerum/ui` come già lo sono i colori, e lo applica a quattro livelli: controlli discreti, controlli continui, layer e transizioni, ambient audio-reattivo e boot.

Il carattere scelto è **glass/futurista**, coerente col design "Versione glassy" da cui vengono le varianti `glass` e `metal` del chassis: si muove la luce più della materia.

## Non obiettivi

- **Layout animations** (`layout`, `layoutId` di motion): vietate, vedi "Vincolo di rendering".
- **Pannelli collassabili**: il chassis è a dimensione fissa 900×680 (`synth.css:22-23`), non c'è spazio da guadagnare. Nessun pannello collassa, quindi nessuna animazione di altezza serve.
- **Stagger sulle liste**: le card preset entrano come blocco unico, non una per una.
- **Animare i 40 parametri di un preset load**: un wipe di luce sul chassis, non quaranta knob in movimento.
- **Toggle utente "low graphics"**: il budget conservativo rende ogni effetto già economico; un interruttore in più non serve. `prefers-reduced-motion` resta l'unica degradazione.

## Vincolo di rendering (regola di review, non consiglio)

La WebView gira nel processo del DAW e condivide CPU e GPU con l'audio. Vale una whitelist:

| Animabile | Vietato |
|---|---|
| `transform`, e le proprietà indipendenti `translate`, `scale`, `rotate` | `backdrop-filter`, e il raggio di `blur()` |
| `opacity` | il raggio di `blur()` dentro un `box-shadow` |
| `filter: brightness()`, `saturate()` | `width`, `height`, `top`, `left`, `grid-template-rows` |
| custom properties consumate da `color-mix` | qualunque proprietà che forzi layout |

`box-shadow` merita una riga a parte, perché la prima stesura di questo documento lo vietava nella tabella e poi lo prescriveva tre sezioni più sotto, dove la lista del bottone lo contiene per nome. Le implementazioni hanno seguito la sezione, non la tabella, e avevano ragione.

La regola vera: si può animare un `box-shadow` su un **controllo** — un anello che si accende, un cappuccio che affonda — dove la superficie è piccola e il raggio di sfocatura resta fisso, perché il costo è il repaint di pochi pixel. Resta vietato dove la sfocatura stessa cambia, e su superfici grandi come il chassis o un pannello, dove lo stesso repaint costa quanto la finestra intera.

Questa distinzione **nessun linter la può esprimere**, perché dipende dalla dimensione della superficie e non dalla sintassi: è una regola per chi fa review, scritta qui perché sia una decisione invece di una dimenticanza.

Il blur dei layer esiste dal primo frame e resta costante: sale solo l'opacity del contenuto sopra. È la regola più controintuitiva del documento — il blur che cresce sarebbe l'effetto più bello e il più caro — e per questo è la prima che `check:motion` verifica.

## Sezione 1 — Fondazione

### Sorgente unica: `packages/ui/src/motion.ts`

Motion (la libreria) vuole numeri e array, CSS vuole `var(--…)`. Due liste separate divergono; quindi una sola, in TypeScript, da cui discendono entrambe.

```ts
export const DUR = { press: 70, state: 120, layer: 200, scene: 320 } as const;

export const EASE = {
  snap:   [0.2, 0.9, 0.3, 1],    // esistente, resta il default
  glass:  [0.16, 1, 0.3, 1],     // expo-out: la luce arriva e si posa
  settle: [0.34, 1.26, 0.64, 1], // overshoot minimo, solo sui rilasci con massa
  exit:   [0.4, 0, 1, 1],        // ease-in: uscire è più svelto che entrare
} as const;

/** Oggetti `Transition` pronti per motion, derivati dagli stessi valori. */
export const T = {
  layerIn:  { duration: DUR.layer / 1000, ease: EASE.glass },
  layerOut: { duration: (DUR.layer * 0.6) / 1000, ease: EASE.exit },
} as const;
```

`theme.css` espone gli stessi valori come `--dur-press/state/layer/scene` e `--ease-glass/settle/exit`. `--ease-snap` e `--default-transition-duration` restano come sono: `DUR.state` **è** quel valore, non un secondo valore che gli somiglia.

### Regola di asimmetria

**Uscita = entrata × 0.6.** Il vetro si illumina con calma e si spegne subito. Vale ovunque, tranne due eccezioni documentate nella sezione 2 (pressione di un tasto, spegnimento di un LED), che sono inversioni deliberate e non sviste.

### Override per variante di chassis

Come già per i colori, `.sx-chassis[data-variant]` ridefinisce i token: `glass` usa `--ease-glass` e durate piene, `metal` accorcia su `--ease-snap`, `soft`/`deep`/`glow` stanno in mezzo. Una primitiva di `@xerum/ui` non sa in che chassis vive: legge i token.

Questo blocco è l'unico punto, oltre a `motion.ts` e al suo specchio in `theme.css`, dove una durata in millisecondi può essere scritta a mano. La regola del valore unico esiste per impedire che **lo stesso** valore viva in due copie che divergono; una durata per variante non è una copia, è un valore nuovo, e appartiene alla tuning di materiale accanto agli override di colore che le stanno già intorno. Fuori da quel blocco la regola vale intera.

### Runtime

`App.tsx` monta `<LazyMotion features={domAnimation} strict>`. `strict` fa fallire a runtime ogni `motion.div`, obbligando `m.div`; `domAnimation` esclude di suo le layout animations. Il divieto della sezione "Non obiettivi" è quindi imposto dal bundle, non dalla disciplina. Peso atteso nell'ordine di 15-20 kB gzip contro i ~34 del bundle pieno; il numero vero si misura in implementazione e finisce nel piano.

### Reduced motion

`useReducedMotion()` per i componenti JS, `@media (prefers-reduced-motion: reduce)` per il CSS — la regola a `synth.css:241` esiste già per l'aurora e si estende. A reduced il movimento va a zero e l'opacity resta, ma a `DUR.press`. Nessuna funzione sparisce: si smette solo di spostare pixel.

## Sezione 2 — Controlli discreti

### Il difetto da cui si parte

`packages/ui/src/components/ui/button.tsx:6` usa `transition-all`. Anima anche `background-color` e `border-color`, quindi alla pressione il colore insegue il transform invece di arrivare insieme a lui. Si sostituisce con una lista esplicita di proprietà su `--dur-press` / `--ease-snap`.

La lista va compilata guardando le varianti, non a memoria: deve contenere `color` (`ghost` e `outline` cambiano il colore del testo) e `opacity` (`disabled:opacity-50`) oltre a `transform`, `box-shadow`, `background-color` e `border-color`. Una proprietà dimenticata non sparisce: smette di animare e scatta, ed è una regressione più silenziosa di quella che questa sezione corregge.

### Prima inversione: la pressione

Discesa **istantanea** (0 ms), risalita in `--dur-press`. Il dito è più veloce della molla, non il contrario. `active:translate-y-px` e `active:shadow-none` (`Button.tsx:21`) restano: guadagnano solo la curva sul ritorno.

### Seconda inversione: il LED

Il pallino di stato (`Toggle.tsx:46`) si accende in `--dur-press` e si spegne in `--dur-state`. Un LED reale ha persistenza allo spegnimento. È l'opposto esatto della regola glass, ed è voluto.

### Thumb del Toggle

`[&_[data-slot=switch-thumb][data-checked]]:translate-x-3!` (`Toggle.tsx:40`) è l'unico punto dell'interfaccia dove l'overshoot è giustificato: il thumb ha massa. Usa `--ease-settle`.

### Segmented: indicatore scorrevole

`Segmented.tsx` oggi accende e spegne ogni piastrina per conto suo — è la "transizione brusca" nella sua forma più pura. Si aggiunge un `<span>` assoluto dentro l'incasso, posizionato con `transform: translateX() scaleX()` calcolato da `offsetLeft` / `offsetWidth` del bottone selezionato, rimisurato al cambio di selezione e su resize.

Transform puro: nessun reflow. È il sostituto di `layoutId`, che abbiamo vietato, e fa lo stesso lavoro a costo zero. Lo stesso componente serve i Tabs.

### Focus ring

`focus-visible:ring-3` (già in `button.tsx:6`) entra in `--dur-state` con una scala da 0.96. A reduced-motion compare senza scala.

### Keybed

Il tasto premuto col mouse e la nota in arrivo da MIDI devono passare per lo stesso stato visivo, guidato dallo stesso attributo. Se i due percorsi divergono si vede immediatamente: una nota MIDI illuminerebbe il tasto in modo diverso da un click sullo stesso tasto.

## Sezione 3 — Controlli continui

### La regola che conta più di tutte le altre

**Durante il drag, zero animazione.** `[data-dragging=true]` impone `transition: none` sull'arco del valore e sul puntatore. Qualsiasi smoothing qui diventa lag percepito, e un knob che lagga è un knob rotto. È il rischio numero uno dell'intera feature.

`Knob.tsx:177` espone già `data-dragging` (da `useDragValue`, che lo restituisce nel suo risultato): il gancio esiste, serve solo la regola CSS.

### Il corollario

Il knob anima **solo** quando il valore cambia da una sorgente che non è il puntatore: doppio click sul default, preset load, automazione dalla DAW, rotella, frecce. Poiché `data-dragging` distingue già i due casi, questa è una regola CSS e non una modifica all'API di `useDragValue`.

### Il settle va sul cappuccio, non sul valore

Al rilascio, `data-part="cap"` fa un micro-rebound di scala con `--ease-settle`. `data-part="value"` e `data-part="pointer"` non si muovono di un pixel: il valore è un dato, non una molla. Confondere i due significa mostrare all'utente un valore che il motore non ha.

### `liveValue`: mai una transizione CSS

Il puntino modulato (`Knob.tsx:233`) arriva già smoothed dal `meterStore` a 30 Hz. Una transizione da 120 ms su un flusso con periodo 33 ms non finisce mai prima del frame successivo: il ritardo si accumula e il puntino resta indietro rispetto all'audio. Stesso divieto per gli anelli di modulazione.

### Wheel

Il ritorno a centro del pitch bend è l'unica molla fisica vera dell'interfaccia e usa `--ease-settle` pieno, non ridotto.

## Sezione 4 — Layer e transizioni

### PresetOverlay

`SynthWindow.tsx:176` monta l'overlay con `{s.browse && <PresetOverlay …>}`: smontaggio secco, nessuna uscita. È il punto in cui `AnimatePresence` guadagna il suo posto nel bundle.

- **Enter**: opacity 0→1 e scale 0.99→1, `T.layerIn`. È già il comportamento ottenuto via `tw-animate-css` a `PresetOverlay.tsx:39` (`animate-in fade-in zoom-in-[0.99] duration-150`); migra a motion per ottenere l'uscita, e le classi `tw-animate-css` su quel nodo spariscono.
- **Exit**: opacity 1→0 e scale 1→0.995, `T.layerOut`.
- **`backdrop-blur-sm` statico**, per il vincolo di rendering.

### Griglia delle card

Nessuno stagger: 11+ card sono 11+ animazioni simultanee. La griglia entra come blocco unico dentro l'enter dell'overlay.

### Tabs

Indicatore: lo stesso componente scorrevole della sezione 2. Contenuto: **crossfade corto, non slide** — uno slide su 900 px di chassis muove troppi pixel per il budget.

### ModChip, drag e drop

Il momento più espressivo del synth, e costa quasi niente perché i ganci ci sono già:

- `ModChip.tsx:16` ha `cursor-grab` / `active:cursor-grabbing`.
- `Knob.tsx:160` ha `data-drop-target`, e `Knob.tsx:182` il ring che ne consegue.

Manca solo: la transizione del ring su `--dur-state` quando il drag comincia, e la comparsa dell'anello di modulazione con uno scale-in dall'arco al momento del drop. Il secondo è un'opacity più un transform su un `<path>` SVG che viene già renderizzato.

## Sezione 5 — Ambient audio-reattivo e boot

### Il conductor

Motion non entra in questa sezione: a 30 Hz si scrivono custom properties, non si montano componenti.

`useMeterConductor()` si abbona al `meterStore` (`src/juce/meters.ts`) e, in un `requestAnimationFrame` coalescente, scrive `--m-lfo`, `--m-env`, `--m-out` sul nodo `.sx-chassis` con `style.setProperty`. **Zero re-render React** — è lo stesso principio per cui `meters.ts` usa i selettori invece di un provider che ri-renderizza tutti i consumatori trenta volte al secondo.

Consumatori di quelle variabili:

- l'aurora di `synth.css:227` guadagna un fattore di opacity legato a `--m-out`;
- i glow dei section header pulsano su `--m-env`;
- il LED dell'LFO respira su `--m-lfo`.

**Solo `opacity` e `brightness` da queste variabili.** Mai un transform sul chassis: 900×680 px ricompositi a 30 Hz sono il modo più rapido per far scattare l'interfaccia dentro un DAW.

### Boot

Un plugin crea e distrugge l'editor a ogni apertura della finestra. Una sequenza di accensione da 600 ms alla decima apertura è una tassa, non un effetto.

- **Primo mount nel processo**: chassis scale 0.985→1 con opacity, poi tre gruppi di pannelli in stagger (tre, non dodici), poi i LED. Circa 600 ms in totale.
- **Riaperture successive**: fade corto su `--dur-layer`.

La distinzione vive in una `let` a livello di modulo, che sopravvive al ciclo di vita del componente ma non al processo.

### Preset load

Un wipe di luce sul chassis su `--dur-scene`. Non quaranta knob animati.

## Test

jsdom non anima: si testano i contratti, non i pixel.

- `PresetOverlay` resta montato dopo `onClose` finché l'uscita non è finita.
- Il knob porta `data-dragging` durante il drag e non dopo.
- A `prefers-reduced-motion: reduce` le durate di movimento vanno a zero.
- `motion.ts` e `theme.css` espongono gli stessi valori (test di coerenza, come `params.freshness.test.ts` fa per i parametri).

Il giudizio estetico sta in Storybook: una story "Motion" per variante di chassis che mostri tutti gli stati insieme.

## `check:motion`

Sul modello di `check:colors` già in `packages/ui/package.json`, uno script che fallisce il build quando trova:

- `backdrop-filter` o `blur(` dentro una dichiarazione `transition`;
- `layoutId` o `layout` come prop;
- `motion.` invece di `m.`;
- una durata in millisecondi scritta a mano fuori da `motion.ts`.

Rende eseguibili le regole delle sezioni 1 e 4 invece di affidarle alla memoria di chi fa review.

## Gate di merge

Standalone Release più un DAW reale, con misura dei frame, prima del merge — la prassi che il progetto già applica alle modifiche che toccano la WebView.
