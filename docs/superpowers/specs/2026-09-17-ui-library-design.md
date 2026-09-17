# Spec — Libreria componenti `@xerum/ui`

Data: 2026-09-17
Stato: bozza approvata a sezioni in chat, in attesa di review finale

## 1. Scopo

Creare una libreria di componenti React per la UI del synth (WebView JUCE 8) che:

1. serva da base per la UI reale del plugin (fasi 2–7 della roadmap in `docs/architecture.md`);
2. sia sincronizzabile su Claude Design tramite `/design-sync` (shape `storybook`), così che l'agente di design costruisca schermate con i componenti reali.

Fuori scope v1: EnvelopeDisplay, LfoDisplay, ModMatrix, PresetBrowser, Keyboard, tema light, i18n, touch multi-punto, bridge APVTS (fase 4 — i componenti espongono solo valori normalizzati).

## 2. Decisioni prese

| Tema | Decisione |
|---|---|
| Perimetro v1 | Set synth essenziale: Knob, Fader, Toggle, Select, Button, Panel, SectionHeader, Tabs, ValueReadout, WavetableDisplay |
| Packaging | Package separato `WebUI/packages/ui`, pnpm workspace con root `WebUI/` |
| Package manager | pnpm (lockfile unico `WebUI/pnpm-lock.yaml`; `package-lock.json` rimosso) |
| Styling | Tailwind v4 CSS-first (`@theme`), utility inline + CVA. CSS compilato shippato in `dist/` |
| Base componenti | shadcn/ui, preset `base-nova` (primitive Base UI, stile nova). Sorgente copiata nel package |
| Look | Evoluzione tema attuale: dark blu-grafite, accent menta `#6ee7c5`, IBM Plex Sans/Mono. Solo dark |
| Interazione controlli | Drag verticale, Shift = fine, rotella, doppio click = default, tastiera |
| Valori | Sempre normalizzati `0..1`. Mapping a parametri APVTS fuori dai componenti |
| Test | Vitest + Testing Library (jsdom), TDD per hook e componenti con logica |
| Storybook | Storybook 10 `@storybook/react-vite`, una story file per componente |

## 3. Struttura package

```
WebUI/
  pnpm-workspace.yaml          packages: ["packages/*"]
  package.json                 app shell; dep "@xerum/ui": "workspace:*"
  packages/ui/
    package.json               name @xerum/ui, type module
                               exports: "." (js+d.ts), "./ui.css", "./theme.css"
    components.json            config shadcn (base-nova, tailwind v4, aliases @/)
    vite.config.ts             lib mode ES, external react/react-dom/react/jsx-runtime,
                               vite-plugin-dts, @tailwindcss/vite
    tsconfig.json              strict, jsx react-jsx, paths @/* -> src/*
    vitest.config.ts           environment jsdom, setup testing-library
    .storybook/main.ts         framework react-vite, stories src/**/*.stories.tsx
    .storybook/preview.ts      import src/index.css, background = --background
    fonts/                     IBM Plex Sans / Mono woff2 (self-hosted)
    src/index.ts               barrel: export named di tutti i componenti pubblici
    src/index.css              @import tailwindcss/theme + utilities (no preflight),
                               @import ./theme.css, @font-face, :root color-scheme dark
    src/theme.css              token (sezione 4)
    src/lib/utils.ts           cn() = clsx + tailwind-merge (da shadcn)
    src/hooks/useDragValue.ts  logica drag/rotella/tastiera condivisa
    src/components/ui/         componenti shadcn copiati dal CLI (button, switch, select,
                               tabs, card, slider, badge, tooltip, separator, label)
    src/components/<Name>/     componenti nostri: <Name>.tsx, <Name>.stories.tsx,
                               <Name>.test.tsx (se logica)
```

Regole:

- I file in `src/components/ui/` sono quelli generati dal CLI shadcn; modifiche locali ammesse ma documentate in testa al file con un commento `// xerum:` per rendere possibile `shadcn add --diff` in futuro.
- I componenti pubblici del design system vivono in `src/components/<Name>/` e wrappano o riesportano quelli in `ui/`. Il barrel `src/index.ts` esporta solo questi. Le primitive `ui/` non sono API pubblica (eccezione: `Tooltip`, `Separator`, `Label` riesportati as-is).
- App shell (`WebUI/src`) importa `@xerum/ui` e `@xerum/ui/ui.css`; nel proprio Tailwind importa `@xerum/ui/theme.css` per usare gli stessi token nella colla di layout.
- Preflight: la libreria non lo include (evita doppio reset); l'app shell importa `tailwindcss` completo.

