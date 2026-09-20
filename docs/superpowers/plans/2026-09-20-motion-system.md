# Motion System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dare alla WebUI di Xerum un vocabolario di motion unico — durate, curve, asimmetrie — tokenizzato in `@xerum/ui` e applicato a controlli, layer e ambient audio-reattivo.

**Architecture:** Un file TypeScript (`packages/ui/src/motion.ts`) è la sorgente unica dei valori; `theme.css` li rispecchia come custom properties e un test ne verifica la coerenza. I controlli (`@xerum/ui`) restano **CSS puro, senza dipendenze runtime**: leggono i token. Solo il livello applicativo (`WebUI/src`) usa `motion/react`, dietro `LazyMotion` + `domAnimation`, e solo per l'uscita dell'overlay e il crossfade dei tab. L'ambient audio non passa da React: un `rAF` coalescente scrive custom properties sul chassis.

**Tech Stack:** React 19.3, Tailwind v4, Base UI 1.8, `motion` (motion/react), Vitest 5 + Testing Library, Storybook 10.

**Spec:** `docs/superpowers/specs/2026-09-20-motion-system-design.md`

## Global Constraints

- **Whitelist di proprietà animabili:** `transform`, `opacity`, `filter: brightness()/saturate()`, custom properties consumate da `color-mix`. Vietati: `backdrop-filter`, raggio di `blur()`, `box-shadow` con `spread` variabile, `width`, `height`, `top`, `left`, `grid-template-rows`.
- **Vietate le layout animations:** nessun `layout` o `layoutId`, nessun import di `motion.` (solo `m.`).
- **Nessuna durata in millisecondi scritta a mano** fuori da `packages/ui/src/motion.ts`, dal suo specchio in `theme.css` e dal blocco di override per variante in `WebUI/src/synth/ui/synth.css`. Nel markup si usa `duration-(--dur-press|state|layer|scene)`. La regola dei valori unici vieta due copie dello stesso valore; una durata per variante è un valore nuovo, non una copia. La regola `hardcoded-duration` di `check:motion` sorveglia le utility Tailwind (`duration-150`), non le dichiarazioni di custom property.
- **Asimmetria glass:** uscita = entrata × 0.6. Due eccezioni deliberate: la pressione di un tasto (discesa istantanea, risalita in `--dur-press`) e lo spegnimento di un LED (accensione `--dur-press`, spegnimento `--dur-state`).
- **`@xerum/ui` non dipende da `motion`.** La libreria resta CSS puro; `motion` è dipendenza del solo pacchetto `xerum-webui`.
- **Mai animare durante il drag di un controllo continuo:** `[data-dragging=true]` impone `transition-none`.
- **I flussi a 30 Hz non hanno transizioni CSS:** `liveValue`, anelli di modulazione, meter.
- Commenti nel codice in italiano (convenzione del repo). Messaggi di commit in inglese.
- Ogni task si esegue dalla cartella `WebUI/` salvo indicazione diversa.

---

### Task 1: Token di motion e coerenza con il tema

**Files:**
- Create: `WebUI/packages/ui/src/motion.ts`
- Create: `WebUI/packages/ui/src/motion.test.ts`
- Modify: `WebUI/packages/ui/src/theme.css:97-98` (blocco `@theme`)
- Modify: `WebUI/packages/ui/src/index.ts` (riesporta i token)
- Modify: `WebUI/src/synth/ui/synth.css` (override dei token per variante di chassis)

**Interfaces:**
- Consumes: niente.
- Produces: `DUR: { press: 70; state: 120; layer: 200; scene: 320 }`, `EASE: { snap; glass; settle; exit }` (ogni valore `readonly [number, number, number, number]`), `bezier(e: Bezier): string`, `EXIT_RATIO: 0.6`, `T: { layerIn: Transition; layerOut: Transition }` dove `Transition = { duration: number /* secondi */; ease: Bezier }`. Custom properties CSS: `--dur-press|state|layer|scene`, `--ease-snap|glass|settle|exit`.

- [ ] **Step 1: Write the failing test**

Create `WebUI/packages/ui/src/motion.test.ts`:

```ts
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";
import { bezier, DUR, EASE, EXIT_RATIO, T } from "./motion";

const theme = readFileSync(resolve(import.meta.dirname, "theme.css"), "utf8");

describe("motion tokens", () => {
  it("mirrors every duration as a --dur-* custom property", () => {
    for (const [name, ms] of Object.entries(DUR)) {
      expect(theme).toContain(`--dur-${name}: ${ms}ms;`);
    }
  });

  it("mirrors every easing as an --ease-* custom property", () => {
    for (const [name, curve] of Object.entries(EASE)) {
      expect(theme).toContain(`--ease-${name}: ${bezier(curve)};`);
    }
  });

  it("keeps the Tailwind default duration equal to DUR.state", () => {
    expect(theme).toContain(`--default-transition-duration: ${DUR.state}ms;`);
  });

  it("derives the layer exit from the enter with the glass asymmetry ratio", () => {
    expect(T.layerOut.duration).toBeCloseTo(T.layerIn.duration * EXIT_RATIO, 5);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- motion`
Expected: FAIL — `Failed to resolve import "./motion"`.

- [ ] **Step 3: Write the token module**

Create `WebUI/packages/ui/src/motion.ts`:

```ts
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
} as const satisfies Record<string, Transition>;
```

- [ ] **Step 4: Mirror the tokens in the theme**

In `WebUI/packages/ui/src/theme.css`, sostituire le due righe esistenti nel blocco `@theme`

```css
  --ease-snap: cubic-bezier(0.2, 0.9, 0.3, 1);
  --default-transition-duration: 120ms;
```

con:

```css
  /* Rispecchiano packages/ui/src/motion.ts, che è la sorgente unica: motion.test.ts
     fallisce se le due copie divergono. */
  --dur-press: 70ms;
  --dur-state: 120ms;
  --dur-layer: 200ms;
  --dur-scene: 320ms;
  --ease-snap: cubic-bezier(0.2, 0.9, 0.3, 1);
  --ease-glass: cubic-bezier(0.16, 1, 0.3, 1);
  --ease-settle: cubic-bezier(0.34, 1.26, 0.64, 1);
  --ease-exit: cubic-bezier(0.4, 0, 1, 1);
  --default-transition-duration: 120ms;
```

Essendo dentro `@theme`, il namespace `--ease-*` di Tailwind v4 genera da solo le utility `ease-glass`, `ease-settle`, `ease-exit`. Le durate si usano invece con la sintassi per custom property: `duration-(--dur-press)`.

- [ ] **Step 5: Export the tokens from the library**

In `WebUI/packages/ui/src/index.ts`, dopo la riga `export { cn } from "@/lib/utils";`:

```ts
export { DUR, EASE, EXIT_RATIO, bezier, T, type Bezier, type Transition } from "@/motion";
```

- [ ] **Step 6: Bend the tokens per chassis variant**

Le curve appartengono al materiale, come i colori: `glass` si muove diverso da `metal`. In
`WebUI/src/synth/ui/synth.css`, accanto ai blocchi che già ridefiniscono i token per variante:

```css
/* Il motion segue il materiale. `soft`, `deep` e `glow` restano sui default della libreria. */
.sx-chassis[data-variant="glass"] {
  --ease-snap: var(--ease-glass);
  --dur-state: 140ms;
}

.sx-chassis[data-variant="metal"] {
  --dur-layer: 160ms;
  --dur-state: 100ms;
}
```

Una primitiva di `@xerum/ui` non sa in che chassis vive: legge i token e basta. Nessun
componente va toccato per questo passo.

- [ ] **Step 7: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- motion`
Expected: PASS, 4 test.

- [ ] **Step 8: Commit**

```bash
git add WebUI/packages/ui/src/motion.ts WebUI/packages/ui/src/motion.test.ts WebUI/packages/ui/src/theme.css WebUI/packages/ui/src/index.ts WebUI/src/synth/ui/synth.css
git commit -m "Add the motion token vocabulary as a single source"
```

---

### Task 2: Guardrail `check:motion`

Arriva subito dopo i token e prima di ogni uso, così tutte le task successive nascono già sorvegliate.

**Files:**
- Create: `scripts/check-motion.mjs`
- Create: `scripts/check-motion.test.mjs`
- Modify: `WebUI/package.json` (script `check:motion`, e `build` lo invoca)

**Interfaces:**
- Consumes: niente.
- Produces: `findViolations(files: { path: string, text: string }[]): { path: string, line: number, rule: string }[]`, esportata per il test; il CLI legge i file da disco ed esce con codice 1 se l'array non è vuoto.

- [ ] **Step 1: Write the failing test**

Create `scripts/check-motion.test.mjs`:

```js
import test from "node:test";
import assert from "node:assert/strict";
import { findViolations } from "./check-motion.mjs";

const rules = (text, path = "a.tsx") => findViolations([{ path, text }]).map((v) => v.rule);