## 4. Token (`src/theme.css`)

Vocabolario semantico shadcn + estensioni synth. Solo dark: i valori stanno su `:root`, nessun blocco `.dark`. Mapping via `@theme inline` come nello schema shadcn per Tailwind v4.

### 4.1 Semantici shadcn (valori dal tema attuale)

| Variabile | Valore | Origine |
|---|---|---|
| `--background` | `#0e1016` | `--bg` attuale |
| `--foreground` | `#e9ecf5` | `--text` attuale |
| `--card` | `#171a24` | `--panel` attuale |
| `--card-foreground` | `#e9ecf5` | |
| `--popover` | `#1e2230` | |
| `--popover-foreground` | `#e9ecf5` | |
| `--primary` | `#6ee7c5` | `--accent` attuale |
| `--primary-foreground` | `#0e1016` | |
| `--secondary` | `#262b3a` | |
| `--secondary-foreground` | `#e9ecf5` | |
| `--muted` | `#1e2230` | |
| `--muted-foreground` | `#9aa3b8` | `--muted` attuale |
| `--accent` | `#262b3a` | hover (semantica shadcn, non il colore menta) |
| `--accent-foreground` | `#e9ecf5` | |
| `--destructive` | `#ff6b6b` | |
| `--border` | `#2a3144` | `--line` attuale |
| `--input` | `#2a3144` | |
| `--ring` | `#6ee7c5` | |
| `--radius` | `0.5rem` | sm/md/lg/xl derivati come da shadcn |

### 4.2 Estensioni xerum (in `@theme`)

```css
/* superfici aggiuntive, dal più scuro al più chiaro */
--color-surface-0: #12151d;
--color-surface-1: #171a24;
--color-surface-2: #1e2230;
--color-surface-3: #262b3a;
--color-line-strong: #3a4258;
--color-text-dim: #5f6880;
--color-warning: #ffc857;

/* colore per sezione synth */
--color-osc: #6ee7c5;
--color-filter: #7aa2ff;
--color-env: #ffb86b;
--color-lfo: #d68cff;
--color-fx: #ff8fb1;
--color-master: #e9ecf5;

/* tipografia */
--font-sans: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
--font-mono: "IBM Plex Mono", ui-monospace, monospace;
--text-2xs: 0.625rem;

/* misure controlli */
--spacing-knob-sm: 2rem;
--spacing-knob-md: 3rem;
--spacing-knob-lg: 4rem;
--spacing-fader: 8rem;

/* motion */
--ease-snap: cubic-bezier(0.2, 0.9, 0.3, 1);
--default-transition-duration: 120ms;
```

### 4.3 Regole di stile

- Nessun colore hardcoded nei componenti: solo token semantici (`bg-card`, `text-muted-foreground`, `border-border`, `text-primary`) o estensioni (`bg-surface-2`, `text-osc`). Verificabile con `grep -E '#[0-9a-f]{3,8}|rgb\(' src/components` → zero risultati.
- Il colore di sezione viaggia come variabile CSS locale `--tone`. `Panel` la imposta (`[--tone:var(--color-filter)]`), i figli la consumano (`text-(--tone)`, `stroke-(--tone)`). Un componente con prop `tone` esplicita la sovrascrive; senza prop eredita; senza Panel cade su `--primary`.
- Regole shadcn del progetto: `gap-*` non `space-*`, `size-*` per dimensioni uguali, `cn()` per classi condizionali, icone `lucide-react` con `data-icon` dentro `Button`.
- Font self-hosted in `fonts/` con `@font-face` in `index.css`: la WebView JUCE può essere offline e design-sync carica `fonts/`.

## 5. Componenti

Convenzioni comuni: `forwardRef` sull'elemento radice, `className` mergiato con `cn()`, prop `tone?: "osc" | "filter" | "env" | "lfo" | "fx" | "master"`, export named. Ogni componente ha `<Name>.stories.tsx` con gli stati principali; ha `<Name>.test.tsx` se contiene logica.

### 5.1 Hook `useDragValue`

```ts
type UseDragValueOptions = {
  value: number;              // 0..1
  defaultValue?: number;      // usato dal doppio click; default 0
  onChange: (v: number) => void;
  step?: number;              // rotella/tastiera; default 0.01
  sensitivity?: number;       // px per corsa completa; default 200
  disabled?: boolean;
};
type UseDragValueResult = {
  handlers: {
    onPointerDown: PointerEventHandler;
    onWheel: WheelEventHandler;
    onDoubleClick: MouseEventHandler;
    onKeyDown: KeyboardEventHandler;
  };
  dragging: boolean;
};
```