test("flags an animated backdrop-filter", () => {
  assert.deepEqual(rules("a { transition: backdrop-filter 200ms; }", "a.css"), ["animated-blur"]);
});

test("flags an animated blur radius", () => {
  assert.deepEqual(rules("a { transition: filter 200ms; filter: blur(var(--b)); }\n.b { transition: blur 1ms }", "a.css"), ["animated-blur"]);
});

test("flags layout animations", () => {
  assert.deepEqual(rules('<m.div layoutId="tab" />'), ["layout-animation"]);
});

test("flags the full motion namespace", () => {
  assert.deepEqual(rules('import { motion } from "motion/react";\n<motion.div />'), ["full-motion-namespace"]);
});

test("flags a hand-written duration utility", () => {
  assert.deepEqual(rules('<div className="transition duration-150" />'), ["hardcoded-duration"]);
});

test("accepts the token forms", () => {
  const ok = [
    '<m.div className="transition-opacity duration-(--dur-layer) ease-glass" />',
    "a { transition: opacity var(--dur-state) var(--ease-glass); }",
    '<div className="duration-0" />',
  ].join("\n");
  assert.deepEqual(rules(ok), []);
});

test("reports the offending line number", () => {
  const [v] = findViolations([{ path: "x.tsx", text: '\n\n<m.div layout />' }]);
  assert.equal(v.line, 3);
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `node --test scripts/check-motion.test.mjs`
Expected: FAIL — `Cannot find module './check-motion.mjs'`.

- [ ] **Step 3: Write the checker**

Create `scripts/check-motion.mjs`:

```js
#!/usr/bin/env node
// Rende eseguibili le regole di docs/superpowers/specs/2026-09-20-motion-system-design.md:
// whitelist delle proprietà animabili, divieto di layout animations, durate solo dai token.
import { readFileSync } from "node:fs";
import { readdir } from "node:fs/promises";
import { join, resolve, extname } from "node:path";

const RULES = [
  // Una transizione che nomina backdrop-filter o blur: il blur dei layer è statico.
  { rule: "animated-blur", re: /transition[^;{}]*\b(backdrop-filter|blur)\b/ },
  // layout / layoutId di motion: forzano reflow, ed è la feature che LazyMotion esclude.
  { rule: "layout-animation", re: /\blayout(Id)?\s*[=\s]/ },
  // Il namespace pieno di motion aggira LazyMotion e gonfia il bundle.
  { rule: "full-motion-namespace", re: /\bmotion\.[a-z]/ },
  // Durate scritte a mano: duration-150, duration-[200ms]. duration-0 è ammessa.
  { rule: "hardcoded-duration", re: /\bduration-(\[?\d*[1-9]\d*m?s?\]?)\b/ },
];

/** Le violazioni di un insieme di file già letti. */
export function findViolations(files) {
  const out = [];
  for (const { path, text } of files) {
    text.split("\n").forEach((line, i) => {
      for (const { rule, re } of RULES) {
        if (re.test(line)) out.push({ path, line: i + 1, rule });
      }
    });
  }
  return out;
}

const EXT = new Set([".ts", ".tsx", ".css"]);
// motion.ts è la sorgente delle durate, theme.css la loro copia CSS: sono esenti.
const EXEMPT = new Set(["motion.ts", "theme.css"]);

async function collect(dir, acc = []) {
  for (const e of await readdir(dir, { withFileTypes: true })) {
    if (e.name === "node_modules" || e.name === "dist") continue;
    const path = join(dir, e.name);
    if (e.isDirectory()) await collect(path, acc);
    else if (EXT.has(extname(e.name)) && !EXEMPT.has(e.name)) acc.push({ path, text: readFileSync(path, "utf8") });
  }
  return acc;
}

if (import.meta.filename === process.argv[1]) {
  // Percorsi relativi a questo file (<repo>/scripts), non alla cwd: lo script si invoca
  // sia dalla radice sia da WebUI/.
  const here = import.meta.dirname;
  const roots = [resolve(here, "../WebUI/src"), resolve(here, "../WebUI/packages/ui/src")];
  const files = (await Promise.all(roots.map((r) => collect(r)))).flat();
  const bad = findViolations(files);
  for (const v of bad) console.error(`${v.path}:${v.line}  ${v.rule}`);
  if (bad.length) {
    console.error(`\n${bad.length} motion rule violation(s). See docs/superpowers/specs/2026-09-20-motion-system-design.md`);
    process.exit(1);
  }
  console.log("motion rules: ok");
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `node --test scripts/check-motion.test.mjs`
Expected: PASS, 7 test.

- [ ] **Step 5: Wire it into the build**

In `WebUI/package.json`, aggiungere allo `scripts`:

```json
    "check:motion": "node ../scripts/check-motion.mjs",
```

e cambiare `build` da `"tsc --noEmit && vite build"` a:

```json
    "build": "pnpm check:motion && tsc --noEmit && vite build",
```

- [ ] **Step 6: Run the checker on the current tree**

Run: `pnpm check:motion`
Expected: fallisce elencando le violazioni già presenti — in particolare `PresetOverlay.tsx:39` (`duration-150`) e `Tabs.tsx:312` (`duration-100`). Sono i due punti che le task 6 e 10 sistemano.

Per non bloccare le task intermedie, aggiungere **solo per ora** i due file a una lista di deroga in testa a `check-motion.mjs`:

```js
// Deroghe temporanee: spariscono con le task 6 e 10 del piano di motion.
const GRANDFATHERED = new Set(["Tabs.tsx", "PresetOverlay.tsx"]);
```

usata in `collect` accanto a `EXEMPT`. La task 10 la rimuove.

- [ ] **Step 7: Verify it now passes**

Run: `pnpm check:motion`
Expected: `motion rules: ok`

- [ ] **Step 8: Commit**

```bash
git add scripts/check-motion.mjs scripts/check-motion.test.mjs WebUI/package.json
git commit -m "Add check:motion, the build guardrail for the motion rules"
```

---

### Task 3: Pressione e focus del Button

**Files:**
- Modify: `WebUI/packages/ui/src/components/ui/button.tsx:6`
- Modify: `WebUI/packages/ui/src/components/Button/Button.tsx:19-23`
- Test: `WebUI/packages/ui/src/components/Button/Button.test.tsx`

**Interfaces:**
- Consumes: `--dur-press`, `--ease-snap` (Task 1).
- Produces: niente di nuovo nell'API.

- [ ] **Step 1: Write the failing test**

Aggiungere in `WebUI/packages/ui/src/components/Button/Button.test.tsx`:

```tsx
describe("Button motion", () => {
  it("transitions a named list of properties, never all of them", () => {
    render(<Button>Load</Button>);
    const el = screen.getByRole("button", { name: "Load" });
    expect(el).not.toHaveClass("transition-all");
    expect(el.className).toContain("transition-[transform,box-shadow,background-color,border-color]");
  });

  it("presses instantly and releases on the press duration", () => {
    render(<Button>Load</Button>);
    const el = screen.getByRole("button", { name: "Load" });
    // Il dito è più veloce della molla: la discesa non ha durata, la risalita sì.
    expect(el.className).toContain("duration-(--dur-press)");
    expect(el.className).toContain("active:duration-0");
    expect(el.className).toContain("ease-snap");
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- Button`
Expected: FAIL — `transition-all` presente, `active:duration-0` assente.

- [ ] **Step 3: Replace transition-all in the shadcn primitive**

In `WebUI/packages/ui/src/components/ui/button.tsx`, nella stringa base della `cva` (riga 6), sostituire `transition-all` con la lista esplicita. La lista deve coprire **ogni** proprietà che le varianti del bottone cambiano davvero: `color` perché `ghost` e `outline` hanno `hover:text-foreground`, `opacity` perché la stessa stringa base porta `disabled:opacity-50`. Una lista che ne dimentica una è una regressione travestita da fix — che è esattamente ciò che questa task rimuove.

```
transition-[transform,box-shadow,background-color,border-color,color,opacity] duration-(--dur-press) ease-snap active:duration-0
```

- [ ] **Step 4: Give the focus ring its own entrance**

In `WebUI/packages/ui/src/components/Button/Button.tsx`, dentro la `cn(...)`, dopo `"rounded-control!"`:

```tsx
        // L'anello di focus entra da appena dentro il bordo, sulla durata di stato.
        "focus-visible:ring-offset-0 motion-safe:focus-visible:scale-[1.004] motion-safe:focus-visible:duration-(--dur-state)",
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- Button`
Expected: PASS.

- [ ] **Step 6: Verify nothing else regressed**

Run: `pnpm --filter @xerum/ui test && pnpm check:motion`
Expected: PASS, `motion rules: ok`.

- [ ] **Step 7: Commit**

```bash
git add WebUI/packages/ui/src/components/ui/button.tsx WebUI/packages/ui/src/components/Button/Button.tsx WebUI/packages/ui/src/components/Button/Button.test.tsx
git commit -m "Give the button press its physics: instant down, eased up"
```

---

### Task 4: Toggle — thumb con massa, LED con persistenza

**Files:**
- Modify: `WebUI/packages/ui/src/components/Toggle/Toggle.tsx:33-49`
- Test: `WebUI/packages/ui/src/components/Toggle/Toggle.test.tsx`

**Interfaces:**
- Consumes: `--dur-press`, `--dur-state`, `--ease-settle` (Task 1).
- Produces: niente di nuovo nell'API.

- [ ] **Step 1: Write the failing test**

Aggiungere in `WebUI/packages/ui/src/components/Toggle/Toggle.test.tsx`:

```tsx
describe("Toggle motion", () => {
  it("moves the thumb on the settle curve: it has mass", () => {
    render(<Toggle checked={false} onChange={() => {}} label="Sync" />);
    const cls = screen.getByRole("switch").className;
    expect(cls).toContain("[&_[data-slot=switch-thumb]]:transition-transform");
    expect(cls).toContain("[&_[data-slot=switch-thumb]]:duration-(--dur-state)");
    expect(cls).toContain("[&_[data-slot=switch-thumb]]:ease-settle");
  });

  it("lights the LED faster than it lets it fade: a real LED has persistence", () => {
    const { rerender } = render(<Toggle checked={true} onChange={() => {}} label="Sync" />);
    const led = () => screen.getByTestId("toggle-led");
    expect(led().className).toContain("duration-(--dur-press)");
    rerender(<Toggle checked={false} onChange={() => {}} label="Sync" />);
    expect(led().className).toContain("duration-(--dur-state)");
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- Toggle`
Expected: FAIL — `Unable to find an element by: [data-testid="toggle-led"]`.

- [ ] **Step 3: Implement**

In `WebUI/packages/ui/src/components/Toggle/Toggle.tsx`, aggiungere alla `cn(...)` dello `Switch`, dopo la riga `"[&_[data-slot=switch-thumb]]:shadow-cap",`:

```tsx
          // Il thumb ha massa: è l'unico overshoot autorizzato dell'interfaccia.
          "[&_[data-slot=switch-thumb]]:transition-transform [&_[data-slot=switch-thumb]]:duration-(--dur-state) [&_[data-slot=switch-thumb]]:ease-settle",
```

e sostituire lo `<span aria-hidden>` del LED con:

```tsx
      <span
        aria-hidden
        data-testid="toggle-led"
        className={cn(
          "size-1.5 shrink-0 rounded-full transition-[background-color,box-shadow] ease-snap",
          // Un LED vero si accende subito e si spegne con persistenza: l'opposto del vetro.
          checked ? "bg-(--tone) shadow-[0_0_4px_var(--tone)] duration-(--dur-press)" : "bg-led-off duration-(--dur-state)",
          disabled && "opacity-50",
        )}
      />
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- Toggle`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add WebUI/packages/ui/src/components/Toggle/Toggle.tsx WebUI/packages/ui/src/components/Toggle/Toggle.test.tsx
git commit -m "Give the toggle thumb mass and the LED persistence"
```

---

### Task 5: `measureIndicator` e l'indicatore scorrevole del Segmented

**Files:**
- Create: `WebUI/packages/ui/src/lib/indicator.ts`
- Create: `WebUI/packages/ui/src/lib/indicator.test.ts`
- Modify: `WebUI/packages/ui/src/components/Segmented/Segmented.tsx`
- Test: `WebUI/packages/ui/src/components/Segmented/Segmented.test.tsx`

**Interfaces:**
- Consumes: `--dur-state`, `--ease-glass` (Task 1).
- Produces: `type IndicatorBox = { x: number; width: number }`, `measureIndicator(container: HTMLElement, item: HTMLElement): IndicatorBox`. È il sostituto di `layoutId`, vietato dai vincoli globali.

- [ ] **Step 1: Write the failing test for the measurement**

Create `WebUI/packages/ui/src/lib/indicator.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import { measureIndicator } from "./indicator";

/** Un elemento finto con il solo `getBoundingClientRect` che serve alla misura. */
const at = (left: number, width: number) =>
  ({ getBoundingClientRect: () => ({ left, width }) }) as unknown as HTMLElement;

describe("measureIndicator", () => {
  it("returns the item offset relative to its container", () => {
    expect(measureIndicator(at(100, 300), at(160, 80))).toEqual({ x: 60, width: 80 });
  });

  it("is zeroed when there is no layout, as in jsdom", () => {
    expect(measureIndicator(at(0, 0), at(0, 0))).toEqual({ x: 0, width: 0 });
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- indicator`
Expected: FAIL — `Failed to resolve import "./indicator"`.

- [ ] **Step 3: Write the measurement**

Create `WebUI/packages/ui/src/lib/indicator.ts`:

```ts
/** Posizione e larghezza dell'indicatore, relative al suo contenitore. */
export type IndicatorBox = { x: number; width: number };

/** La misura da cui nasce un `translateX`/`scaleX`: transform puro, nessun reflow.
    È il sostituto di `layoutId`, che il sistema di motion vieta. */
export function measureIndicator(container: HTMLElement, item: HTMLElement): IndicatorBox {
  const c = container.getBoundingClientRect();
  const i = item.getBoundingClientRect();
  return { x: i.left - c.left, width: i.width };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pnpm --filter @xerum/ui test -- indicator`
Expected: PASS, 2 test.

- [ ] **Step 5: Write the failing test for the Segmented indicator**

Aggiungere in `WebUI/packages/ui/src/components/Segmented/Segmented.test.tsx`:

```tsx
describe("Segmented motion", () => {
  const options = [
    { value: "a", label: "A" },
    { value: "b", label: "B" },
  ];

  it("renders one sliding indicator instead of lighting each plate on its own", () => {
    render(<Segmented value="a" onChange={() => {}} options={options} label="Mode" />);
    const ind = screen.getByTestId("segmented-indicator");
    expect(ind.className).toContain("transition-transform");
    expect(ind.className).toContain("duration-(--dur-state)");
    expect(ind.className).toContain("ease-glass");
  });

  it("hides the indicator until there is a layout to measure", () => {
    render(<Segmented value="a" onChange={() => {}} options={options} label="Mode" />);
    // jsdom non fa layout: larghezza 0, quindi l'indicatore non si vede.
    expect(screen.getByTestId("segmented-indicator")).toHaveAttribute("data-measured", "false");
  });
});
```

- [ ] **Step 6: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- Segmented`
Expected: FAIL — `Unable to find an element by: [data-testid="segmented-indicator"]`.

- [ ] **Step 7: Implement the indicator**

In `WebUI/packages/ui/src/components/Segmented/Segmented.tsx`:

```tsx
import { useLayoutEffect, useRef, useState, type KeyboardEvent } from "react";
import { measureIndicator, type IndicatorBox } from "@/lib/indicator";
```

Dentro il componente, accanto a `const buttons = useRef<(HTMLButtonElement | null)[]>([]);`:

```tsx
  const group = useRef<HTMLDivElement>(null);
  const [box, setBox] = useState<IndicatorBox>({ x: 0, width: 0 });
  const active = options.findIndex((o) => o.value === value);

  // La misura dopo il layout, e a ogni cambio di selezione o di larghezza del gruppo.
  useLayoutEffect(() => {
    const container = group.current;
    const item = buttons.current[active];
    if (!container || !item) return;
    const measure = () => setBox(measureIndicator(container, item));
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(container);
    return () => ro.disconnect();
  }, [active, options.length]);
```

Aggiungere `ref={group}` e `relative` al `<div role="radiogroup">`, e come primo figlio:

```tsx
      <span
        aria-hidden
        data-testid="segmented-indicator"
        data-measured={box.width > 0}
        className="pointer-events-none absolute top-0.5 bottom-0.5 left-0 origin-left rounded-control bg-well shadow-well transition-transform duration-(--dur-state) ease-glass data-[measured=false]:opacity-0"
        style={{ width: 1, transform: `translateX(${box.x}px) scaleX(${box.width})` }}
      />
```

Nelle classi delle piastrine, togliere lo sfondo dello stato selezionato (ora lo disegna l'indicatore) e lasciare solo il colore del testo su `checked`.

- [ ] **Step 8: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- Segmented`
Expected: PASS, compresi i test di tastiera e selezione già esistenti.

- [ ] **Step 9: Commit**

```bash
git add WebUI/packages/ui/src/lib/indicator.ts WebUI/packages/ui/src/lib/indicator.test.ts WebUI/packages/ui/src/components/Segmented/Segmented.tsx WebUI/packages/ui/src/components/Segmented/Segmented.test.tsx
git commit -m "Slide one indicator across the segmented control"
```

---

### Task 6: Indicatore dei Tabs con `Tabs.Indicator` di Base UI

Base UI 1.8 espone già la parte `Indicator` con le custom properties `--active-tab-left` e `--active-tab-width` (verificato in `node_modules/@base-ui/react/tabs/indicator/TabsIndicatorCssVars.d.ts`): per i tab non serve misurare a mano.

**Files:**
- Modify: `WebUI/packages/ui/src/components/ui/tabs.tsx` (esporre `TabsIndicator`)
- Modify: `WebUI/packages/ui/src/components/Tabs/Tabs.tsx`
- Test: `WebUI/packages/ui/src/components/Tabs/Tabs.test.tsx`

**Interfaces:**
- Consumes: `--dur-state`, `--ease-glass` (Task 1).
- Produces: `TabsIndicator` riesportato da `@/components/ui/tabs`.

- [ ] **Step 1: Write the failing test**

Aggiungere in `WebUI/packages/ui/src/components/Tabs/Tabs.test.tsx`:

```tsx
describe("Tabs motion", () => {
  const items = [
    { value: "env", label: "ENV" },
    { value: "lfo", label: "LFO" },
  ];

  it("slides a single underline in the bar variant", () => {
    render(<Tabs variant="bar" value="env" onChange={() => {}} items={items} />);
    const ind = screen.getByTestId("tabs-indicator");
    expect(ind.className).toContain("transition-[translate,scale]");
    expect(ind.className).toContain("duration-(--dur-state)");
    expect(ind.className).toContain("ease-glass");
  });

  it("has no indicator in the plate variant, where the active plate is pressed in", () => {
    render(<Tabs variant="plate" value="env" onChange={() => {}} items={items} />);
    expect(screen.queryByTestId("tabs-indicator")).toBeNull();
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- Tabs`
Expected: FAIL — `Unable to find an element by: [data-testid="tabs-indicator"]`.

- [ ] **Step 3: Export the Base UI part**

In `WebUI/packages/ui/src/components/ui/tabs.tsx`, aggiungere il wrapper e includerlo nell'export di riga 79. Usare l'alias di import già in testa al file per `@base-ui/react/tabs` (leggerlo: potrebbe non chiamarsi `TabsPrimitive`):

```tsx
function TabsIndicator({ className, ...props }: TabsPrimitive.Indicator.Props) {
  return <TabsPrimitive.Indicator data-slot="tabs-indicator" className={className} {...props} />
}
```

- [ ] **Step 4: Use it in the bar variant**

In `WebUI/packages/ui/src/components/Tabs/Tabs.tsx`, dentro `<TabsList>` e dopo il `map` dei trigger:

```tsx
        {bar && (
          <TabsIndicator
            data-testid="tabs-indicator"
            // Base UI pubblica la geometria del tab attivo come custom properties: qui
            // diventano una sola riga che scorre, senza misurare niente a mano.
            className="absolute bottom-0 left-0 h-0.5 w-[var(--active-tab-width)] translate-x-[var(--active-tab-left)] bg-(--tone) shadow-[0_0_6px_var(--tone)] transition-[translate,scale] duration-(--dur-state) ease-glass"
          />
        )}
```

e togliere dal trigger `bar` tutto il blocco `data-active:after:*`, che l'indicatore sostituisce. Aggiungere `relative` alla `TabsList` in variante `bar`.

- [ ] **Step 5: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- Tabs`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add WebUI/packages/ui/src/components/ui/tabs.tsx WebUI/packages/ui/src/components/Tabs/Tabs.tsx WebUI/packages/ui/src/components/Tabs/Tabs.test.tsx
git commit -m "Slide the tab underline with the Base UI indicator"
```

---

### Task 7: Knob — nessuna animazione durante il drag, settle sul cappuccio

È la regola che conta più di tutte: uno smoothing qui diventa lag percepito e il knob sembra rotto.

**Files:**
- Modify: `WebUI/packages/ui/src/components/Knob/Knob.tsx` (righe 177-290, le parti SVG)
- Test: `WebUI/packages/ui/src/components/Knob/Knob.test.tsx`

**Interfaces:**
- Consumes: `data-dragging` (già presente a `Knob.tsx:177`), `--dur-state`, `--ease-settle`, `--ease-glass`.
- Produces: niente di nuovo nell'API.

- [ ] **Step 1: Write the failing test**

Aggiungere in `WebUI/packages/ui/src/components/Knob/Knob.test.tsx`:

```tsx
describe("Knob motion", () => {
  it("animates the value arc only when the change did not come from the pointer", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" />);
    const arc = screen.getByTestId("knob-value-arc");
    expect(arc.getAttribute("class")).toContain("transition-[d,stroke-dashoffset]");
    // Durante il drag la transizione sparisce: il valore insegue il dito, non una curva.
    expect(arc.getAttribute("class")).toContain("group-data-[dragging=true]/knob:transition-none");
  });

  it("settles the cap, never the value", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" />);
    expect(screen.getByTestId("knob").querySelector('[data-part="cap"]')?.getAttribute("class")).toContain("ease-settle");
    expect(screen.getByTestId("knob-value-arc").getAttribute("class")).not.toContain("ease-settle");
  });

  it("never transitions the live value dot: it already arrives at 30 Hz", () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" mods={[{ tone: "lfo", depth: 0.2 }]} liveValue={0.4} />);
    expect(screen.getByTestId("knob-live").getAttribute("class") ?? "").not.toContain("transition");
  });

  it("marks the drag on the root so CSS can switch the rule", async () => {
    render(<Knob value={0.3} onChange={() => {}} label="Cutoff" />);
    const knob = screen.getByTestId("knob");
    expect(knob).toHaveAttribute("data-dragging", "false");
    await userEvent.pointer([{ keys: "[MouseLeft>]", target: knob }]);
    expect(knob).toHaveAttribute("data-dragging", "true");
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- Knob`
Expected: FAIL sui primi due test (nessuna classe di transizione sull'arco né sul cappuccio).

- [ ] **Step 3: Implement**

In `WebUI/packages/ui/src/components/Knob/Knob.tsx`:

- all'arco del valore (`data-part="value"`, riga ~222) aggiungere alla `className`:

```
transition-[d,stroke-dashoffset] duration-(--dur-state) ease-glass group-data-[dragging=true]/knob:transition-none
```

- al puntatore (`data-part="pointer"`, riga ~259) aggiungere:

```
transition-transform duration-(--dur-state) ease-glass group-data-[dragging=true]/knob:transition-none
```

- al cappuccio (`data-part="cap"`, riga ~247) aggiungere, e **solo qui**, il rimbalzo:

```
origin-center transition-transform duration-(--dur-press) ease-settle group-data-[dragging=true]/knob:scale-[0.985]
```

- lasciare `data-part="tick"`, `knob-live` e gli anelli dei mod senza alcuna transizione.

Verificare che la riga 177 renda `data-dragging={dragging}` come stringa `"false"` e non come attributo assente: se React lo omette con `false`, usare `data-dragging={String(dragging)}`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- Knob`
Expected: PASS, compresi i test di drag già esistenti.

- [ ] **Step 5: Commit**

```bash
git add WebUI/packages/ui/src/components/Knob/Knob.tsx WebUI/packages/ui/src/components/Knob/Knob.test.tsx
git commit -m "Stop the knob from animating under the finger"
```

---

### Task 8: Fader, Wheel e Keybed

**Files:**
- Modify: `WebUI/packages/ui/src/components/Fader/Fader.tsx`
- Modify: `WebUI/packages/ui/src/components/Wheel/Wheel.tsx`
- Modify: `WebUI/packages/ui/src/components/Keybed/Keybed.tsx`
- Test: i rispettivi `*.test.tsx`

**Interfaces:**
- Consumes: le stesse regole della Task 7.
- Produces: niente di nuovo nell'API.

- [ ] **Step 1: Write the failing tests**

In `WebUI/packages/ui/src/components/Wheel/Wheel.test.tsx`:

```tsx
describe("Wheel motion", () => {
  it("returns to centre on the settle curve: it is the one real spring here", () => {
    render(<Wheel value={0.5} onChange={() => {}} label="Pitch" />);
    const cls = screen.getByTestId("wheel").querySelector('[data-part="fill"]')?.getAttribute("class") ?? "";
    expect(cls).toContain("ease-settle");
    expect(cls).toContain("group-data-[dragging=true]/wheel:transition-none");
  });
});
```

In `WebUI/packages/ui/src/components/Keybed/Keybed.test.tsx`:

```tsx
describe("Keybed motion", () => {
  it("presses a key the same way for a click and for an incoming MIDI note", async () => {
    const { rerender } = render(<Keybed onNoteOn={() => {}} onNoteOff={() => {}} />);
    const key = screen.getAllByTestId("key")[0]!;
    await userEvent.pointer([{ keys: "[MouseLeft>]", target: key }]);
    const pressedByPointer = key.getAttribute("data-pressed");
    await userEvent.pointer([{ keys: "[/MouseLeft]", target: key }]);
    // Stessa nota, questa volta annunciata dal motore: stesso attributo, stessa transizione.
    rerender(<Keybed onNoteOn={() => {}} onNoteOff={() => {}} active={{ [key.getAttribute("data-key")!]: true }} />);
    expect(key.getAttribute("data-pressed")).toBe(pressedByPointer);
  });
});
```

Adattare il nome della prop delle note attive a quello reale di `KeybedProps` (`KeybedNoteMask` è già esportata da `index.ts`): leggerlo in `Keybed.tsx` prima di scrivere il test.

- [ ] **Step 2: Run tests to verify they fail**

Run: `pnpm --filter @xerum/ui test -- Wheel Keybed`
Expected: FAIL.

- [ ] **Step 3: Implement**

- `Wheel.tsx`: aggiungere `group/wheel` e `data-dragging` alla radice se non ci sono; al `data-part="fill"` la classe `transition-transform duration-(--dur-state) ease-settle group-data-[dragging=true]/wheel:transition-none`.
- `Fader.tsx`: stessa coppia del Knob — `ease-glass` sul riempimento, `transition-none` durante il drag, nessuna transizione sul cappuccio mentre si trascina.
- `Keybed.tsx`: un solo attributo `data-pressed` alimentato sia dal puntatore sia dalla maschera di note in arrivo, con `transition-[transform,filter] duration-(--dur-press) ease-snap` e `data-[pressed=true]:translate-y-px data-[pressed=true]:brightness-90`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test`
Expected: PASS, suite intera.

- [ ] **Step 5: Commit**

```bash
git add WebUI/packages/ui/src/components/Fader WebUI/packages/ui/src/components/Wheel WebUI/packages/ui/src/components/Keybed
git commit -m "Give the fader, wheel, and keybed the same motion rules as the knob"
```

---

### Task 9: `motion` in dipendenza, `LazyMotion` alla radice

**Files:**
- Modify: `WebUI/package.json` (dipendenza `motion`)
- Modify: `WebUI/src/App.tsx`
- Test: `WebUI/src/App.test.tsx` (creare)

**Interfaces:**
- Consumes: `T` da `@xerum/ui` (Task 1).
- Produces: il contesto `LazyMotion` per tutta l'app. Da qui in poi i componenti applicativi usano `m.div` e `AnimatePresence` da `motion/react`; `motion.div` è vietato e `check:motion` lo blocca.

- [ ] **Step 1: Add the dependency**

Run: `pnpm add motion --filter xerum-webui`

Verificare che il pacchetto esporti `motion/react` con `LazyMotion`, `domAnimation`, `m`, `AnimatePresence`, `useReducedMotion`:

Run: `node -e "import('motion/react').then(m => console.log(['LazyMotion','domAnimation','m','AnimatePresence','useReducedMotion'].filter(k => !(k in m))))"`
Expected: `[]`

**Non** aggiungere `motion` a `packages/ui`: la libreria resta CSS puro (vincolo globale).

- [ ] **Step 2: Write the failing test**

Create `WebUI/src/App.test.tsx`:

```tsx
import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import App from "./App";

describe("App", () => {
  it("renders the chassis inside the motion provider", () => {
    render(<App />);
    expect(screen.getByTestId("chassis")).toBeInTheDocument();
  });

  it("refuses the full motion namespace: LazyMotion runs in strict mode", async () => {
    const { LazyMotion, domAnimation, motion } = await import("motion/react");
    expect(() =>
      render(
        <LazyMotion features={domAnimation} strict>
          <motion.div />
        </LazyMotion>,
      ),
    ).toThrow();
  });
});
```

- [ ] **Step 3: Run test to verify it fails**

Run: `pnpm test -- App`
Expected: FAIL sul secondo test — senza `strict` il namespace pieno non solleva.

- [ ] **Step 4: Wrap the app**

In `WebUI/src/App.tsx`:

```tsx
import { LazyMotion, domAnimation } from "motion/react";
```

e avvolgere il ritorno:

```tsx
  return (
    // `strict` fa fallire ogni `motion.*`: obbliga `m.*` e tiene il bundle sul solo
    // `domAnimation`, che di suo non contiene le layout animations (vietate dalla spec).
    <LazyMotion features={domAnimation} strict>
      <SynthWindow … />
    </LazyMotion>
  );
```

- [ ] **Step 5: Run tests and the guardrail**

Run: `pnpm test -- App && pnpm check:motion`
Expected: PASS; `motion rules: ok`.

- [ ] **Step 6: Record the bundle cost**

Run: `pnpm build && ls -l dist/assets/*.js`

Annotare la dimensione nel messaggio di commit. La spec si attende un ordine di 15-20 kB gzip in più; se il delta supera i 25 kB gzip, fermarsi e segnalarlo invece di proseguire.

- [ ] **Step 7: Commit**

```bash
git add WebUI/package.json WebUI/pnpm-lock.yaml WebUI/src/App.tsx WebUI/src/App.test.tsx
git commit -m "Mount LazyMotion in strict mode at the app root"
```

---

### Task 10: Uscita del PresetOverlay

**Files:**
- Modify: `WebUI/src/synth/ui/SynthWindow.tsx:176`
- Modify: `WebUI/src/synth/ui/PresetOverlay.tsx:39`
- Modify: `scripts/check-motion.mjs` (togliere la deroga)
- Test: `WebUI/src/synth/ui/SynthWindow.test.tsx`

**Interfaces:**
- Consumes: `T.layerIn`, `T.layerOut` da `@xerum/ui`; `LazyMotion` (Task 9).
- Produces: l'overlay resta montato per la durata dell'uscita dopo `onClose`.

- [ ] **Step 1: Write the failing test**

Aggiungere in `WebUI/src/synth/ui/SynthWindow.test.tsx`:

```tsx
describe("PresetOverlay exit", () => {
  it("keeps the overlay mounted while it leaves, instead of cutting it", async () => {
    render(<SynthWindow variant="glass" initialTab="env" gutter={0} />);
    await userEvent.click(screen.getByRole("button", { name: /preset/i }));
    const dialog = await screen.findByRole("dialog", { name: "Presets" });
    await userEvent.click(within(dialog).getByRole("button", { name: "Close presets" }));
    // Il nodo è ancora lì: sta uscendo. Prima smontava dentro lo stesso tick.
    expect(screen.queryByRole("dialog", { name: "Presets" })).toBeInTheDocument();
    await waitForElementToBeRemoved(() => screen.queryByRole("dialog", { name: "Presets" }));
  });
});
```

Aggiungere agli import del file: `within`, `waitForElementToBeRemoved` da `@testing-library/react`.

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm test -- SynthWindow`
Expected: FAIL — `expect(element).toBeInTheDocument()` su `null`: l'overlay è già sparito.

- [ ] **Step 3: Turn the overlay into a motion node**

In `WebUI/src/synth/ui/PresetOverlay.tsx`, sostituire il `<div role="dialog">` di riga 34-40 con:

```tsx
import { m } from "motion/react";
import { T } from "@xerum/ui";

…

    <m.div
      role="dialog"
      aria-label="Presets"
      onKeyDown={(e) => e.key === "Escape" && onClose()}
      initial={{ opacity: 0, scale: 0.99 }}
      animate={{ opacity: 1, scale: 1, transition: T.layerIn }}
      exit={{ opacity: 0, scale: 0.995, transition: T.layerOut }}
      // Il blur c'è dal primo frame e non si muove: animarne il raggio è la sola cosa
      // che la WebView non può permettersi (vedi la whitelist nella spec).
      className="sx-ovl absolute inset-0 z-20 flex flex-col gap-3 rounded-[14px] bg-background/94 p-3.5 backdrop-blur-sm"
    >
```

Sono sparite le classi `animate-in fade-in zoom-in-[0.99] duration-150` di `tw-animate-css`: le sostituisce `initial`/`animate`. Chiudere con `</m.div>`.

- [ ] **Step 4: Wrap the mount point**

In `WebUI/src/synth/ui/SynthWindow.tsx`, sostituire la riga 176 con:

```tsx
          <AnimatePresence>
            {s.browse && <PresetOverlay current={s.preset} onPick={s.pick} onClose={() => s.setBrowse(false)} />}
          </AnimatePresence>
```

e importare `AnimatePresence` da `motion/react`.

- [ ] **Step 5: Remove the guardrail exemption**

In `scripts/check-motion.mjs` eliminare la costante `GRANDFATHERED` e il suo uso: le due violazioni storiche sono sistemate (questa task per `PresetOverlay.tsx`, la Task 11 per `Tabs.tsx` — se la Task 11 non è ancora fatta, lasciare la sola voce `Tabs.tsx` e toglierla lì).

- [ ] **Step 6: Run tests and the guardrail**

Run: `pnpm test -- SynthWindow PresetOverlay && pnpm check:motion`
Expected: PASS; `motion rules: ok`.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/ui/SynthWindow.tsx WebUI/src/synth/ui/PresetOverlay.tsx WebUI/src/synth/ui/SynthWindow.test.tsx scripts/check-motion.mjs
git commit -m "Let the preset overlay leave instead of vanishing"
```

---

### Task 11: Crossfade del contenuto dei tab

**Files:**
- Modify: `WebUI/src/synth/ui/Tabs.tsx:25-41` (`TabArea`) e riga 312 (lo step dell'arp)
- Test: `WebUI/src/synth/ui/Tabs.test.tsx` (creare se assente)

**Interfaces:**
- Consumes: `T.layerIn`, `T.layerOut`; `AnimatePresence` (Task 9).
- Produces: `TabArea` fa crossfade del figlio a ogni cambio di `tab`.

- [ ] **Step 1: Write the failing test**

```tsx
import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { TabArea } from "./Tabs";

describe("TabArea", () => {
  it("crossfades the page instead of sliding it: 900px of chassis are too many pixels", () => {
    const { rerender } = render(<TabArea tab="env" setTab={() => {}}><p>ENV</p></TabArea>);
    const page = screen.getByTestId("tab-page");
    expect(page.className).toContain("opacity");
    expect(page.className).not.toContain("translate-x");
    rerender(<TabArea tab="lfo" setTab={() => {}}><p>LFO</p></TabArea>);
    expect(screen.getAllByTestId("tab-page").length).toBeGreaterThanOrEqual(1);
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm test -- Tabs`
Expected: FAIL — nessun `tab-page`.

- [ ] **Step 3: Implement the crossfade**

In `WebUI/src/synth/ui/Tabs.tsx`, dentro `TabArea`, sostituire `{children}` con:

```tsx
      <AnimatePresence mode="wait" initial={false}>
        <m.div
          key={tab}
          data-testid="tab-page"
          className="flex min-h-0 flex-1 flex-col"
          initial={{ opacity: 0 }}
          animate={{ opacity: 1, transition: T.layerIn }}
          exit={{ opacity: 0, transition: T.layerOut }}
        >
          {children}
        </m.div>
      </AnimatePresence>
```

`mode="wait"` evita che due pagine coesistano dentro un'area alta 31 unità.

- [ ] **Step 4: Fix the arp step duration**

Alla riga 312, sostituire `duration-100` con `duration-(--dur-press)` — è la violazione che teneva `Tabs.tsx` in deroga nella Task 2.

- [ ] **Step 5: Drop the last exemption**

Togliere `Tabs.tsx` da `GRANDFATHERED` in `scripts/check-motion.mjs`, e con essa la costante se è rimasta vuota.

- [ ] **Step 6: Run tests and the guardrail**

Run: `pnpm test && pnpm check:motion`
Expected: PASS; `motion rules: ok`.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/ui/Tabs.tsx WebUI/src/synth/ui/Tabs.test.tsx scripts/check-motion.mjs
git commit -m "Crossfade the tab pages and retire the motion exemptions"
```

---

### Task 12: ModChip — bersagli che si illuminano, anello che nasce

**Files:**
- Modify: `WebUI/src/synth/ui/ModChip.tsx:16`
- Modify: `WebUI/packages/ui/src/components/Knob/Knob.tsx:182` (la transizione del ring) e `:211` (l'anello mod)
- Test: `WebUI/packages/ui/src/components/Knob/Knob.test.tsx`

**Interfaces:**
- Consumes: `data-drop-target` (già a `Knob.tsx:160`), `--dur-state`, `--ease-glass`.
- Produces: niente di nuovo nell'API.

- [ ] **Step 1: Write the failing test**

```tsx
it("lights the drop target on a transition, not on a cut", () => {
  render(<Knob value={0.3} onChange={() => {}} label="Cutoff" onDropMod={() => {}} />);
  const cls = screen.getByTestId("knob").className;
  expect(cls).toContain("transition-[box-shadow]");
  expect(cls).toContain("duration-(--dur-state)");
  expect(cls).toContain("ease-glass");
});

it("grows the modulation ring from the arc when it appears", () => {
  render(<Knob value={0.3} onChange={() => {}} label="Cutoff" mods={[{ tone: "lfo", depth: 0.2 }]} />);
  const cls = screen.getByTestId("knob-mod-arc").getAttribute("class") ?? "";
  expect(cls).toContain("origin-center");
  expect(cls).toContain("motion-safe:animate-[sx-ring-in_var(--dur-state)_var(--ease-glass)]");
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- Knob`
Expected: FAIL.

- [ ] **Step 3: Implement**

- `Knob.tsx:182`: alla radice aggiungere `transition-[box-shadow] duration-(--dur-state) ease-glass`.
- `Knob.tsx:211` (l'anello mod): aggiungere `origin-center motion-safe:animate-[sx-ring-in_var(--dur-state)_var(--ease-glass)]`.
- In `WebUI/packages/ui/src/index.css`, la keyframe:

```css
/* L'anello di modulazione nasce dall'arco del valore: scala e opacity, niente altro. */
@keyframes sx-ring-in {
  from { opacity: 0; scale: 0.9; }
  to   { opacity: 1; scale: 1; }
}
```

- `ModChip.tsx:16`: sostituire `transition-transform` con `transition-[transform,box-shadow] duration-(--dur-press) ease-snap`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `pnpm --filter @xerum/ui test -- Knob && pnpm test -- ModChip`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add WebUI/packages/ui/src/components/Knob/Knob.tsx WebUI/packages/ui/src/components/Knob/Knob.test.tsx WebUI/packages/ui/src/index.css WebUI/src/synth/ui/ModChip.tsx
git commit -m "Light the modulation drop targets and grow the ring on drop"
```

---

### Task 13: `useMeterConductor` — l'ambient audio senza re-render

**Files:**
- Create: `WebUI/src/synth/ui/conductor.ts`
- Create: `WebUI/src/synth/ui/conductor.test.ts`
- Modify: `WebUI/src/synth/ui/SynthWindow.tsx` (chiamata dell'hook sul `chassisRef`)
- Modify: `WebUI/src/synth/ui/synth.css:227-244` (l'aurora legge `--m-out`)

**Interfaces:**
- Consumes: `meterStore` da `../../juce/meters`, `MeterFrame` da `../../juce/backend`.
- Produces: `writeMeterVars(el: HTMLElement, f: MeterFrame): void` (pura, testabile) e `useMeterConductor(ref: RefObject<HTMLElement | null>): void`. Custom properties scritte: `--m-out`, `--m-env`, `--m-lfo`.

- [ ] **Step 1: Write the failing test**

Create `WebUI/src/synth/ui/conductor.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import { ZERO_METERS } from "../../juce/backend";
import { writeMeterVars } from "./conductor";

describe("writeMeterVars", () => {
  it("writes the three ambient variables as unitless numbers", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, outL: 0.5, outR: 0.7, env: 0.25, lfo: -0.5 });
    expect(el.style.getPropertyValue("--m-out")).toBe("0.7");
    expect(el.style.getPropertyValue("--m-env")).toBe("0.25");
    // L'LFO è bipolare: l'ambient ne usa il modulo, perché serve una luminosità.
    expect(el.style.getPropertyValue("--m-lfo")).toBe("0.5");
  });

  it("clamps into 0..1, so a rogue frame cannot blow the brightness out", () => {
    const el = document.createElement("div");
    writeMeterVars(el, { ...ZERO_METERS, outL: 4, env: Number.NaN, lfo: -9 });
    expect(el.style.getPropertyValue("--m-out")).toBe("1");
    expect(el.style.getPropertyValue("--m-env")).toBe("0");
    expect(el.style.getPropertyValue("--m-lfo")).toBe("1");
  });
});
```

Prima di scrivere il test, leggere i nomi veri dei campi in `WebUI/src/juce/backend.ts` (`MeterFrame`, `ZERO_METERS`) e adattarli: il test deve usare i campi che esistono, non questi se divergono.

- [ ] **Step 2: Run test to verify it fails**

Run: `pnpm test -- conductor`
Expected: FAIL — `Failed to resolve import "./conductor"`.

- [ ] **Step 3: Write the conductor**

Create `WebUI/src/synth/ui/conductor.ts`:

```ts
// L'ambient audio non passa da React: a 30 Hz si scrivono tre custom properties sul chassis
// e il CSS fa il resto. Far ri-renderizzare N componenti trenta volte al secondo dentro una
// WebView che divide la CPU con il motore audio è esattamente ciò che meters.ts evita.
import { useEffect, type RefObject } from "react";
import { meterStore } from "../../juce/meters";
import { useBackend } from "../../juce/provider";
import type { MeterFrame } from "../../juce/backend";

const unit = (v: number) => (Number.isFinite(v) ? Math.min(1, Math.max(0, Math.abs(v))) : 0);

/** Le tre variabili da cui dipende l'ambient. Pura, così è testabile senza rAF. */
export function writeMeterVars(el: HTMLElement, f: MeterFrame) {
  el.style.setProperty("--m-out", String(unit(Math.max(f.outL, f.outR))));
  el.style.setProperty("--m-env", String(unit(f.env)));
  el.style.setProperty("--m-lfo", String(unit(f.lfo)));
}

/** Un solo rAF per tutta la finestra, coalescente: più frame nello stesso vsync ne scrivono uno. */
export function useMeterConductor(ref: RefObject<HTMLElement | null>) {
  const backend = useBackend();
  useEffect(() => {
    const store = meterStore(backend);
    let raf = 0;
    const flush = () => {
      raf = 0;
      const el = ref.current;
      if (el) writeMeterVars(el, store.get());
    };
    const off = store.subscribe(() => {
      if (!raf) raf = requestAnimationFrame(flush);
    });
    return () => {
      if (raf) cancelAnimationFrame(raf);
      off();
    };
  }, [backend, ref]);
}
```

Adattare `useBackend` al nome reale esportato da `WebUI/src/juce/provider.tsx`.

- [ ] **Step 4: Run test to verify it passes**

Run: `pnpm test -- conductor`
Expected: PASS, 2 test.

- [ ] **Step 5: Mount it and let the CSS read it**

In `WebUI/src/synth/ui/SynthWindow.tsx`, dopo `const chassisRef = useRef<HTMLDivElement>(null);`:

```tsx
  useMeterConductor(chassisRef);
```

In `WebUI/src/synth/ui/synth.css`, dare valori di riposo nel blocco `.sx-chassis` (così la UI è corretta anche senza motore):

```css
  --m-out: 0;
  --m-env: 0;
  --m-lfo: 0;
```

e far leggere `--m-out` all'aurora, che oggi ha opacity fissa: `opacity: calc(0.35 + 0.4 * var(--m-out));`. Solo opacity — nessun transform sul chassis, che è 900×680.

- [ ] **Step 6: Verify no extra renders**

Run: `pnpm test && pnpm check:motion`
Expected: PASS; `motion rules: ok`. In particolare i test dei meter esistenti (`meters.test.tsx`) devono restare verdi: il conductor non passa dal ciclo di render.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/ui/conductor.ts WebUI/src/synth/ui/conductor.test.ts WebUI/src/synth/ui/SynthWindow.tsx WebUI/src/synth/ui/synth.css
git commit -m "Drive the ambient glow from the meters without re-rendering"
```

---

### Task 14: Accensione e caricamento di un preset

**Files:**
- Create: `WebUI/src/synth/ui/boot.ts`
- Create: `WebUI/src/synth/ui/boot.test.ts`
- Modify: `WebUI/src/synth/ui/SynthWindow.tsx`
- Modify: `WebUI/src/synth/ui/synth.css` (keyframes)

**Interfaces:**
- Consumes: `DUR` da `@xerum/ui`.
- Produces: `consumeFirstBoot(): boolean` — `true` una sola volta per processo, `false` a ogni chiamata successiva; `resetFirstBoot()` per i test.

- [ ] **Step 1: Write the failing test**

Create `WebUI/src/synth/ui/boot.test.ts`:

```ts
import { beforeEach, describe, expect, it } from "vitest";
import { consumeFirstBoot, resetFirstBoot } from "./boot";

describe("consumeFirstBoot", () => {
  beforeEach(() => resetFirstBoot());

  it("is true once and false afterwards", () => {
    expect(consumeFirstBoot()).toBe(true);
    expect(consumeFirstBoot()).toBe(false);
    expect(consumeFirstBoot()).toBe(false);
  });
});
```

E in `WebUI/src/synth/ui/SynthWindow.test.tsx`:

```tsx
it("plays the full opening once per process, then only fades", () => {
  resetFirstBoot();
  const { unmount } = render(<SynthWindow variant="glass" initialTab="env" gutter={0} />);
  expect(screen.getByTestId("chassis")).toHaveAttribute("data-boot", "first");
  unmount();
  // Un plugin ricrea l'editor a ogni apertura della finestra: la decima volta
  // una sequenza da 600 ms è una tassa, non un effetto.
  render(<SynthWindow variant="glass" initialTab="env" gutter={0} />);
  expect(screen.getByTestId("chassis")).toHaveAttribute("data-boot", "again");
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pnpm test -- boot SynthWindow`
Expected: FAIL — `Failed to resolve import "./boot"`.

- [ ] **Step 3: Implement the flag**

Create `WebUI/src/synth/ui/boot.ts`:

```ts
// L'host ricrea l'editor a ogni apertura della finestra: la sequenza piena va suonata
// una volta sola nella vita del processo, non a ogni riapertura.
let done = false;

/** `true` alla prima chiamata del processo, `false` sempre dopo. */
export function consumeFirstBoot(): boolean {
  if (done) return false;
  done = true;
  return true;
}

/** Solo per i test. */
export function resetFirstBoot() {
  done = false;
}
```

- [ ] **Step 4: Use it in the window**

In `WebUI/src/synth/ui/SynthWindow.tsx`:

```tsx
  const [boot] = useState(() => (consumeFirstBoot() ? "first" : "again"));
```

e sul div del chassis: `data-boot={boot}`.

In `WebUI/src/synth/ui/synth.css`:

```css
/* Accensione: il chassis arriva da appena dietro, i pannelli in tre gruppi, poi i LED. */
@keyframes sx-boot {
  from { opacity: 0; scale: 0.985; }
  to   { opacity: 1; scale: 1; }
}

.sx-chassis[data-boot="first"] {
  animation: sx-boot var(--dur-scene) var(--ease-glass) both;
}

.sx-chassis[data-boot="again"] {
  animation: sx-boot var(--dur-layer) var(--ease-glass) both;
  animation-name: sx-fade-in; /* solo opacity: nessuna scala alla riapertura */
}

@keyframes sx-fade-in {
  from { opacity: 0; }
  to   { opacity: 1; }
}

@media (prefers-reduced-motion: reduce) {
  .sx-chassis[data-boot] { animation: none; }
}
```

- [ ] **Step 5: Add the preset-load wipe**

Un preset cambia ~40 parametri: un solo wipe di luce sul chassis, non 40 knob animati. In `synth.css`:

```css
/* Un preset caricato è un colpo di luce che attraversa il chassis, una volta. */
@keyframes sx-wipe {
  from { opacity: 0.45; }
  to   { opacity: 0; }
}

.sx-chassis::after {
  content: "";
  position: absolute;
  inset: 0;
  border-radius: inherit;
  pointer-events: none;
  opacity: 0;
  background: linear-gradient(100deg, transparent 30%, var(--color-osc) 50%, transparent 70%);
  mix-blend-mode: screen;
}

.sx-chassis[data-wipe]::after {
  animation: sx-wipe var(--dur-scene) var(--ease-exit);
}
```

In `SynthWindow.tsx`, un `data-wipe` che porta il nome del preset caricato (cambiando chiave, l'animazione riparte):

```tsx
        data-wipe={s.preset.name}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `pnpm test -- boot SynthWindow && pnpm check:motion`
Expected: PASS; `motion rules: ok`.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/ui/boot.ts WebUI/src/synth/ui/boot.test.ts WebUI/src/synth/ui/SynthWindow.tsx WebUI/src/synth/ui/synth.css
git commit -m "Open the window once per process and wipe light on preset load"
```

---

### Task 15: Reduced motion, di fatto e non solo a parole

**Files:**
- Modify: `WebUI/packages/ui/src/test/setup.ts` (stub di `matchMedia`)
- Create: `WebUI/packages/ui/src/motion.reduced.test.tsx`
- Modify: `WebUI/packages/ui/src/index.css` (regola globale)

**Interfaces:**
- Consumes: tutto ciò che le task 3-14 hanno prodotto.
- Produces: `setReducedMotion(on: boolean)` esportata da `test/setup.ts` per i test.

- [ ] **Step 1: Add the matchMedia stub**

In `WebUI/packages/ui/src/test/setup.ts`, in fondo:

```ts
// jsdom non implementa matchMedia: senza, ogni componente che chiede prefers-reduced-motion
// solleva invece di rispondere.
let reduced = false;

export function setReducedMotion(on: boolean) {
  reduced = on;
}

globalThis.matchMedia ??= ((query: string) =>
  ({
    matches: reduced && query.includes("prefers-reduced-motion"),
    media: query,
    onchange: null,
    addEventListener: () => {},
    removeEventListener: () => {},
    addListener: () => {},
    removeListener: () => {},
    dispatchEvent: () => false,
  }) as unknown as MediaQueryList) as typeof matchMedia;

afterEach(() => setReducedMotion(false));
```

- [ ] **Step 2: Write the failing test**

Create `WebUI/packages/ui/src/motion.reduced.test.tsx`:

```tsx
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";

const css = readFileSync(resolve(import.meta.dirname, "index.css"), "utf8");

describe("reduced motion", () => {
  it("kills movement globally while keeping opacity legible", () => {
    // Una regola sola, non una per componente: ogni transizione nuova è coperta dal giorno uno.
    expect(css).toContain("@media (prefers-reduced-motion: reduce)");
    expect(css).toMatch(/prefers-reduced-motion: reduce\)\s*\{[^}]*animation-duration:\s*0\.01ms/s);
    expect(css).toMatch(/prefers-reduced-motion: reduce\)\s*\{[^}]*transition-property:\s*opacity/s);
  });
});
```

- [ ] **Step 3: Run test to verify it fails**

Run: `pnpm --filter @xerum/ui test -- motion.reduced`
Expected: FAIL — la regola globale non c'è.

- [ ] **Step 4: Write the global rule**

In fondo a `WebUI/packages/ui/src/index.css`:

```css
/* A movimento ridotto non sparisce nessuna funzione: si smette di spostare pixel.
   L'opacity resta, perché è ciò che rende leggibile un cambio di stato. */
@media (prefers-reduced-motion: reduce) {
  *,
  *::before,
  *::after {
    animation-duration: 0.01ms !important;
    animation-iteration-count: 1 !important;
    transition-property: opacity !important;
    transition-duration: var(--dur-press) !important;
    scroll-behavior: auto !important;
  }
}
```

- [ ] **Step 5: Run the whole suite**

Run: `pnpm test && pnpm --filter @xerum/ui test && pnpm check:motion && node --test ../scripts/*.test.mjs`
Expected: tutto verde.

- [ ] **Step 6: Commit**

```bash
git add WebUI/packages/ui/src/test/setup.ts WebUI/packages/ui/src/motion.reduced.test.tsx WebUI/packages/ui/src/index.css
git commit -m "Honour reduced motion with one rule instead of many"
```

---

### Task 16: La story che permette di giudicare a occhio

**Files:**
- Create: `WebUI/packages/ui/src/stories/Motion.stories.tsx`

**Interfaces:**
- Consumes: tutte le primitive toccate dalle task 3-12.
- Produces: una story per variante di chassis.

- [ ] **Step 1: Write the story**

Create `WebUI/packages/ui/src/stories/Motion.stories.tsx`:

```tsx
import type { Meta, StoryObj } from "@storybook/react-vite";
import { useState } from "react";
import { Button } from "@/components/Button/Button";
import { Knob } from "@/components/Knob/Knob";
import { Segmented } from "@/components/Segmented/Segmented";
import { Tabs } from "@/components/Tabs/Tabs";
import { Toggle } from "@/components/Toggle/Toggle";

const VARIANTS = ["soft", "deep", "glow", "glass", "metal"] as const;

/** Tutti gli stati animati in una schermata: è qui che il motion si giudica a occhio,
    perché jsdom non anima e un test può solo verificare i contratti. */
function Bench() {
  const [seg, setSeg] = useState("a");
  const [tab, setTab] = useState("env");
  const [on, setOn] = useState(false);
  const [v, setV] = useState(0.3);
  return (
    <div className="flex flex-col gap-4 p-4">
      <div className="flex items-center gap-3">
        <Button tone="osc">Load</Button>
        <Button variant="secondary">Save</Button>
        <Button variant="outline">Init</Button>
        <Toggle checked={on} onChange={setOn} label="Sync" tone="lfo" />
      </div>
      <Segmented
        label="Mode"
        value={seg}
        onChange={setSeg}
        options={[
          { value: "a", label: "TRI" },
          { value: "b", label: "SAW" },
          { value: "c", label: "SQR" },
        ]}
        tone="osc"
      />
      <Tabs
        variant="bar"
        value={tab}
        onChange={setTab}
        items={[
          { value: "env", label: "ENV", tone: "env" },
          { value: "lfo", label: "LFO", tone: "lfo" },
          { value: "fx", label: "FX", tone: "fx" },
        ]}
      />
      <div className="flex gap-4">
        <Knob value={v} onChange={setV} label="Cutoff" tone="filter" />
        <Knob value={v} onChange={setV} label="Depth" tone="lfo" mods={[{ tone: "lfo", depth: 0.25 }]} liveValue={v} />
      </div>
    </div>
  );
}

const meta = {
  title: "Motion/Bench",
  component: Bench,
} satisfies Meta<typeof Bench>;

export default meta;
type Story = StoryObj<typeof meta>;

/** Una story per variante: le curve cambiano con lo chassis, quindi vanno viste tutte. */
export const AllVariants: Story = {
  render: () => (
    <div className="grid grid-cols-2 gap-4">
      {VARIANTS.map((variant) => (
        <div key={variant} className="sx-chassis" data-variant={variant} style={{ width: 420, height: "auto" }}>
          <p className="px-4 pt-3 font-mono text-2xs tracking-widest text-text-dim uppercase">{variant}</p>
          <Bench />
        </div>
      ))}
    </div>
  ),
};
```

`synth.css` vive nell'app e non in libreria: se le varianti non si vedono in Storybook, importarlo nella story con `import "../../../../src/synth/ui/synth.css";` oppure ridurre la story alla sola variante di default e annotarlo. Scegliere la prima se l'import risolve, la seconda altrimenti.

- [ ] **Step 2: Look at it**

Run: `pnpm --filter @xerum/ui storybook`

Guardare, per ciascuna variante: il tasto che affonda di colpo e risale, il LED che si accende prima di quanto si spenga, l'indicatore del Segmented che scorre, la sottolineatura dei tab che lo segue, il knob che non anima sotto il dito e il cappuccio che si assesta al rilascio.

- [ ] **Step 3: Commit**

```bash
git add WebUI/packages/ui/src/stories/Motion.stories.tsx
git commit -m "Add the motion bench story, one per chassis variant"
```

---

### Task 17: Gate di merge nel DAW

Nessun test automatico sostituisce questa verifica: jsdom non anima e Storybook non gira dentro l'host.

**Files:**
- Modify: `docs/architecture.md` (sezione sulla WebUI: una voce sul sistema di motion)

**Interfaces:**
- Consumes: tutto il piano.
- Produces: la riga di documentazione che dice dove vivono i token.

- [ ] **Step 1: Build everything**

Run: `pnpm ui:build && pnpm build`
Expected: successo, e il delta di bundle registrato nella Task 9 confermato.

- [ ] **Step 2: Build and launch the Release Standalone**

I comandi CMake e l'avvio dell'app **li esegue l'utente**: fornirglieli e attendere l'esito, non lanciarli.

Da proporre:

```bash
cmake --build build/macos-release --config Release --target Xerum_Standalone
open build/macos-release/Xerum_artefacts/Release/Standalone/Xerum.app
```

- [ ] **Step 3: Check the four things that only the host can show**

Da verificare a mano, con un DAW aperto e l'audio in riproduzione:

1. Il knob trascinato non ha ritardo rispetto al puntatore.
2. L'apertura e la chiusura del preset overlay non fanno cadere frame mentre il synth suona.
3. L'ambient segue l'audio senza che la UI scatti (è il punto dove `--m-out` costa, se costa).
4. Riaprendo la finestra dell'editor la sequenza di accensione non si ripete.

Se uno dei quattro fallisce, fermarsi e riportarlo: è il gate, non una formalità.

- [ ] **Step 4: Document where the tokens live**

Aggiungere in `docs/architecture.md`, nella sezione della WebUI:

```markdown
Il vocabolario di motion (durate, curve, asimmetrie) sta in `WebUI/packages/ui/src/motion.ts`,
rispecchiato come custom properties in `theme.css` e verificato da `motion.test.ts`. Le regole
che lo governano — whitelist delle proprietà animabili, divieto di layout animations, nessuna
durata scritta a mano — sono applicate da `scripts/check-motion.mjs`, che il build invoca.
Il progetto di riferimento è `docs/superpowers/specs/2026-09-20-motion-system-design.md`.
```

- [ ] **Step 5: Commit**

```bash
git add docs/architecture.md
git commit -m "Document the motion system in the architecture notes"
```

---

## Note per chi esegue

- **Se un test passa senza che il codice sia stato scritto**, il test è sbagliato: le asserzioni su `className` sono fragili per costruzione, e un `toContain` su una stringa vuota non fallisce mai. Verificare sempre il rosso prima del verde.
- **Vitest ha `css: false`**: nessun test può leggere uno stile calcolato. Tutto ciò che si verifica sono attributi, classi e montaggio. È una scelta della spec, non una mancanza.
- **`pnpm --filter @xerum/ui test -- <nome>`** filtra per nome di file; dalla radice `WebUI/`, `pnpm test -- <nome>` fa lo stesso per l'app.
- **Se una task rivela che un gancio non esiste** (un `data-part`, una prop, un campo di `MeterFrame`), leggere il file prima di inventarne uno: il piano cita le righe, ma il codice è la verità.