Comportamento:

- `pointerdown` → `setPointerCapture`, memorizza `y0` e `value0`. `pointermove` → `value0 + (y0 - y) / sensitivity`; con Shift premuto la sensibilità è ×10 (fine). `pointerup`/`pointercancel` → rilascio.
- `wheel` → `±step` (Shift: `±step/10`), `preventDefault`.
- `dblclick` → `defaultValue`.
- Tastiera: ↑/→ `+step`, ↓/← `-step`, PageUp/PageDown `±10·step`, Home `0`, End `1`. Shift = `step/10`.
- Sempre clamp `0..1`. `disabled` → handlers no-op. `onChange` chiamato solo se il valore cambia.

### 5.2 Controlli

| Componente | Base | Props | Comportamento |
|---|---|---|---|
| `Knob` | custom + `useDragValue` | `value, defaultValue?, onChange, label, format?: (v) => string, size?: "sm"\|"md"\|"lg", tone?, bipolar?, disabled?` | SVG: track arco 270°, arco valore in `--tone` (da 0 o dal centro se `bipolar`), indicatore. Label sotto in `text-2xs uppercase`. Readout (`format(value)`) visibile in hover/drag/focus. `role="slider"`, `aria-valuenow/min/max/valuetext`, `tabIndex=0` |
| `Fader` | shadcn `Slider` (Base UI) + wrapper | `value, defaultValue?, onChange, label?, format?, orientation?: "vertical"\|"horizontal", tone?, disabled?` | Slider gestisce drag/tastiera/ARIA. Wrapper aggiunge doppio click → `defaultValue`, Shift = passo fine, readout. Lunghezza `--spacing-fader`. Se Base UI Slider non consente il passo fine dinamico, si passa a track custom + `useDragValue` (decisione a implementazione, criterio: meno codice) |
| `Toggle` | shadcn `Switch` | `checked, onChange, label?, tone?, disabled?` | Variante visiva LED: pallino acceso in `--tone` quando on. `role="switch"` dal primitive |
| `Select` | shadcn `Select` | `value, onChange, options: { value: string; label: string }[], label?, placeholder?, disabled?` | Wrapper che compone `SelectTrigger/SelectContent/SelectGroup/SelectItem` da `options`. Popover con `bg-popover` |
| `Button` | shadcn `Button` | shadcn: `variant` (`default`, `outline`, `ghost`, `secondary`, `destructive`), `size` (`sm`, `default`, `icon`) + `tone?` | `tone` imposta `--tone` e la variante `default` usa `bg-(--tone)` al posto di `bg-primary` |

### 5.3 Layout e display

| Componente | Base | Props | Comportamento |
|---|---|---|---|
| `Panel` | shadcn `Card` | `title?, tone?, actions?: ReactNode, children` | Modulo synth: header sottile (titolo uppercase + slot `actions`), bordo superiore 2px in `--tone`, imposta `--tone` per i figli. Body `flex flex-col gap-3` |
| `SectionHeader` | custom | `title, tone?, actions?` | Riga: titolo `text-xs uppercase tracking-widest` in `--tone`, `Separator` che riempie, slot destro |
| `Tabs` | shadcn `Tabs` | `value, onChange, items: { value: string; label: string }[], tone?` | Solo barra (`TabsList` + `TabsTrigger` da `items`); indicatore attivo in `--tone`. Contenuto gestito dal consumer |
| `ValueReadout` | shadcn `Badge` | `value: string, label?, mono?` | Chip `bg-surface-2`; `mono` usa `font-mono tabular-nums` |
| `WavetableDisplay` | custom canvas | `frames: Float32Array[], position: number, tone?, className?` | Disegna il frame più vicino a `position·(frames.length-1)` in `--tone` a piena opacità; ±3 frame vicini dietro, sfumati e traslati (pseudo-3D). `ResizeObserver` + `devicePixelRatio`. Solo render, nessun editing. Se `frames` è vuoto disegna una linea piatta |

### 5.4 Riesportati as-is

`Tooltip` (+ `TooltipTrigger`, `TooltipContent`), `Separator`, `Label`.

## 6. Build e output

- `pnpm --filter @xerum/ui build` → `dist/index.js`, `dist/index.d.ts` (+ sotto-file), `dist/ui.css`.
- `dist/ui.css` contiene: layer theme (variabili), utilities usate dai componenti, `@font-face`. Percorsi font relativi a `dist/` (copia in `dist/fonts/` via `publicDir`).
- Esterni: `react`, `react-dom`, `react/jsx-runtime`. Tutto il resto (Base UI, CVA, lucide) bundlato.
- `pnpm --filter @xerum/ui storybook` → dev server; `build-storybook` → `storybook-static/` (usato da design-sync come riferimento locale).
- App shell: `pnpm --filter serum-style-synth-webui dev` invariato su porta 5173.

## 7. Test

| Unità | Cosa si verifica |
|---|---|
| `useDragValue` | ogni regola di §5.1: drag su/giù, Shift, clamp, rotella, doppio click, tutti i tasti, `disabled`, nessun `onChange` se valore invariato |
| `Knob` | `role="slider"` e ARIA aggiornati, readout compare in hover/focus, `format` applicato, `bipolar` cambia il path dell'arco, `disabled` blocca input |
| `Fader` | orientazione, doppio click reset, ARIA dal primitive |
| `Toggle` | `role="switch"`, click e Space cambiano stato, `disabled` |
| `Tabs` | tab attivo, click e frecce cambiano `value` |
| `Select` | opzioni renderizzate, selezione chiama `onChange` |
| `Panel` | imposta `--tone` sullo stile inline/classe in base a `tone` |
| `WavetableDisplay` | smoke: monta senza errori con `frames` vuoto e con dati (canvas mockato in jsdom) |

Comando: `pnpm --filter @xerum/ui test`. Tutti i test devono passare prima di ogni commit.

## 8. Piano di consegna (macro)

1. Workspace pnpm + package `@xerum/ui` vuoto + shadcn init `base-nova` + Tailwind v4 + tema (§4) + build lib + Storybook + Vitest. Verifica: build pulita, story "Tokens" che mostra i colori.
2. `useDragValue` (TDD) → `Knob` → `Fader`.
3. `Button`, `Toggle`, `Select`, `Tabs`, `ValueReadout`, `Panel`, `SectionHeader`.
4. `WavetableDisplay`.
5. App shell consuma `@xerum/ui`: `App.tsx` diventa una demo con un Panel per osc/filter/env con Knob reali (valori in stato locale).
6. `/design-sync` su `WebUI/packages/ui`.

Il piano dettagliato viene scritto con la skill `writing-plans` dopo l'approvazione di questa spec.

## 9. Rischi

- **Base UI Slider e passo fine**: se non supporta un `step` dinamico con Shift, fallback a track custom con `useDragValue` (§5.2). Decisione presa in fase 2.
- **shadcn CLI in monorepo pnpm**: `components.json` deve stare in `packages/ui`; alias `@/` risolto da `tsconfig.json` del package. Se il CLI non riconosce la struttura, `init --monorepo` non serve (non è un monorepo Next); si configura `components.json` a mano.
- **Tailwind v4 in lib mode**: `@tailwindcss/vite` deve scansionare solo `packages/ui/src`. Se genera utility non usate o manca qualche classe dinamica, si aggiunge `@source` esplicito in `index.css`.
- **Font licenza**: IBM Plex è OFL, redistribuzione in `fonts/` ammessa.

## 10. Addendum 2026-09-18 — decisioni fissate dai probe di fattibilità

Verificate in un progetto usa-e-getta prima di scrivere il piano (`docs/superpowers/plans/2026-09-17-ui-library.md`):

- **Fader custom** (track + `useDragValue`), non Base UI Slider: Base UI usa Shift = passo grande, opposto a §5.1. Chiude il rischio in §9.
- `useDragValue` espone anche `ref` (listener `wheel` nativo non-passive: React registra `wheel` come passive) e `onPointerMove/Up/Cancel`; opzione `axis: "y" | "x"` per il Fader orizzontale.
- I token estensione (§4.2) stanno in `@theme static`: Tailwind v4 omette dal CSS le variabili non usate, design-sync deve vederle tutte.
- I font stanno in `src/fonts/` e vengono inlineati base64 in `dist/ui.css` (Vite lib mode inlinea sempre gli asset). Sostituisce `packages/ui/fonts/` + `dist/fonts/` di §3 e §6.
- `pnpm-workspace.yaml` richiede `onlyBuiltDependencies: [esbuild]`, altrimenti `shadcn add` fallisce (`ERR_PNPM_IGNORED_BUILDS`).
- `components.json` scritto a mano (non `shadcn init --template`, che creerebbe un progetto nuovo); il CLI v4 usa il package `cn` per `cn()` e richiede `@import "shadcn/tailwind.css"` (custom variant `data-checked` ecc.) e `tw-animate-css`.
