# @xerum/ui Component Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Creare il package `@xerum/ui` (React 19 + Tailwind v4 + shadcn base-nova) con 10 componenti synth verificati da test e Storybook, consumato dalla app shell in `WebUI/` e pronto per `/design-sync`.

**Architecture:** pnpm workspace con root `WebUI/` (app shell) e `WebUI/packages/ui` (libreria). La libreria compila in lib mode Vite → `dist/index.js` + `dist/index.d.ts` + `dist/ui.css` (token, utility usate, font inline). I componenti pubblici vivono in `src/components/<Name>/` e wrappano le primitive shadcn copiate in `src/components/ui/`; il colore di sezione viaggia come variabile CSS `--tone`. Knob e Fader condividono l'hook `useDragValue`.

**Tech Stack:** pnpm 11, Vite 6, TypeScript 5.7 strict, React 19, Tailwind 4.3 (`@tailwindcss/vite`), shadcn CLI 4 con Base UI (`@base-ui/react` 1.8), CVA, `cn`, lucide-react, Vitest 5 + jsdom + Testing Library, Storybook 10 (`@storybook/react-vite`), vite-plugin-dts 5.

**Spec:** `docs/superpowers/specs/2026-09-17-ui-library-design.md`

## Global Constraints

- Package manager: **pnpm** (lockfile unico `WebUI/pnpm-lock.yaml`; `WebUI/package-lock.json` va rimosso). Tutti i comandi si lanciano da `WebUI/`.
- Node 24 (installato: v24.14.0). Nessun `.nvmrc` richiesto.
- Vite resta **6.x** in entrambi i package (`@vitejs/plugin-react` **4.7.x**, non 6.x che richiede Vite 8).
- Valori dei controlli sempre **normalizzati `0..1`**.
- **Solo dark**: valori su `:root`, nessun blocco `.dark`.
- **Nessun colore hardcoded** in `src/components`, `src/hooks`, `src/lib` (esclusi `*.stories.tsx`): `pnpm --filter @xerum/ui check:colors` deve passare.
- File in `src/components/ui/` = output del CLI shadcn. Modifiche locali ammesse solo con commento `// xerum: <motivo>` in testa al file.
- Ogni componente pubblico: `<Name>.tsx` + `<Name>.stories.tsx`; `<Name>.test.tsx` se ha logica. Export named dal barrel `src/index.ts`.
- Tutti i test passano prima di ogni commit: `pnpm --filter @xerum/ui test`.
- Prop `tone?: Tone` con `Tone = "osc" | "filter" | "env" | "lfo" | "fx" | "master"`.
- Regole shadcn: `gap-*` non `space-*`, `size-*` per dimensioni uguali, `cn()` per classi condizionali, icone `lucide-react` con `data-icon` dentro `Button`.
- Se un test `toBeDisabled()` fallisce perché la primitive Base UI espone `aria-disabled` invece dell'attributo `disabled`, sostituire con `toHaveAttribute("aria-disabled", "true")`.
- Commit message: prima riga imperativa, corpo opzionale, chiusura con `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.
- Deviazioni dalla spec decise dai probe di fattibilità (già verificate, non rimetterle in discussione):
  - **Fader è custom** (track + `useDragValue`), non wrapper di Base UI Slider: Base UI usa Shift = passo grande, la spec vuole Shift = passo fine. Nessun `slider` da shadcn.
  - `useDragValue` espone `ref` (listener `wheel` nativo non-passive, perché React registra `wheel` come passive e `preventDefault` non avrebbe effetto) e gestisce anche `onPointerMove/Up/Cancel`. Opzione `axis: "y" | "x"` per il Fader orizzontale.
  - I token estensione stanno in `@theme static` (Tailwind v4 omette dal CSS le variabili non usate; design-sync deve vederle tutte).
  - I font woff2 stanno in `src/fonts/` e vengono **inlineati base64** in `dist/ui.css` (Vite lib mode inlinea sempre gli asset). Nessuna cartella `dist/fonts/`.
  - `pnpm-workspace.yaml` deve avere `onlyBuiltDependencies: [esbuild]`, altrimenti `shadcn add` fallisce con `ERR_PNPM_IGNORED_BUILDS`.

---

## File Structure

```
WebUI/
  pnpm-workspace.yaml                       (nuovo) workspace + onlyBuiltDependencies
  package.json                              (modifica) packageManager, dep @xerum/ui, script
  package-lock.json                         (rimosso)
  pnpm-lock.yaml                            (generato)
  vite.config.ts                            (modifica) plugin tailwind
  src/styles.css                            (modifica) Tailwind + import tema libreria
  src/App.tsx                               (modifica, Task 13) demo con componenti reali
  packages/ui/
    package.json                            manifest libreria
    components.json                         config shadcn (base-nova)
    tsconfig.json
    vite.config.ts                          lib mode + tailwind + dts
    vitest.config.ts
    .storybook/main.ts
    .storybook/preview.ts
    src/index.ts                            barrel + import CSS
    src/index.css                           import tailwind/shadcn/tema/font + layer base
    src/theme.css                           token (@theme inline + @theme static + :root)
    src/fonts/*.woff2                       IBM Plex Sans 400/500/600, Mono 400
    src/lib/utils.ts                        cn
    src/lib/tone.ts                         Tone, TONES, toneStyle
    src/lib/arc.ts                          knobAngles, arcPath (SVG)
    src/hooks/useDragValue.ts
    src/test/setup.ts                       jest-dom + stub setPointerCapture/ResizeObserver
    src/components/ui/*.tsx                 shadcn: button switch select tabs card badge tooltip separator label
    src/components/Knob/Knob.tsx            + stories + test
    src/components/Fader/Fader.tsx          + stories + test
    src/components/Button/Button.tsx        + stories + test
    src/components/Toggle/Toggle.tsx        + stories + test
    src/components/Select/Select.tsx        + stories + test
    src/components/Tabs/Tabs.tsx            + stories + test
    src/components/ValueReadout/ValueReadout.tsx  + stories
    src/components/Panel/Panel.tsx          + stories + test
    src/components/SectionHeader/SectionHeader.tsx + stories
    src/components/WavetableDisplay/WavetableDisplay.tsx + stories + test
    src/stories/Tokens.stories.tsx          palette e tipografia
```

---

### Task 1: Workspace pnpm, package `@xerum/ui`, shadcn, tema, build libreria

**Files:**
- Create: `WebUI/pnpm-workspace.yaml`
- Modify: `WebUI/package.json`
- Delete: `WebUI/package-lock.json`
- Create: `WebUI/packages/ui/package.json`, `components.json`, `tsconfig.json`, `vite.config.ts`
- Create: `WebUI/packages/ui/src/index.ts`, `src/index.css`, `src/theme.css`, `src/lib/utils.ts`, `src/lib/tone.ts`
- Create: `WebUI/packages/ui/src/fonts/*.woff2` (copiati da `@fontsource`)
- Create (via CLI): `WebUI/packages/ui/src/components/ui/{button,switch,select,tabs,card,badge,tooltip,separator,label}.tsx`

**Interfaces:**
- Produces: `Tone`, `TONES`, `toneStyle(tone?: Tone): CSSProperties | undefined` da `@/lib/tone`; `cn` da `@/lib/utils`; token CSS `--color-surface-0..3`, `--color-line-strong`, `--color-text-dim`, `--color-warning`, `--color-osc|filter|env|lfo|fx|master`, `--text-2xs`, `--spacing-knob-sm|md|lg`, `--spacing-fader`, `--ease-snap`; variabile runtime `--tone` (default `var(--primary)`).

- [ ] **Step 1: Workspace e root package**

Crea `WebUI/pnpm-workspace.yaml`:

```yaml
packages:
  - "packages/*"

onlyBuiltDependencies:
  - esbuild
```

Sostituisci `WebUI/package.json` con:

```json
{
  "name": "serum-style-synth-webui",
  "private": true,
  "version": "0.1.0",
  "type": "module",
  "packageManager": "pnpm@11.15.1",
  "scripts": {
    "dev": "vite",
    "build": "tsc --noEmit && vite build",
    "preview": "vite preview",
    "ui:build": "pnpm --filter @xerum/ui build",
    "ui:test": "pnpm --filter @xerum/ui test",
    "ui:storybook": "pnpm --filter @xerum/ui storybook"
  },
  "dependencies": {
    "@xerum/ui": "workspace:*",
    "react": "^19.0.0",
    "react-dom": "^19.0.0"
  },
  "devDependencies": {
    "@tailwindcss/vite": "^4.3.3",
    "@types/react": "^19.0.0",
    "@types/react-dom": "^19.0.0",
    "@vitejs/plugin-react": "^4.7.0",
    "tailwindcss": "^4.3.3",
    "typescript": "~5.7.2",
    "vite": "^6.0.0"
  }
}
```

Rimuovi il lockfile npm: `rm WebUI/package-lock.json`.

- [ ] **Step 2: Manifest del package libreria**

Crea `WebUI/packages/ui/package.json`:

```json
{
  "name": "@xerum/ui",
  "version": "0.1.0",
  "private": true,
  "type": "module",
  "sideEffects": ["**/*.css"],
  "main": "./dist/index.js",
  "module": "./dist/index.js",
  "types": "./dist/index.d.ts",
  "exports": {
    ".": { "types": "./dist/index.d.ts", "import": "./dist/index.js" },
    "./ui.css": "./dist/ui.css",
    "./theme.css": "./src/theme.css"
  },
  "files": ["dist", "src/theme.css"],
  "scripts": {
    "build": "tsc --noEmit && vite build",
    "typecheck": "tsc --noEmit",
    "test": "vitest run",
    "test:watch": "vitest",
    "storybook": "storybook dev -p 6006",
    "build-storybook": "storybook build",
    "check:colors": "! grep -rnE '#[0-9a-fA-F]{3,8}\\b|rgba?\\(' src/components src/hooks src/lib --include=*.ts --include=*.tsx --exclude=*.stories.tsx"
  },
  "peerDependencies": {
    "react": "^19.0.0",
    "react-dom": "^19.0.0"
  },
  "dependencies": {
    "@base-ui/react": "^1.8.0",
    "class-variance-authority": "^0.7.1",
    "cn": "^0.3.0",
    "lucide-react": "^1.46.0",
    "shadcn": "^4.21.0",
    "tw-animate-css": "^1.4.0"
  },
  "devDependencies": {
    "@fontsource/ibm-plex-mono": "^5.3.0",
    "@fontsource/ibm-plex-sans": "^5.3.0",
    "@storybook/react-vite": "^10.6.0",
    "@tailwindcss/vite": "^4.3.3",
    "@testing-library/jest-dom": "^7.0.1",
    "@testing-library/react": "^16.3.3",
    "@testing-library/user-event": "^14.6.7",
    "@types/node": "^24.0.0",
    "@types/react": "^19.0.0",
    "@types/react-dom": "^19.0.0",
    "@vitejs/plugin-react": "^4.7.0",
    "jsdom": "^30.1.0",
    "react": "^19.0.0",
    "react-dom": "^19.0.0",
    "storybook": "^10.6.0",
    "tailwindcss": "^4.3.3",
    "typescript": "~5.7.2",
    "vite": "^6.0.0",
    "vite-plugin-dts": "^5.1.0",
    "vitest": "^5.0.1"
  }
}
```

- [ ] **Step 3: tsconfig, vite.config, components.json**

`WebUI/packages/ui/tsconfig.json`:

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "lib": ["ES2022", "DOM", "DOM.Iterable"],
    "module": "ESNext",
    "moduleResolution": "bundler",
    "jsx": "react-jsx",
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true,
    "isolatedModules": true,
    "moduleDetection": "force",
    "skipLibCheck": true,
    "noEmit": true,
    "declaration": true,
    "types": ["vitest/globals"],
    "baseUrl": ".",
    "paths": { "@/*": ["./src/*"] }
  },
  "include": ["src", ".storybook", "vite.config.ts", "vitest.config.ts"]
}
```

`WebUI/packages/ui/vite.config.ts`:

```ts
import { resolve } from "node:path";
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import dts from "vite-plugin-dts";

export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
    dts({
      tsconfigPath: "./tsconfig.json",
      rollupTypes: false,
      exclude: ["**/*.test.*", "**/*.stories.*", "src/test/**", ".storybook/**"],
    }),
  ],
  resolve: { alias: { "@": resolve(__dirname, "src") } },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    sourcemap: true,
    lib: {
      entry: resolve(__dirname, "src/index.ts"),
      formats: ["es"],
      fileName: "index",
      cssFileName: "ui",
    },
    rollupOptions: {
      external: ["react", "react-dom", "react/jsx-runtime"],
    },
  },
});
```

`WebUI/packages/ui/components.json`:

```json
{
  "$schema": "https://ui.shadcn.com/schema.json",
  "style": "base-nova",
  "rsc": false,
  "tsx": true,
  "tailwind": {
    "config": "",
    "css": "src/index.css",
    "baseColor": "neutral",
    "cssVariables": true,
    "prefix": ""
  },
  "iconLibrary": "lucide",
  "rtl": false,
  "aliases": {
    "components": "@/components",
    "utils": "@/lib/utils",
    "ui": "@/components/ui",
    "lib": "@/lib",
    "hooks": "@/hooks"
  },
  "menuColor": "default",
  "menuAccent": "subtle",
  "registries": {}
}
```

- [ ] **Step 4: Tema, CSS, lib helpers**

`WebUI/packages/ui/src/theme.css`:

```css
/* Token @xerum/ui — solo dark. Vocabolario semantico shadcn + estensioni synth. */

@theme inline {
  --font-sans: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
  --font-mono: "IBM Plex Mono", ui-monospace, monospace;
  --font-heading: var(--font-sans);

  --color-background: var(--background);
  --color-foreground: var(--foreground);
  --color-card: var(--card);
  --color-card-foreground: var(--card-foreground);
  --color-popover: var(--popover);
  --color-popover-foreground: var(--popover-foreground);
  --color-primary: var(--primary);
  --color-primary-foreground: var(--primary-foreground);
  --color-secondary: var(--secondary);
  --color-secondary-foreground: var(--secondary-foreground);
  --color-muted: var(--muted);
  --color-muted-foreground: var(--muted-foreground);
  --color-accent: var(--accent);
  --color-accent-foreground: var(--accent-foreground);
  --color-destructive: var(--destructive);
  --color-border: var(--border);
  --color-input: var(--input);
  --color-ring: var(--ring);

  --radius-sm: calc(var(--radius) * 0.6);
  --radius-md: calc(var(--radius) * 0.8);
  --radius-lg: var(--radius);
  --radius-xl: calc(var(--radius) * 1.4);
  --radius-2xl: calc(var(--radius) * 1.8);
  --radius-3xl: calc(var(--radius) * 2.2);
  --radius-4xl: calc(var(--radius) * 2.6);
}

/* Estensioni xerum: static = sempre emesse nel CSS, anche se non usate (design-sync le legge). */
@theme static {
  --color-surface-0: #12151d;
  --color-surface-1: #171a24;
  --color-surface-2: #1e2230;
  --color-surface-3: #262b3a;
  --color-line-strong: #3a4258;
  --color-text-dim: #5f6880;
  --color-warning: #ffc857;

  --color-osc: #6ee7c5;
  --color-filter: #7aa2ff;
  --color-env: #ffb86b;
  --color-lfo: #d68cff;
  --color-fx: #ff8fb1;
  --color-master: #e9ecf5;

  --text-2xs: 0.625rem;
  --text-2xs--line-height: 0.875rem;

  --spacing-knob-sm: 2rem;
  --spacing-knob-md: 3rem;
  --spacing-knob-lg: 4rem;
  --spacing-fader: 8rem;

  --ease-snap: cubic-bezier(0.2, 0.9, 0.3, 1);
  --default-transition-duration: 120ms;
}

:root {
  color-scheme: dark;

  --background: #0e1016;
  --foreground: #e9ecf5;
  --card: #171a24;
  --card-foreground: #e9ecf5;
  --popover: #1e2230;
  --popover-foreground: #e9ecf5;
  --primary: #6ee7c5;
  --primary-foreground: #0e1016;
  --secondary: #262b3a;
  --secondary-foreground: #e9ecf5;
  --muted: #1e2230;
  --muted-foreground: #9aa3b8;
  --accent: #262b3a;
  --accent-foreground: #e9ecf5;
  --destructive: #ff6b6b;
  --border: #2a3144;
  --input: #2a3144;
  --ring: #6ee7c5;
  --radius: 0.5rem;

  /* colore di sezione corrente: Panel/tone lo sovrascrivono */
  --tone: var(--primary);
}
```

`WebUI/packages/ui/src/index.css`:

```css
@import "tailwindcss/theme.css" layer(theme);
@import "tailwindcss/utilities.css" layer(utilities);
@import "tw-animate-css";
@import "shadcn/tailwind.css";
@import "./theme.css";

@font-face {
  font-family: "IBM Plex Sans";
  font-style: normal;
  font-weight: 400;
  font-display: swap;
  src: url("./fonts/ibm-plex-sans-latin-400-normal.woff2") format("woff2");
}
@font-face {
  font-family: "IBM Plex Sans";
  font-style: normal;
  font-weight: 500;
  font-display: swap;
  src: url("./fonts/ibm-plex-sans-latin-500-normal.woff2") format("woff2");
}
@font-face {
  font-family: "IBM Plex Sans";
  font-style: normal;
  font-weight: 600;
  font-display: swap;
  src: url("./fonts/ibm-plex-sans-latin-600-normal.woff2") format("woff2");
}
@font-face {
  font-family: "IBM Plex Mono";
  font-style: normal;
  font-weight: 400;
  font-display: swap;
  src: url("./fonts/ibm-plex-mono-latin-400-normal.woff2") format("woff2");
}

@layer base {
  * {
    @apply border-border outline-ring/50;
  }
  html {
    @apply font-sans;
  }
}
```

`WebUI/packages/ui/src/lib/utils.ts`:

```ts
export { cn } from "cn";
```

`WebUI/packages/ui/src/lib/tone.ts`:

```ts
import type { CSSProperties } from "react";

export type Tone = "osc" | "filter" | "env" | "lfo" | "fx" | "master";

export const TONES: readonly Tone[] = ["osc", "filter", "env", "lfo", "fx", "master"];

/**
 * Stile inline che imposta la variabile `--tone` sul colore di sezione.
 * I figli la consumano con `text-(--tone)`, `bg-(--tone)`, `stroke-(--tone)`.
 * Senza `tone` ritorna undefined: il componente eredita `--tone` dal Panel o dal default `:root`.
 */
export function toneStyle(tone?: Tone): CSSProperties | undefined {
  if (!tone) return undefined;
  return { "--tone": `var(--color-${tone})` } as CSSProperties;
}
```

`WebUI/packages/ui/src/index.ts` (temporaneo, il barrel cresce nei task successivi):

```ts
import "./index.css";

export { cn } from "@/lib/utils";
export { TONES, toneStyle, type Tone } from "@/lib/tone";
```

- [ ] **Step 5: Install, shadcn add, font**

Da `WebUI/`:

```bash
cd WebUI
pnpm install
cd packages/ui
pnpm dlx shadcn@latest add button switch select tabs card badge tooltip separator label -y
mkdir -p src/fonts
cp node_modules/@fontsource/ibm-plex-sans/files/ibm-plex-sans-latin-400-normal.woff2 src/fonts/
cp node_modules/@fontsource/ibm-plex-sans/files/ibm-plex-sans-latin-500-normal.woff2 src/fonts/
cp node_modules/@fontsource/ibm-plex-sans/files/ibm-plex-sans-latin-600-normal.woff2 src/fonts/
cp node_modules/@fontsource/ibm-plex-mono/files/ibm-plex-mono-latin-400-normal.woff2 src/fonts/
ls src/components/ui
```

Atteso: `badge.tsx button.tsx card.tsx label.tsx select.tsx separator.tsx switch.tsx tabs.tsx tooltip.tsx`. Se il CLI stampa un esempio con `RootLayout`/`TooltipProvider` è solo documentazione, ignorarlo. Se `pnpm dlx shadcn` fallisce con `ERR_PNPM_IGNORED_BUILDS`, verificare che `pnpm-workspace.yaml` contenga `onlyBuiltDependencies: [esbuild]` e rilanciare `pnpm install`.

- [ ] **Step 6: Build e verifica del CSS**

```bash
cd WebUI
pnpm --filter @xerum/ui build
ls packages/ui/dist
grep -c -- "--color-osc:#6ee7c5" packages/ui/dist/ui.css
grep -c -- "--spacing-knob-md:3rem" packages/ui/dist/ui.css
grep -c "data:font/woff2" packages/ui/dist/ui.css
grep -c "data-checked" packages/ui/dist/ui.css
```

Atteso: `dist/` contiene `index.js`, `index.js.map`, `index.d.ts`, `ui.css`, `lib/`, `components/ui/*.d.ts`; i quattro `grep -c` stampano `1`, `1`, `4`, un numero > 0. `tsc --noEmit` pulito (se `vitest/globals` non è ancora risolto perché Vitest arriva nel Task 2, è già in devDependencies: `pnpm install` del passo 5 lo ha installato).

- [ ] **Step 7: Commit**

```bash
cd /Users/andrea/Documents/personal/xerum
git add WebUI/pnpm-workspace.yaml WebUI/package.json WebUI/pnpm-lock.yaml WebUI/packages/ui
git rm -q WebUI/package-lock.json
git commit -m "Scaffold @xerum/ui package with pnpm workspace, shadcn base-nova and theme tokens

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

Controllare prima che `.gitignore` escluda `node_modules`, `dist`, `storybook-static` (aggiungere `WebUI/packages/ui/dist/` e `WebUI/packages/ui/storybook-static/` a `.gitignore` se mancano, e includerlo nel commit).

---

### Task 2: Vitest, Storybook, story dei token

**Files:**
- Create: `WebUI/packages/ui/vitest.config.ts`, `src/test/setup.ts`
- Create: `WebUI/packages/ui/.storybook/main.ts`, `.storybook/preview.ts`
- Create: `WebUI/packages/ui/src/stories/Tokens.stories.tsx`
- Test: `WebUI/packages/ui/src/lib/tone.test.ts`

**Interfaces:**
- Consumes: `toneStyle`, `TONES` da `@/lib/tone` (Task 1)
- Produces: comandi `pnpm --filter @xerum/ui test` e `build-storybook` funzionanti; stub globali `setPointerCapture`, `releasePointerCapture`, `hasPointerCapture`, `ResizeObserver` per i test.

- [ ] **Step 1: Test dell'helper tone (fallisce: nessuna config Vitest)**

`WebUI/packages/ui/src/lib/tone.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import { TONES, toneStyle } from "./tone";

describe("toneStyle", () => {
  it("returns undefined without a tone", () => {
    expect(toneStyle(undefined)).toBeUndefined();
  });

  it("maps a tone to the --tone CSS variable", () => {
    expect(toneStyle("filter")).toEqual({ "--tone": "var(--color-filter)" });
  });

  it("lists all six tones", () => {
    expect(TONES).toEqual(["osc", "filter", "env", "lfo", "fx", "master"]);
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test`
Atteso: FAIL (nessun file di configurazione / `No test files found` o errore setup).

- [ ] **Step 2: Config Vitest e setup**

`WebUI/packages/ui/vitest.config.ts`:

```ts
import { resolve } from "node:path";
import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  resolve: { alias: { "@": resolve(__dirname, "src") } },
  test: {
    environment: "jsdom",
    globals: true,
    css: false,
    setupFiles: ["./src/test/setup.ts"],
    include: ["src/**/*.test.{ts,tsx}"],
  },
});
```

`WebUI/packages/ui/src/test/setup.ts`:

```ts
import "@testing-library/jest-dom/vitest";
import { afterEach } from "vitest";
import { cleanup } from "@testing-library/react";

afterEach(() => cleanup());

// jsdom non implementa pointer capture né ResizeObserver.
const proto = Element.prototype as Element & {
  setPointerCapture?: (id: number) => void;
  releasePointerCapture?: (id: number) => void;
  hasPointerCapture?: (id: number) => boolean;
};
proto.setPointerCapture ??= () => {};
proto.releasePointerCapture ??= () => {};
proto.hasPointerCapture ??= () => false;

if (typeof globalThis.ResizeObserver === "undefined") {
  class ResizeObserverStub {
    observe() {}
    unobserve() {}
    disconnect() {}
  }
  globalThis.ResizeObserver = ResizeObserverStub as unknown as typeof ResizeObserver;
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test`
Atteso: PASS, 3 test.

- [ ] **Step 3: Storybook config**

`WebUI/packages/ui/.storybook/main.ts`:

```ts
import type { StorybookConfig } from "@storybook/react-vite";

const config: StorybookConfig = {
  framework: "@storybook/react-vite",
  stories: ["../src/**/*.stories.tsx"],
  core: { disableTelemetry: true },
  async viteFinal(config) {
    // La build lib del package non serve a Storybook: rimuove lib mode e dts.
    return {
      ...config,
      build: { ...config.build, lib: undefined, rollupOptions: {} },
      plugins: (config.plugins ?? []).filter(
        (p) => !(p && typeof p === "object" && "name" in p && String(p.name).startsWith("vite:dts")),
      ),
    };
  },
};

export default config;
```

`WebUI/packages/ui/.storybook/preview.ts`:

```ts
import type { Preview } from "@storybook/react-vite";
import "../src/index.css";

const preview: Preview = {
  parameters: {
    backgrounds: {
      options: { dark: { name: "dark", value: "#0e1016" } },
    },
    layout: "centered",
  },
  initialGlobals: { backgrounds: { value: "dark" } },
};

export default preview;
```

- [ ] **Step 4: Story dei token**

`WebUI/packages/ui/src/stories/Tokens.stories.tsx`:

```tsx
import type { Meta, StoryObj } from "@storybook/react-vite";
import { TONES } from "@/lib/tone";

const meta = { title: "Foundations/Tokens" } satisfies Meta;
export default meta;

const semantic = [
  "background", "foreground", "card", "popover", "primary", "secondary",
  "muted", "muted-foreground", "accent", "destructive", "border", "ring",
] as const;

const surfaces = ["surface-0", "surface-1", "surface-2", "surface-3", "line-strong", "text-dim", "warning"] as const;

function Swatch({ name }: { name: string }) {
  return (
    <div className="flex flex-col gap-1">
      <div className="size-12 rounded-md border border-border" style={{ background: `var(--color-${name})` }} />
      <span className="font-mono text-2xs text-muted-foreground">{name}</span>
    </div>
  );
}

export const Colors: StoryObj = {
  render: () => (
    <div className="flex flex-col gap-6 p-6 text-foreground">
      <section className="flex flex-col gap-2">
        <h2 className="text-xs uppercase tracking-widest text-muted-foreground">Semantic</h2>
        <div className="flex flex-wrap gap-3">{semantic.map((n) => <Swatch key={n} name={n} />)}</div>
      </section>
      <section className="flex flex-col gap-2">
        <h2 className="text-xs uppercase tracking-widest text-muted-foreground">Surfaces</h2>
        <div className="flex flex-wrap gap-3">{surfaces.map((n) => <Swatch key={n} name={n} />)}</div>
      </section>
      <section className="flex flex-col gap-2">
        <h2 className="text-xs uppercase tracking-widest text-muted-foreground">Tones</h2>
        <div className="flex flex-wrap gap-3">{TONES.map((n) => <Swatch key={n} name={n} />)}</div>
      </section>
    </div>
  ),
};

export const Typography: StoryObj = {
  render: () => (
    <div className="flex flex-col gap-3 p-6 text-foreground">
      <p className="font-sans text-2xl font-semibold">IBM Plex Sans 600 — Wavetable</p>
      <p className="font-sans text-base font-medium">IBM Plex Sans 500 — Filter cutoff</p>
      <p className="font-sans text-sm">IBM Plex Sans 400 — body text</p>
      <p className="font-mono text-sm tabular-nums">IBM Plex Mono 400 — 440.00 Hz</p>
      <p className="text-2xs uppercase tracking-wider text-muted-foreground">text-2xs label</p>
    </div>
  ),
};
```

- [ ] **Step 5: Verifica Storybook**

```bash
cd WebUI && pnpm --filter @xerum/ui build-storybook
ls packages/ui/storybook-static | head
```

Atteso: build senza errori, cartella `storybook-static/` con `index.html` e `iframe.html`. Se `viteFinal` fallisce per il filtro plugin, rimuovere il filtro e tenere solo `build: { lib: undefined }`: dts in Storybook è innocuo.

Poi `pnpm --filter @xerum/ui test` e `typecheck` puliti.

- [ ] **Step 6: Commit**

```bash
git add WebUI/packages/ui/vitest.config.ts WebUI/packages/ui/src/test WebUI/packages/ui/.storybook WebUI/packages/ui/src/stories WebUI/packages/ui/src/lib/tone.test.ts
git commit -m "Add Vitest, Storybook and token stories to @xerum/ui

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: Hook `useDragValue`

**Files:**
- Create: `WebUI/packages/ui/src/hooks/useDragValue.ts`
- Test: `WebUI/packages/ui/src/hooks/useDragValue.test.tsx`

**Interfaces:**
- Produces:

```ts
export type UseDragValueOptions = {
  value: number;               // 0..1
  defaultValue?: number;       // doppio click; default 0
  onChange: (v: number) => void;
  step?: number;               // rotella/tastiera; default 0.01
  sensitivity?: number;        // px per corsa completa; default 200
  axis?: "y" | "x";            // default "y" (su = +); "x": destra = +
  disabled?: boolean;
};
export type UseDragValueResult = {
  ref: (el: HTMLElement | null) => void;   // da mettere sull'elemento: wheel non-passive
  handlers: {
    onPointerDown: React.PointerEventHandler<HTMLElement>;
    onPointerMove: React.PointerEventHandler<HTMLElement>;
    onPointerUp: React.PointerEventHandler<HTMLElement>;
    onPointerCancel: React.PointerEventHandler<HTMLElement>;
    onDoubleClick: React.MouseEventHandler<HTMLElement>;
    onKeyDown: React.KeyboardEventHandler<HTMLElement>;
  };
  dragging: boolean;
};
export function useDragValue(o: UseDragValueOptions): UseDragValueResult;
```

- [ ] **Step 1: Test (fallisce: modulo assente)**

`WebUI/packages/ui/src/hooks/useDragValue.test.tsx`:

```tsx
import { useState } from "react";
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { useDragValue, type UseDragValueOptions } from "./useDragValue";

type HarnessProps = Partial<Omit<UseDragValueOptions, "value" | "onChange">> & {
  initial?: number;
  onChange?: (v: number) => void;
};

function Harness({ initial = 0.5, onChange, ...opts }: HarnessProps) {
  const [value, setValue] = useState(initial);
  const { ref, handlers, dragging } = useDragValue({
    value,
    onChange: (v) => {
      setValue(v);
      onChange?.(v);
    },
    ...opts,
  });
  return (
    <div ref={ref} data-testid="target" data-dragging={dragging} tabIndex={0} {...handlers}>
      {value.toFixed(4)}
    </div>
  );
}

const target = () => screen.getByTestId("target");
const drag = (fromY: number, toY: number, extra: Record<string, unknown> = {}) => {
  fireEvent.pointerDown(target(), { clientY: fromY, clientX: 0, button: 0, pointerId: 1 });
  fireEvent.pointerMove(target(), { clientY: toY, clientX: 0, pointerId: 1, ...extra });
  fireEvent.pointerUp(target(), { clientY: toY, clientX: 0, pointerId: 1 });
};

describe("useDragValue", () => {
  it("increases when dragging up (default sensitivity 200px = full range)", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    drag(100, 60);
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("decreases when dragging down", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    drag(100, 140);
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.3, 5));
  });

  it("clamps to 0..1", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.9} onChange={onChange} />);
    drag(100, 0);
    expect(onChange).toHaveBeenLastCalledWith(1);
  });

  it("uses x axis when axis is 'x' (right = +)", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} axis="x" />);
    fireEvent.pointerDown(target(), { clientX: 100, clientY: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(target(), { clientX: 140, clientY: 0, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("is 10x finer with shift held during drag", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    fireEvent.pointerDown(target(), { clientY: 100, clientX: 0, button: 0, pointerId: 1, shiftKey: true });
    fireEvent.pointerMove(target(), { clientY: 60, clientX: 0, pointerId: 1, shiftKey: true });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.52, 5));
  });

  it("re-anchors when shift toggles mid-drag (no jump)", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    fireEvent.pointerDown(target(), { clientY: 100, clientX: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(target(), { clientY: 60, clientX: 0, pointerId: 1 });      // -> 0.7
    fireEvent.pointerMove(target(), { clientY: 60, clientX: 0, pointerId: 1, shiftKey: true }); // re-anchor, no change
    fireEvent.pointerMove(target(), { clientY: 40, clientX: 0, pointerId: 1, shiftKey: true }); // +20px fine = +0.01
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.71, 5));
  });

  it("exposes dragging state", () => {
    render(<Harness />);
    expect(target()).toHaveAttribute("data-dragging", "false");
    fireEvent.pointerDown(target(), { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(target()).toHaveAttribute("data-dragging", "true");
    fireEvent.pointerUp(target(), { clientY: 0, clientX: 0, pointerId: 1 });
    expect(target()).toHaveAttribute("data-dragging", "false");
  });

  it("wheel up adds step, wheel down subtracts, shift makes it fine", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} step={0.05} />);
    fireEvent.wheel(target(), { deltaY: -100 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.55, 5));
    fireEvent.wheel(target(), { deltaY: 100 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.5, 5));
    fireEvent.wheel(target(), { deltaY: -100, shiftKey: true });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.505, 5));
  });

  it("double click resets to defaultValue", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.8} defaultValue={0.25} onChange={onChange} />);
    fireEvent.doubleClick(target());
    expect(onChange).toHaveBeenLastCalledWith(0.25);
  });

  it("keyboard: arrows, page, home, end, shift fine", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} step={0.01} />);
    const t = target();
    fireEvent.keyDown(t, { key: "ArrowUp" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.51, 5));
    fireEvent.keyDown(t, { key: "ArrowRight" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.52, 5));
    fireEvent.keyDown(t, { key: "ArrowDown" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.51, 5));
    fireEvent.keyDown(t, { key: "ArrowLeft" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.5, 5));
    fireEvent.keyDown(t, { key: "PageUp" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.6, 5));
    fireEvent.keyDown(t, { key: "PageDown" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.5, 5));
    fireEvent.keyDown(t, { key: "ArrowUp", shiftKey: true });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.501, 5));
    fireEvent.keyDown(t, { key: "Home" });
    expect(onChange).toHaveBeenLastCalledWith(0);
    fireEvent.keyDown(t, { key: "End" });
    expect(onChange).toHaveBeenLastCalledWith(1);
  });

  it("does not call onChange when the value would not change", () => {
    const onChange = vi.fn();
    render(<Harness initial={0} onChange={onChange} />);
    fireEvent.keyDown(target(), { key: "Home" });
    fireEvent.keyDown(target(), { key: "ArrowDown" });
    drag(100, 200);
    expect(onChange).not.toHaveBeenCalled();
  });

  it("ignores everything when disabled", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} disabled />);
    drag(100, 0);
    fireEvent.wheel(target(), { deltaY: -100 });
    fireEvent.doubleClick(target());
    fireEvent.keyDown(target(), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
    expect(target()).toHaveAttribute("data-dragging", "false");
  });

  it("ignores non-primary buttons", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    fireEvent.pointerDown(target(), { clientY: 100, clientX: 0, button: 2, pointerId: 1 });
    fireEvent.pointerMove(target(), { clientY: 0, clientX: 0, pointerId: 1 });
    expect(onChange).not.toHaveBeenCalled();
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- useDragValue`
Atteso: FAIL con `Cannot find module './useDragValue'`.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/hooks/useDragValue.ts`:

```ts
import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type KeyboardEventHandler,
  type MouseEventHandler,
  type PointerEventHandler,
} from "react";

export type UseDragValueOptions = {
  /** Valore corrente, normalizzato 0..1. */
  value: number;
  /** Valore ripristinato dal doppio click. Default 0. */
  defaultValue?: number;
  onChange: (v: number) => void;
  /** Incremento per rotella e frecce. Default 0.01. Shift = step/10. */
  step?: number;
  /** Pixel di trascinamento per percorrere l'intero range. Default 200. Shift = ×10 (fine). */
  sensitivity?: number;
  /** "y": su = +. "x": destra = +. Default "y". */
  axis?: "y" | "x";
  disabled?: boolean;
};

export type UseDragValueResult = {
  ref: (el: HTMLElement | null) => void;
  handlers: {
    onPointerDown: PointerEventHandler<HTMLElement>;
    onPointerMove: PointerEventHandler<HTMLElement>;
    onPointerUp: PointerEventHandler<HTMLElement>;
    onPointerCancel: PointerEventHandler<HTMLElement>;
    onDoubleClick: MouseEventHandler<HTMLElement>;
    onKeyDown: KeyboardEventHandler<HTMLElement>;
  };
  dragging: boolean;
};

type Origin = { pos: number; value: number; shift: boolean };

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

export function useDragValue({
  value,
  defaultValue = 0,
  onChange,
  step = 0.01,
  sensitivity = 200,
  axis = "y",
  disabled = false,
}: UseDragValueOptions): UseDragValueResult {
  const [dragging, setDragging] = useState(false);
  const origin = useRef<Origin | null>(null);
  const elRef = useRef<HTMLElement | null>(null);

  // Ultimi valori senza rifare i listener a ogni render.
  const latest = useRef({ value, onChange, disabled, step });
  latest.current = { value, onChange, disabled, step };

  const emit = useCallback((next: number) => {
    const v = clamp01(next);
    if (v !== latest.current.value) latest.current.onChange(v);
  }, []);

  const readPos = useCallback(
    (e: { clientX: number; clientY: number }) => (axis === "y" ? e.clientY : e.clientX),
    [axis],
  );

  const onPointerDown: PointerEventHandler<HTMLElement> = useCallback(
    (e) => {
      if (latest.current.disabled || e.button !== 0) return;
      e.preventDefault();
      e.currentTarget.setPointerCapture?.(e.pointerId);
      origin.current = { pos: readPos(e), value: latest.current.value, shift: e.shiftKey };
      setDragging(true);
    },
    [readPos],
  );

  const onPointerMove: PointerEventHandler<HTMLElement> = useCallback(
    (e) => {
      const o = origin.current;
      if (!o) return;
      if (o.shift !== e.shiftKey) {
        // Cambio modalità fine/normale: riancora per evitare salti.
        origin.current = { pos: readPos(e), value: latest.current.value, shift: e.shiftKey };
        return;
      }
      const sens = e.shiftKey ? sensitivity * 10 : sensitivity;
      const delta = axis === "y" ? o.pos - e.clientY : e.clientX - o.pos;
      emit(o.value + delta / sens);
    },
    [axis, emit, readPos, sensitivity],
  );

  const endDrag: PointerEventHandler<HTMLElement> = useCallback((e) => {
    if (!origin.current) return;
    origin.current = null;
    if (e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture?.(e.pointerId);
    }
    setDragging(false);
  }, []);

  const onDoubleClick: MouseEventHandler<HTMLElement> = useCallback(() => {
    if (latest.current.disabled) return;
    emit(defaultValue);
  }, [defaultValue, emit]);

  const onKeyDown: KeyboardEventHandler<HTMLElement> = useCallback(
    (e) => {
      if (latest.current.disabled) return;
      const s = e.shiftKey ? latest.current.step / 10 : latest.current.step;
      const v = latest.current.value;
      const targets: Record<string, number> = {
        ArrowUp: v + s,
        ArrowRight: v + s,
        ArrowDown: v - s,
        ArrowLeft: v - s,
        PageUp: v + 10 * latest.current.step,
        PageDown: v - 10 * latest.current.step,
        Home: 0,
        End: 1,
      };
      const next = targets[e.key];
      if (next === undefined) return;
      e.preventDefault();
      emit(next);
    },
    [emit],
  );

  // Wheel come listener nativo non-passive: React registra `wheel` passive e
  // preventDefault non fermerebbe lo scroll della pagina.
  const ref = useCallback((el: HTMLElement | null) => {
    elRef.current = el;
  }, []);

  useEffect(() => {
    const el = elRef.current;
    if (!el) return;
    const onWheel = (e: WheelEvent) => {
      if (latest.current.disabled) return;
      e.preventDefault();
      const s = e.shiftKey ? latest.current.step / 10 : latest.current.step;
      emit(latest.current.value + (e.deltaY < 0 ? s : -s));
    };
    el.addEventListener("wheel", onWheel, { passive: false });
    return () => el.removeEventListener("wheel", onWheel);
  }, [emit]);

  return {
    ref,
    handlers: {
      onPointerDown,
      onPointerMove,
      onPointerUp: endDrag,
      onPointerCancel: endDrag,
      onDoubleClick,
      onKeyDown,
    },
    dragging,
  };
}
```

- [ ] **Step 3: Test verdi**

Run: `cd WebUI && pnpm --filter @xerum/ui test -- useDragValue`
Atteso: PASS, 13 test. Se il test "re-anchors" fallisce per arrotondamento, usare `closeTo(x, 3)`; se `fireEvent.wheel` non chiama il listener, verificare che il `ref` callback sia montato prima dell'effetto (lo è: i callback ref girano prima degli effetti).

- [ ] **Step 4: Commit**

```bash
git add WebUI/packages/ui/src/hooks
git commit -m "Add useDragValue hook for knob and fader interaction

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: Helper arco SVG e componente `Knob`

**Files:**
- Create: `WebUI/packages/ui/src/lib/arc.ts`, `src/lib/arc.test.ts`
- Create: `WebUI/packages/ui/src/components/Knob/Knob.tsx`, `Knob.test.tsx`, `Knob.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `useDragValue` (Task 3), `toneStyle`, `Tone`, `cn`
- Produces:

```ts
// @/lib/arc
export const KNOB_START = 135;  // gradi, SVG (y verso il basso): basso-sinistra
export const KNOB_SWEEP = 270;
export function knobAngles(value: number, bipolar: boolean): { start: number; end: number };
export function arcPath(cx: number, cy: number, r: number, startDeg: number, endDeg: number): string;
// @/components/Knob
export type KnobProps = {
  value: number; defaultValue?: number; onChange: (v: number) => void; label: string;
  format?: (v: number) => string; size?: "sm" | "md" | "lg"; tone?: Tone; bipolar?: boolean;
  disabled?: boolean; className?: string; id?: string;
};
export function Knob(props: KnobProps): JSX.Element;
```

- [ ] **Step 1: Test dell'arco (fallisce)**

`WebUI/packages/ui/src/lib/arc.test.ts`:

```ts
import { describe, expect, it } from "vitest";
import { arcPath, knobAngles, KNOB_START, KNOB_SWEEP } from "./arc";

describe("knobAngles", () => {
  it("unipolar: from start to start + sweep*value", () => {
    expect(knobAngles(0, false)).toEqual({ start: KNOB_START, end: KNOB_START });
    expect(knobAngles(1, false)).toEqual({ start: KNOB_START, end: KNOB_START + KNOB_SWEEP });
    expect(knobAngles(0.5, false)).toEqual({ start: KNOB_START, end: 270 });
  });

  it("bipolar: from centre (270°) to the value angle, ordered", () => {
    expect(knobAngles(0.5, true)).toEqual({ start: 270, end: 270 });
    expect(knobAngles(1, true)).toEqual({ start: 270, end: 405 });
    expect(knobAngles(0.25, true)).toEqual({ start: 202.5, end: 270 });
  });
});

describe("arcPath", () => {
  it("returns an SVG arc from start to end angle", () => {
    const d = arcPath(20, 20, 16, 270, 360);
    expect(d).toMatch(/^M 20 4 A 16 16 0 0 1 36 20$/);
  });

  it("uses the large-arc flag past 180°", () => {
    expect(arcPath(20, 20, 16, 135, 405)).toContain(" 0 1 1 ");
  });

  it("swaps the angles when end < start", () => {
    expect(arcPath(20, 20, 16, 360, 270)).toBe(arcPath(20, 20, 16, 270, 360));
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- arc`
Atteso: FAIL, modulo assente.

- [ ] **Step 2: Implementazione arco**

`WebUI/packages/ui/src/lib/arc.ts`:

```ts
/** Angoli in gradi nel sistema SVG (0° = destra, y verso il basso, senso orario). */
export const KNOB_START = 135;
export const KNOB_SWEEP = 270;
const KNOB_CENTRE = KNOB_START + KNOB_SWEEP / 2; // 270 = in alto

export function knobAngles(value: number, bipolar: boolean): { start: number; end: number } {
  const at = KNOB_START + KNOB_SWEEP * value;
  if (!bipolar) return { start: KNOB_START, end: at };
  return at >= KNOB_CENTRE ? { start: KNOB_CENTRE, end: at } : { start: at, end: KNOB_CENTRE };
}

function polar(cx: number, cy: number, r: number, deg: number): [number, number] {
  const a = (deg * Math.PI) / 180;
  return [round(cx + r * Math.cos(a)), round(cy + r * Math.sin(a))];
}

const round = (n: number) => Math.round(n * 1000) / 1000;

export function arcPath(cx: number, cy: number, r: number, startDeg: number, endDeg: number): string {
  if (endDeg < startDeg) [startDeg, endDeg] = [endDeg, startDeg];
  const [x1, y1] = polar(cx, cy, r, startDeg);
  const [x2, y2] = polar(cx, cy, r, endDeg);
  const large = endDeg - startDeg > 180 ? 1 : 0;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- arc` → PASS. (Se il primo test di `arcPath` fallisce per `-0` o `4.000`, la funzione `round` produce `4` e `36`: verificare che `Math.round` non lasci `-0`; in tal caso `round = (n) => Math.round(n * 1000) / 1000 + 0`.)

- [ ] **Step 3: Test del Knob (fallisce)**

`WebUI/packages/ui/src/components/Knob/Knob.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Knob } from "./Knob";

describe("Knob", () => {
  it("is an accessible slider with normalised range", () => {
    render(<Knob value={0.25} onChange={() => {}} label="Cutoff" />);
    const slider = screen.getByRole("slider", { name: "Cutoff" });
    expect(slider).toHaveAttribute("aria-valuemin", "0");
    expect(slider).toHaveAttribute("aria-valuemax", "1");
    expect(slider).toHaveAttribute("aria-valuenow", "0.25");
    expect(slider).toHaveAttribute("tabindex", "0");
  });

  it("uses format for the readout and aria-valuetext", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" format={(v) => `${Math.round(v * 20000)} Hz`} />);
    expect(screen.getByRole("slider")).toHaveAttribute("aria-valuetext", "10000 Hz");
    expect(screen.getByTestId("knob-readout")).toHaveTextContent("10000 Hz");
  });

  it("defaults the readout to a percentage", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Level" />);
    expect(screen.getByTestId("knob-readout")).toHaveTextContent("50%");
  });

  it("changes value with the keyboard", () => {
    const onChange = vi.fn();
    render(<Knob value={0.5} onChange={onChange} label="Cutoff" />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "ArrowUp" });
    expect(onChange).toHaveBeenCalledWith(expect.closeTo(0.51, 5));
  });

  it("marks dragging state", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" />);
    const slider = screen.getByRole("slider");
    expect(slider).toHaveAttribute("data-dragging", "false");
    fireEvent.pointerDown(slider, { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(slider).toHaveAttribute("data-dragging", "true");
  });

  it("draws the value arc from the centre when bipolar", () => {
    const { rerender } = render(<Knob value={0.5} onChange={() => {}} label="Pan" bipolar />);
    const arc = () => screen.getByTestId("knob-value-arc").getAttribute("d") ?? "";
    // 0.5 bipolar = arco nullo: inizio e fine coincidono in alto (20, 4)
    expect(arc()).toMatch(/^M 20 4 A 16 16 0 0 1 20 4$/);
    rerender(<Knob value={1} onChange={() => {}} label="Pan" bipolar />);
    expect(arc().startsWith("M 20 4")).toBe(true);
  });

  it("blocks input and exposes aria-disabled when disabled", () => {
    const onChange = vi.fn();
    render(<Knob value={0.5} onChange={onChange} label="Cutoff" disabled />);
    const slider = screen.getByRole("slider");
    expect(slider).toHaveAttribute("aria-disabled", "true");
    fireEvent.keyDown(slider, { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("sets --tone from the tone prop", () => {
    render(<Knob value={0.5} onChange={() => {}} label="Cutoff" tone="filter" />);
    expect(screen.getByTestId("knob").style.getPropertyValue("--tone")).toBe("var(--color-filter)");
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Knob` → FAIL, modulo assente.

- [ ] **Step 4: Implementazione Knob**

`WebUI/packages/ui/src/components/Knob/Knob.tsx`:

```tsx
import { cva } from "class-variance-authority";
import { cn } from "@/lib/utils";
import { arcPath, knobAngles, KNOB_START, KNOB_SWEEP } from "@/lib/arc";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type KnobProps = {
  /** 0..1 */
  value: number;
  /** Valore del doppio click. Default 0 (0.5 se bipolar). */
  defaultValue?: number;
  onChange: (v: number) => void;
  label: string;
  /** Testo del readout e di aria-valuetext. Default: percentuale. */
  format?: (v: number) => string;
  size?: "sm" | "md" | "lg";
  tone?: Tone;
  /** Arco disegnato dal centro invece che da zero. */
  bipolar?: boolean;
  disabled?: boolean;
  className?: string;
  id?: string;
};

const knobSize = cva("relative shrink-0 rounded-full outline-none select-none touch-none", {
  variants: {
    size: { sm: "size-knob-sm", md: "size-knob-md", lg: "size-knob-lg" },
  },
  defaultVariants: { size: "md" },
});

const defaultFormat = (v: number) => `${Math.round(v * 100)}%`;

// viewBox 40×40, arco a raggio 16
const C = 20;
const R = 16;

export function Knob({
  value,
  defaultValue,
  onChange,
  label,
  format = defaultFormat,
  size = "md",
  tone,
  bipolar = false,
  disabled = false,
  className,
  id,
}: KnobProps) {
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue: defaultValue ?? (bipolar ? 0.5 : 0),
    onChange,
    disabled,
  });

  const { start, end } = knobAngles(value, bipolar);
  const text = format(value);
  const pointerDeg = KNOB_START + KNOB_SWEEP * value;

  return (
    <div
      data-testid="knob"
      data-slot="knob"
      className={cn("group/knob flex flex-col items-center gap-1", disabled && "opacity-50", className)}
      style={toneStyle(tone)}
    >
      <div
        ref={ref}
        id={id}
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-valuetext={text}
        aria-disabled={disabled || undefined}
        aria-orientation="vertical"
        data-dragging={dragging}
        className={cn(
          knobSize({ size }),
          "cursor-ns-resize focus-visible:ring-2 focus-visible:ring-(--tone)/50",
          disabled && "cursor-not-allowed",
        )}
        {...handlers}
      >
        <svg viewBox="0 0 40 40" className="size-full">
          <path
            d={arcPath(C, C, R, KNOB_START, KNOB_START + KNOB_SWEEP)}
            className="fill-none stroke-surface-3"
            strokeWidth={3}
            strokeLinecap="round"
          />
          <path
            data-testid="knob-value-arc"
            d={arcPath(C, C, R, start, end)}
            className="fill-none stroke-(--tone) transition-[d] ease-snap"
            strokeWidth={3}
            strokeLinecap="round"
          />
          <circle cx={C} cy={C} r={11} className="fill-surface-1 stroke-border" strokeWidth={1} />
          <line
            x1={C}
            y1={C}
            x2={C}
            y2={C - 9}
            className="stroke-foreground"
            strokeWidth={2}
            strokeLinecap="round"
            transform={`rotate(${pointerDeg - 270} ${C} ${C})`}
          />
        </svg>
        <span
          data-testid="knob-readout"
          className={cn(
            "pointer-events-none absolute -top-5 left-1/2 -translate-x-1/2 rounded-sm bg-popover px-1 py-px font-mono text-2xs whitespace-nowrap text-foreground tabular-nums opacity-0 transition-opacity",
            "group-hover/knob:opacity-100 group-focus-within/knob:opacity-100 data-[dragging=true]:opacity-100",
          )}
          data-dragging={dragging}
        >
          {text}
        </span>
      </div>
      <span className="text-2xs uppercase tracking-wider text-muted-foreground">{label}</span>
    </div>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Knob` → PASS, 8 test.

- [ ] **Step 5: Story**

`WebUI/packages/ui/src/components/Knob/Knob.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Knob, type KnobProps } from "./Knob";

const meta = {
  title: "Controls/Knob",
  component: Knob,
  args: { value: 0.35, label: "Cutoff", onChange: () => {} },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Knob>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: KnobProps) {
  const [value, setValue] = useState(props.value);
  return <Knob {...props} value={value} onChange={setValue} />;
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Sizes: Story = {
  render: (args) => (
    <div className="flex items-end gap-6">
      <Controlled {...args} size="sm" label="sm" />
      <Controlled {...args} size="md" label="md" />
      <Controlled {...args} size="lg" label="lg" />
    </div>
  ),
};
export const Tones: Story = {
  render: (args) => (
    <div className="flex gap-6">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Bipolar: Story = {
  args: { value: 0.5, label: "Pan", bipolar: true, format: (v) => `${Math.round((v - 0.5) * 200)}` },
  render: (args) => <Controlled {...args} />,
};
export const Formatted: Story = {
  args: { value: 0.5, label: "Cutoff", tone: "filter", format: (v) => `${Math.round(20 * Math.pow(1000, v))} Hz` },
  render: (args) => <Controlled {...args} />,
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
```

- [ ] **Step 6: Export e verifica visiva**

In `src/index.ts` aggiungi:

```ts
export { Knob, type KnobProps } from "@/components/Knob/Knob";
```

Run: `cd WebUI && pnpm --filter @xerum/ui storybook` e apri `http://localhost:6006`. Verifica: arco in colore tono, indicatore ruota da basso-sinistra a basso-destra, readout appare in hover, drag verticale funziona, doppio click resetta. Chiudi il server. Poi `pnpm --filter @xerum/ui test` e `typecheck` e `check:colors` puliti.

- [ ] **Step 7: Commit**

```bash
git add WebUI/packages/ui/src/lib/arc.ts WebUI/packages/ui/src/lib/arc.test.ts WebUI/packages/ui/src/components/Knob WebUI/packages/ui/src/index.ts
git commit -m "Add Knob component with SVG arc and drag interaction

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: Componente `Fader`

**Files:**
- Create: `WebUI/packages/ui/src/components/Fader/Fader.tsx`, `Fader.test.tsx`, `Fader.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `useDragValue` con `axis`, `toneStyle`, `cn`
- Produces:

```ts
export type FaderProps = {
  value: number; defaultValue?: number; onChange: (v: number) => void; label?: string;
  format?: (v: number) => string; orientation?: "vertical" | "horizontal"; tone?: Tone;
  disabled?: boolean; className?: string; id?: string;
};
export function Fader(props: FaderProps): JSX.Element;
```

- [ ] **Step 1: Test (fallisce)**

`WebUI/packages/ui/src/components/Fader/Fader.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Fader } from "./Fader";

describe("Fader", () => {
  it("is a vertical slider by default", () => {
    render(<Fader value={0.5} onChange={() => {}} label="Level" />);
    const s = screen.getByRole("slider", { name: "Level" });
    expect(s).toHaveAttribute("aria-orientation", "vertical");
    expect(s).toHaveAttribute("aria-valuenow", "0.5");
  });

  it("supports horizontal orientation and x-axis drag", () => {
    const onChange = vi.fn();
    render(<Fader value={0.5} onChange={onChange} orientation="horizontal" />);
    const s = screen.getByRole("slider");
    expect(s).toHaveAttribute("aria-orientation", "horizontal");
    fireEvent.pointerDown(s, { clientX: 100, clientY: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(s, { clientX: 140, clientY: 0, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("resets on double click", () => {
    const onChange = vi.fn();
    render(<Fader value={0.8} defaultValue={0.5} onChange={onChange} />);
    fireEvent.doubleClick(screen.getByRole("slider"));
    expect(onChange).toHaveBeenCalledWith(0.5);
  });

  it("sizes the fill from the value", () => {
    render(<Fader value={0.25} onChange={() => {}} />);
    expect(screen.getByTestId("fader-fill").style.height).toBe("25%");
  });

  it("shows the formatted readout", () => {
    render(<Fader value={0.5} onChange={() => {}} format={(v) => `${(v * 12 - 6).toFixed(1)} dB`} />);
    expect(screen.getByTestId("fader-readout")).toHaveTextContent("0.0 dB");
  });

  it("is inert when disabled", () => {
    const onChange = vi.fn();
    render(<Fader value={0.5} onChange={onChange} disabled />);
    fireEvent.keyDown(screen.getByRole("slider"), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Fader` → FAIL.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/components/Fader/Fader.tsx`:

```tsx
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type FaderProps = {
  /** 0..1 */
  value: number;
  defaultValue?: number;
  onChange: (v: number) => void;
  label?: string;
  format?: (v: number) => string;
  orientation?: "vertical" | "horizontal";
  tone?: Tone;
  disabled?: boolean;
  className?: string;
  id?: string;
};

const defaultFormat = (v: number) => `${Math.round(v * 100)}%`;

export function Fader({
  value,
  defaultValue = 0,
  onChange,
  label,
  format = defaultFormat,
  orientation = "vertical",
  tone,
  disabled = false,
  className,
  id,
}: FaderProps) {
  const vertical = orientation === "vertical";
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue,
    onChange,
    disabled,
    axis: vertical ? "y" : "x",
  });
  const text = format(value);
  const pct = `${value * 100}%`;

  return (
    <div
      data-slot="fader"
      className={cn("group/fader flex items-center gap-2", vertical ? "flex-col" : "flex-row", disabled && "opacity-50", className)}
      style={toneStyle(tone)}
    >
      <div
        ref={ref}
        id={id}
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-orientation={orientation}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-valuetext={text}
        aria-disabled={disabled || undefined}
        data-dragging={dragging}
        className={cn(
          "relative rounded-full bg-surface-2 outline-none select-none touch-none focus-visible:ring-2 focus-visible:ring-(--tone)/50",
          vertical ? "h-fader w-2 cursor-ns-resize" : "h-2 w-fader cursor-ew-resize",
          disabled && "cursor-not-allowed",
        )}
        {...handlers}
      >
        <div
          data-testid="fader-fill"
          className={cn("absolute rounded-full bg-(--tone)", vertical ? "inset-x-0 bottom-0" : "inset-y-0 left-0")}
          style={vertical ? { height: pct } : { width: pct }}
        />
        <div
          data-slot="fader-thumb"
          className={cn(
            "absolute size-4 rounded-full border border-line-strong bg-surface-3 shadow-sm transition-transform ease-snap group-data-[dragging=true]/fader:scale-110",
            vertical ? "left-1/2 -translate-x-1/2 translate-y-1/2" : "top-1/2 -translate-x-1/2 -translate-y-1/2",
          )}
          style={vertical ? { bottom: pct } : { left: pct }}
        />
      </div>
      <div className={cn("flex items-center gap-1", vertical ? "flex-col" : "flex-row")}>
        <span
          data-testid="fader-readout"
          className="font-mono text-2xs text-foreground tabular-nums"
        >
          {text}
        </span>
        {label && <span className="text-2xs uppercase tracking-wider text-muted-foreground">{label}</span>}
      </div>
    </div>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Fader` → PASS, 6 test.

- [ ] **Step 3: Story**

`WebUI/packages/ui/src/components/Fader/Fader.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Fader, type FaderProps } from "./Fader";

const meta = {
  title: "Controls/Fader",
  component: Fader,
  args: { value: 0.6, label: "Level", onChange: () => {} },
  argTypes: { tone: { control: "select", options: ["osc", "filter", "env", "lfo", "fx", "master"] } },
} satisfies Meta<typeof Fader>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: FaderProps) {
  const [value, setValue] = useState(props.value);
  return <Fader {...props} value={value} onChange={setValue} />;
}

export const Vertical: Story = { render: (args) => <Controlled {...args} /> };
export const Horizontal: Story = { args: { orientation: "horizontal" }, render: (args) => <Controlled {...args} /> };
export const Tones: Story = {
  render: (args) => (
    <div className="flex gap-6">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Decibel: Story = {
  args: { tone: "master", format: (v) => `${(v * 12 - 6).toFixed(1)} dB`, defaultValue: 0.5 },
  render: (args) => <Controlled {...args} />,
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
```

- [ ] **Step 4: Export, verifica, commit**

In `src/index.ts`: `export { Fader, type FaderProps } from "@/components/Fader/Fader";`

Storybook: thumb segue il valore, fill in colore tono, drag verticale e orizzontale corretti. `test`, `typecheck`, `check:colors` puliti.

```bash
git add WebUI/packages/ui/src/components/Fader WebUI/packages/ui/src/index.ts
git commit -m "Add Fader component

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 6: Componente `Button` con `tone`

**Files:**
- Create: `WebUI/packages/ui/src/components/Button/Button.tsx`, `Button.test.tsx`, `Button.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `Button as BaseButton`, `buttonVariants` da `@/components/ui/button`
- Produces: `ButtonProps = React.ComponentProps<typeof BaseButton> & { tone?: Tone }`; `Button`; riesporta `buttonVariants`.

- [ ] **Step 1: Test (fallisce)**

`WebUI/packages/ui/src/components/Button/Button.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Button } from "./Button";

describe("Button", () => {
  it("renders a button and fires onClick", async () => {
    const onClick = vi.fn();
    render(<Button onClick={onClick}>Init</Button>);
    await userEvent.click(screen.getByRole("button", { name: "Init" }));
    expect(onClick).toHaveBeenCalledTimes(1);
  });

  it("applies the tone as --tone and uses it for the default variant", () => {
    render(<Button tone="env">Env</Button>);
    const b = screen.getByRole("button");
    expect(b.style.getPropertyValue("--tone")).toBe("var(--color-env)");
    expect(b.className).toContain("bg-(--tone)");
  });

  it("does not recolor non-default variants", () => {
    render(<Button tone="env" variant="ghost">Env</Button>);
    expect(screen.getByRole("button").className).not.toContain("bg-(--tone)");
  });

  it("supports disabled", () => {
    render(<Button disabled>Off</Button>);
    expect(screen.getByRole("button")).toBeDisabled();
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Button` → FAIL.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/components/Button/Button.tsx`:

```tsx
import type { ComponentProps } from "react";
import { Button as BaseButton, buttonVariants } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type ButtonProps = ComponentProps<typeof BaseButton> & {
  /** Colore di sezione: la variante `default` usa `--tone` come sfondo. */
  tone?: Tone;
};

export function Button({ tone, variant = "default", className, style, ...props }: ButtonProps) {
  const toned = tone !== undefined && variant === "default";
  return (
    <BaseButton
      variant={variant}
      style={{ ...toneStyle(tone), ...style }}
      className={cn(toned && "bg-(--tone) text-background hover:bg-(--tone)/80", className)}
      {...props}
    />
  );
}

export { buttonVariants };
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Button` → PASS, 4 test.

- [ ] **Step 3: Story**

`WebUI/packages/ui/src/components/Button/Button.stories.tsx`:

```tsx
import type { Meta, StoryObj } from "@storybook/react-vite";
import { PlayIcon } from "lucide-react";
import { Button } from "./Button";

const meta = {
  title: "Controls/Button",
  component: Button,
  args: { children: "Randomize" },
} satisfies Meta<typeof Button>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Variants: Story = {
  render: (args) => (
    <div className="flex flex-wrap items-center gap-3">
      <Button {...args} variant="default" />
      <Button {...args} variant="secondary" />
      <Button {...args} variant="outline" />
      <Button {...args} variant="ghost" />
      <Button {...args} variant="destructive" />
    </div>
  ),
};
export const Sizes: Story = {
  render: (args) => (
    <div className="flex items-center gap-3">
      <Button {...args} size="xs" />
      <Button {...args} size="sm" />
      <Button {...args} size="default" />
      <Button {...args} size="icon" aria-label="Play"><PlayIcon /></Button>
    </div>
  ),
};
export const Tones: Story = {
  render: (args) => (
    <div className="flex flex-wrap gap-3">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Button key={t} {...args} tone={t}>{t}</Button>
      ))}
    </div>
  ),
};
export const WithIcon: Story = {
  render: (args) => (
    <Button {...args}>
      <PlayIcon data-icon="inline-start" />
      Preview
    </Button>
  ),
};
```

- [ ] **Step 4: Export, verifica, commit**

`src/index.ts`: `export { Button, buttonVariants, type ButtonProps } from "@/components/Button/Button";`

Storybook: varianti e toni visibili. `test`, `typecheck`, `check:colors` puliti.

```bash
git add WebUI/packages/ui/src/components/Button WebUI/packages/ui/src/index.ts
git commit -m "Add Button component with section tone

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: Componente `Toggle`

**Files:**
- Create: `WebUI/packages/ui/src/components/Toggle/Toggle.tsx`, `Toggle.test.tsx`, `Toggle.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `Switch` da `@/components/ui/switch` (prop `checked`, `onCheckedChange(checked, eventDetails)`, `disabled`, `size`)
- Produces: `ToggleProps = { checked: boolean; onChange: (checked: boolean) => void; label?: string; tone?: Tone; disabled?: boolean; className?: string; id?: string }`; `Toggle`.

- [ ] **Step 1: Test (fallisce)**

`WebUI/packages/ui/src/components/Toggle/Toggle.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Toggle } from "./Toggle";

describe("Toggle", () => {
  it("is a switch labelled by its label", () => {
    render(<Toggle checked={false} onChange={() => {}} label="Sync" />);
    expect(screen.getByRole("switch", { name: "Sync" })).toHaveAttribute("aria-checked", "false");
  });

  it("calls onChange with the next state on click", async () => {
    const onChange = vi.fn();
    render(<Toggle checked={false} onChange={onChange} label="Sync" />);
    await userEvent.click(screen.getByRole("switch"));
    expect(onChange).toHaveBeenCalledWith(true);
  });

  it("toggles with Space", async () => {
    const onChange = vi.fn();
    render(<Toggle checked={true} onChange={onChange} label="Sync" />);
    screen.getByRole("switch").focus();
    await userEvent.keyboard(" ");
    expect(onChange).toHaveBeenCalledWith(false);
  });

  it("is disabled", async () => {
    const onChange = vi.fn();
    render(<Toggle checked={false} onChange={onChange} label="Sync" disabled />);
    await userEvent.click(screen.getByRole("switch"));
    expect(onChange).not.toHaveBeenCalled();
  });

  it("sets --tone", () => {
    render(<Toggle checked onChange={() => {}} label="Sync" tone="lfo" />);
    expect(screen.getByTestId("toggle").style.getPropertyValue("--tone")).toBe("var(--color-lfo)");
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Toggle` → FAIL.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/components/Toggle/Toggle.tsx`:

```tsx
import { useId } from "react";
import { Switch } from "@/components/ui/switch";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type ToggleProps = {
  checked: boolean;
  onChange: (checked: boolean) => void;
  label?: string;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
  id?: string;
};

export function Toggle({ checked, onChange, label, tone, disabled = false, className, id }: ToggleProps) {
  const autoId = useId();
  const switchId = id ?? autoId;
  return (
    <div
      data-testid="toggle"
      data-slot="toggle"
      className={cn("inline-flex items-center gap-2", disabled && "opacity-50", className)}
      style={toneStyle(tone)}
    >
      <Switch
        id={switchId}
        size="sm"
        checked={checked}
        onCheckedChange={(next) => onChange(next)}
        disabled={disabled}
        aria-label={label}
        className="data-checked:bg-(--tone) focus-visible:ring-(--tone)/50"
      />
      {label && (
        <label
          htmlFor={switchId}
          className={cn(
            "cursor-pointer text-2xs uppercase tracking-wider select-none",
            checked ? "text-(--tone)" : "text-muted-foreground",
          )}
        >
          {label}
        </label>
      )}
    </div>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Toggle` → PASS, 5 test. (Se `getByRole("switch", { name })` non trova il nome perché Base UI espone `aria-labelledby`, `aria-label` sul `Switch` è comunque passato: verificare nel DOM con `screen.debug()`.)

- [ ] **Step 3: Story**

`WebUI/packages/ui/src/components/Toggle/Toggle.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Toggle, type ToggleProps } from "./Toggle";

const meta = {
  title: "Controls/Toggle",
  component: Toggle,
  args: { checked: true, label: "Sync", onChange: () => {} },
} satisfies Meta<typeof Toggle>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: ToggleProps) {
  const [checked, setChecked] = useState(props.checked);
  return <Toggle {...props} checked={checked} onChange={setChecked} />;
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Off: Story = { args: { checked: false }, render: (args) => <Controlled {...args} /> };
export const Tones: Story = {
  render: (args) => (
    <div className="flex flex-col gap-3">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Controlled key={t} {...args} tone={t} label={t} />
      ))}
    </div>
  ),
};
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
```

- [ ] **Step 4: Export, verifica, commit**

`src/index.ts`: `export { Toggle, type ToggleProps } from "@/components/Toggle/Toggle";`

```bash
git add WebUI/packages/ui/src/components/Toggle WebUI/packages/ui/src/index.ts
git commit -m "Add Toggle component

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: Componente `Select`

**Files:**
- Create: `WebUI/packages/ui/src/components/Select/Select.tsx`, `Select.test.tsx`, `Select.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `Select as SelectRoot, SelectTrigger, SelectValue, SelectContent, SelectGroup, SelectItem` da `@/components/ui/select`. Base UI `Select.Root` accetta `items={[{value,label}]}` per mostrare il label selezionato nel trigger, `value`, `onValueChange(value, eventDetails)`.
- Produces: `SelectOption = { value: string; label: string }`; `SelectProps = { value: string | null; onChange: (v: string) => void; options: SelectOption[]; label?: string; placeholder?: string; disabled?: boolean; className?: string; id?: string }`; `Select`.

- [ ] **Step 1: Test (fallisce)**

`WebUI/packages/ui/src/components/Select/Select.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Select } from "./Select";

const options = [
  { value: "saw", label: "Saw" },
  { value: "square", label: "Square" },
  { value: "sine", label: "Sine" },
];

describe("Select", () => {
  it("renders the selected label in the trigger", () => {
    render(<Select value="square" onChange={() => {}} options={options} label="Waveform" />);
    expect(screen.getByRole("combobox", { name: "Waveform" })).toHaveTextContent("Square");
  });

  it("shows the placeholder when nothing is selected", () => {
    render(<Select value={null} onChange={() => {}} options={options} placeholder="Pick a wave" />);
    expect(screen.getByRole("combobox")).toHaveTextContent("Pick a wave");
  });

  it("opens and selects an option", async () => {
    const onChange = vi.fn();
    render(<Select value="saw" onChange={onChange} options={options} label="Waveform" />);
    await userEvent.click(screen.getByRole("combobox"));
    await userEvent.click(await screen.findByRole("option", { name: "Sine" }));
    expect(onChange).toHaveBeenCalledWith("sine");
  });

  it("is disabled", () => {
    render(<Select value="saw" onChange={() => {}} options={options} disabled />);
    expect(screen.getByRole("combobox")).toBeDisabled();
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Select` → FAIL.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/components/Select/Select.tsx`:

```tsx
import {
  Select as SelectRoot,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { cn } from "@/lib/utils";

export type SelectOption = { value: string; label: string };

export type SelectProps = {
  value: string | null;
  onChange: (value: string) => void;
  options: SelectOption[];
  /** Nome accessibile del trigger. */
  label?: string;
  placeholder?: string;
  disabled?: boolean;
  className?: string;
  id?: string;
};

export function Select({ value, onChange, options, label, placeholder = "Select…", disabled = false, className, id }: SelectProps) {
  return (
    <SelectRoot
      items={options}
      value={value}
      onValueChange={(next) => {
        if (typeof next === "string") onChange(next);
      }}
      disabled={disabled}
    >
      <SelectTrigger id={id} size="sm" aria-label={label} className={cn("w-full font-mono text-xs", className)}>
        <SelectValue placeholder={placeholder} />
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          {options.map((o) => (
            <SelectItem key={o.value} value={o.value} className="font-mono text-xs">
              {o.label}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </SelectRoot>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Select` → PASS, 4 test. Se il test "opens and selects" fallisce perché il popup Base UI non si apre in jsdom (nessun `option` trovato), sostituirlo con la variante tastiera: focus sul trigger, `await userEvent.keyboard("{ArrowDown}")`, poi `findByRole("option")`. Se anche quella fallisce, ridurre il test a `expect(screen.getByRole("combobox")).toHaveAttribute("aria-haspopup")` e annotare in `.design-sync/NOTES.md` (verrà creato da design-sync) che la selezione di Select va verificata in Storybook.

- [ ] **Step 3: Story**

`WebUI/packages/ui/src/components/Select/Select.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Select, type SelectProps } from "./Select";

const options = [
  { value: "basic", label: "Basic Shapes" },
  { value: "analog", label: "Analog Classics" },
  { value: "vocal", label: "Vocal Formants" },
  { value: "digital", label: "Digital Harsh" },
];

const meta = {
  title: "Controls/Select",
  component: Select,
  args: { value: "analog", options, label: "Wavetable", onChange: () => {} },
} satisfies Meta<typeof Select>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: SelectProps) {
  const [value, setValue] = useState(props.value);
  return (
    <div className="w-48">
      <Select {...props} value={value} onChange={setValue} />
    </div>
  );
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Empty: Story = { args: { value: null, placeholder: "Choose table" }, render: (args) => <Controlled {...args} /> };
export const Disabled: Story = { args: { disabled: true }, render: (args) => <Controlled {...args} /> };
```

- [ ] **Step 4: Export, verifica, commit**

`src/index.ts`: `export { Select, type SelectOption, type SelectProps } from "@/components/Select/Select";`

Storybook: popup con `bg-popover`, item selezionato con check, tastiera funzionante.

```bash
git add WebUI/packages/ui/src/components/Select WebUI/packages/ui/src/index.ts
git commit -m "Add Select component

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: Componente `Tabs`

**Files:**
- Create: `WebUI/packages/ui/src/components/Tabs/Tabs.tsx`, `Tabs.test.tsx`, `Tabs.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `Tabs as TabsRoot, TabsList, TabsTrigger` da `@/components/ui/tabs` (`value`, `onValueChange(value, eventDetails)`, `TabsList variant="line"`).
- Produces: `TabItem = { value: string; label: string }`; `TabsProps = { value: string; onChange: (v: string) => void; items: TabItem[]; tone?: Tone; className?: string }`; `Tabs`.

- [ ] **Step 1: Test (fallisce)**

`WebUI/packages/ui/src/components/Tabs/Tabs.test.tsx`:

```tsx
import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Tabs } from "./Tabs";

const items = [
  { value: "osc", label: "OSC" },
  { value: "filter", label: "FILTER" },
  { value: "env", label: "ENV" },
];

describe("Tabs", () => {
  it("renders a tablist with the active tab selected", () => {
    render(<Tabs value="filter" onChange={() => {}} items={items} />);
    expect(screen.getByRole("tablist")).toBeInTheDocument();
    expect(screen.getByRole("tab", { name: "FILTER" })).toHaveAttribute("aria-selected", "true");
    expect(screen.getByRole("tab", { name: "OSC" })).toHaveAttribute("aria-selected", "false");
  });

  it("calls onChange on click", async () => {
    const onChange = vi.fn();
    render(<Tabs value="osc" onChange={onChange} items={items} />);
    await userEvent.click(screen.getByRole("tab", { name: "ENV" }));
    expect(onChange).toHaveBeenCalledWith("env");
  });

  it("moves with arrow keys", async () => {
    const onChange = vi.fn();
    render(<Tabs value="osc" onChange={onChange} items={items} />);
    screen.getByRole("tab", { name: "OSC" }).focus();
    await userEvent.keyboard("{ArrowRight}");
    expect(onChange).toHaveBeenCalledWith("filter");
  });

  it("sets --tone", () => {
    render(<Tabs value="osc" onChange={() => {}} items={items} tone="osc" />);
    expect(screen.getByTestId("tabs").style.getPropertyValue("--tone")).toBe("var(--color-osc)");
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Tabs` → FAIL.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/components/Tabs/Tabs.tsx`:

```tsx
import { Tabs as TabsRoot, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type TabItem = { value: string; label: string };

export type TabsProps = {
  value: string;
  onChange: (value: string) => void;
  items: TabItem[];
  tone?: Tone;
  className?: string;
};

/** Solo la barra dei tab: il contenuto attivo lo renderizza il consumer in base a `value`. */
export function Tabs({ value, onChange, items, tone, className }: TabsProps) {
  return (
    <TabsRoot
      data-testid="tabs"
      value={value}
      onValueChange={(next) => onChange(String(next))}
      className={className}
      style={toneStyle(tone)}
    >
      <TabsList variant="line" className="h-7 gap-3 p-0">
        {items.map((item) => (
          <TabsTrigger
            key={item.value}
            value={item.value}
            className={cn(
              "px-1 text-2xs font-medium tracking-wider uppercase",
              "data-active:text-(--tone) after:bg-(--tone)",
            )}
          >
            {item.label}
          </TabsTrigger>
        ))}
      </TabsList>
    </TabsRoot>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Tabs` → PASS, 4 test. Se `data-testid` non arriva al DOM attraverso `TabsRoot`, avvolgere in un `<div data-testid="tabs" style={toneStyle(tone)}>` e spostare lì lo stile.

- [ ] **Step 3: Story**

`WebUI/packages/ui/src/components/Tabs/Tabs.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Tabs, type TabsProps } from "./Tabs";

const items = [
  { value: "osc", label: "OSC A" },
  { value: "osc-b", label: "OSC B" },
  { value: "noise", label: "Noise" },
  { value: "sub", label: "Sub" },
];

const meta = {
  title: "Layout/Tabs",
  component: Tabs,
  args: { value: "osc", items, onChange: () => {} },
} satisfies Meta<typeof Tabs>;
export default meta;
type Story = StoryObj<typeof meta>;

function Controlled(props: TabsProps) {
  const [value, setValue] = useState(props.value);
  return (
    <div className="flex flex-col gap-3">
      <Tabs {...props} value={value} onChange={setValue} />
      <p className="font-mono text-xs text-muted-foreground">active: {value}</p>
    </div>
  );
}

export const Default: Story = { render: (args) => <Controlled {...args} /> };
export const Toned: Story = { args: { tone: "filter" }, render: (args) => <Controlled {...args} /> };
```

- [ ] **Step 4: Export, verifica, commit**

`src/index.ts`: `export { Tabs, type TabItem, type TabsProps } from "@/components/Tabs/Tabs";`

```bash
git add WebUI/packages/ui/src/components/Tabs WebUI/packages/ui/src/index.ts
git commit -m "Add Tabs component

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 10: Componente `ValueReadout`

**Files:**
- Create: `WebUI/packages/ui/src/components/ValueReadout/ValueReadout.tsx`, `ValueReadout.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `Badge` da `@/components/ui/badge`
- Produces: `ValueReadoutProps = { value: string; label?: string; mono?: boolean; className?: string }`; `ValueReadout`.

- [ ] **Step 1: Implementazione (solo presentazionale, nessun test)**

`WebUI/packages/ui/src/components/ValueReadout/ValueReadout.tsx`:

```tsx
import { Badge } from "@/components/ui/badge";
import { cn } from "@/lib/utils";

export type ValueReadoutProps = {
  value: string;
  label?: string;
  /** Font mono con cifre tabulari. Default true. */
  mono?: boolean;
  className?: string;
};

export function ValueReadout({ value, label, mono = true, className }: ValueReadoutProps) {
  return (
    <Badge
      variant="secondary"
      data-slot="value-readout"
      className={cn("h-5 gap-1.5 rounded-sm bg-surface-2 px-1.5 text-foreground", mono && "font-mono tabular-nums", className)}
    >
      {label && <span className="text-2xs tracking-wider text-muted-foreground uppercase">{label}</span>}
      <span>{value}</span>
    </Badge>
  );
}
```

- [ ] **Step 2: Story**

`WebUI/packages/ui/src/components/ValueReadout/ValueReadout.stories.tsx`:

```tsx
import type { Meta, StoryObj } from "@storybook/react-vite";
import { ValueReadout } from "./ValueReadout";

const meta = {
  title: "Display/ValueReadout",
  component: ValueReadout,
  args: { value: "440.00 Hz" },
} satisfies Meta<typeof ValueReadout>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Labelled: Story = { args: { label: "cutoff", value: "2.4 kHz" } };
export const Proportional: Story = { args: { value: "Sine", mono: false } };
```

- [ ] **Step 3: Export, verifica, commit**

`src/index.ts`: `export { ValueReadout, type ValueReadoutProps } from "@/components/ValueReadout/ValueReadout";`

`typecheck`, `check:colors`, Storybook ok.

```bash
git add WebUI/packages/ui/src/components/ValueReadout WebUI/packages/ui/src/index.ts
git commit -m "Add ValueReadout component

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 11: Componenti `Panel` e `SectionHeader`

**Files:**
- Create: `WebUI/packages/ui/src/components/SectionHeader/SectionHeader.tsx`, `SectionHeader.stories.tsx`
- Create: `WebUI/packages/ui/src/components/Panel/Panel.tsx`, `Panel.test.tsx`, `Panel.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `Card, CardContent` da `@/components/ui/card`; `Separator` da `@/components/ui/separator`; `Knob` (solo nella story).
- Produces:
  - `SectionHeaderProps = { title: string; tone?: Tone; actions?: ReactNode; className?: string }`; `SectionHeader`.
  - `PanelProps = { title?: string; tone?: Tone; actions?: ReactNode; children: ReactNode; className?: string }`; `Panel`.

- [ ] **Step 1: SectionHeader**

`WebUI/packages/ui/src/components/SectionHeader/SectionHeader.tsx`:

```tsx
import type { ReactNode } from "react";
import { Separator } from "@/components/ui/separator";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type SectionHeaderProps = {
  title: string;
  tone?: Tone;
  /** Slot a destra (Toggle, Select, Button…). */
  actions?: ReactNode;
  className?: string;
};

export function SectionHeader({ title, tone, actions, className }: SectionHeaderProps) {
  return (
    <div data-slot="section-header" className={cn("flex items-center gap-2", className)} style={toneStyle(tone)}>
      <span className="text-2xs font-semibold tracking-widest text-(--tone) uppercase">{title}</span>
      <Separator className="flex-1 bg-border" />
      {actions && <div className="flex items-center gap-2">{actions}</div>}
    </div>
  );
}
```

`WebUI/packages/ui/src/components/SectionHeader/SectionHeader.stories.tsx`:

```tsx
import type { Meta, StoryObj } from "@storybook/react-vite";
import { SectionHeader } from "./SectionHeader";
import { Toggle } from "@/components/Toggle/Toggle";

const meta = {
  title: "Layout/SectionHeader",
  component: SectionHeader,
  args: { title: "Filter", tone: "filter" },
  decorators: [(Story) => <div className="w-80"><Story /></div>],
} satisfies Meta<typeof SectionHeader>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const WithActions: Story = {
  args: { title: "LFO 1", tone: "lfo", actions: <Toggle checked onChange={() => {}} label="sync" /> },
};
```

- [ ] **Step 2: Test del Panel (fallisce)**

`WebUI/packages/ui/src/components/Panel/Panel.test.tsx`:

```tsx
import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import { Panel } from "./Panel";

describe("Panel", () => {
  it("renders title and children", () => {
    render(<Panel title="Oscillator"><span>body</span></Panel>);
    expect(screen.getByText("Oscillator")).toBeInTheDocument();
    expect(screen.getByText("body")).toBeInTheDocument();
  });

  it("sets --tone on the root so children inherit it", () => {
    render(<Panel title="Filter" tone="filter">x</Panel>);
    expect(screen.getByTestId("panel").style.getPropertyValue("--tone")).toBe("var(--color-filter)");
  });

  it("leaves --tone unset without a tone prop", () => {
    render(<Panel>x</Panel>);
    expect(screen.getByTestId("panel").style.getPropertyValue("--tone")).toBe("");
  });

  it("renders actions in the header", () => {
    render(<Panel title="Env" actions={<button>reset</button>}>x</Panel>);
    expect(screen.getByRole("button", { name: "reset" })).toBeInTheDocument();
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Panel` → FAIL.

- [ ] **Step 3: Implementazione Panel**

`WebUI/packages/ui/src/components/Panel/Panel.tsx`:

```tsx
import type { ReactNode } from "react";
import { Card, CardContent } from "@/components/ui/card";
import { SectionHeader } from "@/components/SectionHeader/SectionHeader";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type PanelProps = {
  title?: string;
  /** Colore di sezione: bordo superiore e `--tone` ereditata da tutti i figli. */
  tone?: Tone;
  actions?: ReactNode;
  children: ReactNode;
  className?: string;
};

export function Panel({ title, tone, actions, children, className }: PanelProps) {
  return (
    <Card
      data-testid="panel"
      data-slot="panel"
      size="sm"
      className={cn(
        "gap-2 rounded-lg border-t-2 border-t-(--tone) bg-surface-1 py-2 ring-border",
        className,
      )}
      style={toneStyle(tone)}
    >
      {title && (
        <div className="px-3">
          <SectionHeader title={title} actions={actions} />
        </div>
      )}
      <CardContent className="flex flex-col gap-3 px-3">{children}</CardContent>
    </Card>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- Panel` → PASS, 4 test.

- [ ] **Step 4: Story Panel (composizione reale)**

`WebUI/packages/ui/src/components/Panel/Panel.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Panel } from "./Panel";
import { Knob } from "@/components/Knob/Knob";
import { Toggle } from "@/components/Toggle/Toggle";
import { Select } from "@/components/Select/Select";

const meta = {
  title: "Layout/Panel",
  component: Panel,
  args: { title: "Filter", tone: "filter", children: null },
} satisfies Meta<typeof Panel>;
export default meta;
type Story = StoryObj<typeof meta>;

function FilterPanel(args: Story["args"]) {
  const [cutoff, setCutoff] = useState(0.6);
  const [res, setRes] = useState(0.2);
  const [drive, setDrive] = useState(0.1);
  const [on, setOn] = useState(true);
  const [type, setType] = useState<string | null>("lp24");
  return (
    <Panel {...args} actions={<Toggle checked={on} onChange={setOn} label="on" />}>
      <Select
        value={type}
        onChange={setType}
        options={[
          { value: "lp24", label: "LP 24" },
          { value: "lp12", label: "LP 12" },
          { value: "hp12", label: "HP 12" },
        ]}
        label="Filter type"
      />
      <div className="flex gap-4">
        <Knob value={cutoff} onChange={setCutoff} label="Cutoff" format={(v) => `${Math.round(20 * Math.pow(1000, v))} Hz`} />
        <Knob value={res} onChange={setRes} label="Res" />
        <Knob value={drive} onChange={setDrive} label="Drive" size="sm" />
      </div>
    </Panel>
  );
}

export const Filter: Story = { render: (args) => <div className="w-72"><FilterPanel {...args} /></div> };
export const Tones: Story = {
  render: (args) => (
    <div className="grid grid-cols-3 gap-3">
      {(["osc", "filter", "env", "lfo", "fx", "master"] as const).map((t) => (
        <Panel key={t} {...args} title={t} tone={t}>
          <Knob value={0.5} onChange={() => {}} label="amt" size="sm" />
        </Panel>
      ))}
    </div>
  ),
};
export const Untitled: Story = { args: { title: undefined }, render: (args) => <Panel {...args}><span className="text-xs">no header</span></Panel> };
```

- [ ] **Step 5: Export, verifica, commit**

`src/index.ts`:

```ts
export { SectionHeader, type SectionHeaderProps } from "@/components/SectionHeader/SectionHeader";
export { Panel, type PanelProps } from "@/components/Panel/Panel";
```

Storybook: nella story `Tones` ogni knob eredita il colore del proprio Panel senza prop `tone`. `test`, `typecheck`, `check:colors` puliti.

```bash
git add WebUI/packages/ui/src/components/Panel WebUI/packages/ui/src/components/SectionHeader WebUI/packages/ui/src/index.ts
git commit -m "Add Panel and SectionHeader components

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 12: Componente `WavetableDisplay`

**Files:**
- Create: `WebUI/packages/ui/src/components/WavetableDisplay/WavetableDisplay.tsx`, `WavetableDisplay.test.tsx`, `WavetableDisplay.stories.tsx`
- Modify: `WebUI/packages/ui/src/index.ts`

**Interfaces:**
- Consumes: `toneStyle`, `cn`
- Produces: `WavetableDisplayProps = { frames: Float32Array[]; position: number; tone?: Tone; className?: string }`; `WavetableDisplay`; helper esportato `frameIndex(position: number, count: number): number`.

- [ ] **Step 1: Test (fallisce)**

`WebUI/packages/ui/src/components/WavetableDisplay/WavetableDisplay.test.tsx`:

```tsx
import { beforeEach, describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { frameIndex, WavetableDisplay } from "./WavetableDisplay";

const sine = (n = 64) => Float32Array.from({ length: n }, (_, i) => Math.sin((i / n) * Math.PI * 2));

describe("frameIndex", () => {
  it("maps 0..1 to the nearest frame", () => {
    expect(frameIndex(0, 8)).toBe(0);
    expect(frameIndex(1, 8)).toBe(7);
    expect(frameIndex(0.5, 8)).toBe(4);
    expect(frameIndex(0.5, 0)).toBe(0);
  });
});

describe("WavetableDisplay", () => {
  const ctx = {
    clearRect: vi.fn(), beginPath: vi.fn(), moveTo: vi.fn(), lineTo: vi.fn(), stroke: vi.fn(),
    setTransform: vi.fn(), scale: vi.fn(), save: vi.fn(), restore: vi.fn(),
    lineWidth: 0, strokeStyle: "", globalAlpha: 1,
  };

  beforeEach(() => {
    vi.spyOn(HTMLCanvasElement.prototype, "getContext").mockReturnValue(ctx as unknown as CanvasRenderingContext2D);
    ctx.stroke.mockClear();
  });

  it("renders a canvas with an accessible label", () => {
    render(<WavetableDisplay frames={[sine()]} position={0} />);
    expect(screen.getByRole("img", { name: "Wavetable" })).toBeInstanceOf(HTMLCanvasElement);
  });

  it("draws a flat line with no frames", () => {
    render(<WavetableDisplay frames={[]} position={0} />);
    expect(ctx.stroke).toHaveBeenCalled();
  });

  it("draws the current frame plus neighbours", () => {
    render(<WavetableDisplay frames={[sine(), sine(), sine(), sine(), sine()]} position={0.5} />);
    // frame corrente + 2 vicini per lato = 5 stroke
    expect(ctx.stroke).toHaveBeenCalledTimes(5);
  });

  it("sets --tone", () => {
    render(<WavetableDisplay frames={[sine()]} position={0} tone="osc" />);
    expect(screen.getByTestId("wavetable").style.getPropertyValue("--tone")).toBe("var(--color-osc)");
  });
});
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- WavetableDisplay` → FAIL.

- [ ] **Step 2: Implementazione**

`WebUI/packages/ui/src/components/WavetableDisplay/WavetableDisplay.tsx`:

```tsx
import { useEffect, useRef } from "react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type WavetableDisplayProps = {
  /** Frame della wavetable, ciascuno campioni in -1..1. */
  frames: Float32Array[];
  /** Posizione 0..1 nella tabella. */
  position: number;
  tone?: Tone;
  className?: string;
};

const NEIGHBOURS = 3;

export function frameIndex(position: number, count: number): number {
  if (count <= 0) return 0;
  const p = Math.min(1, Math.max(0, position));
  return Math.round(p * (count - 1));
}

function drawFrame(
  ctx: CanvasRenderingContext2D,
  frame: Float32Array | null,
  w: number,
  h: number,
  offset: number,
  alpha: number,
  color: string,
) {
  const inset = 6;
  const midY = h / 2 - offset * 4;
  const amp = (h / 2 - inset) * (1 - offset * 0.12);
  ctx.beginPath();
  if (!frame || frame.length === 0) {
    ctx.moveTo(inset, midY);
    ctx.lineTo(w - inset, midY);
  } else {
    const n = frame.length;
    for (let i = 0; i < n; i++) {
      const x = inset + offset * 6 + ((w - inset * 2 - offset * 12) * i) / (n - 1);
      const y = midY - (frame[i] ?? 0) * amp;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
  }
  ctx.globalAlpha = alpha;
  ctx.strokeStyle = color;
  ctx.lineWidth = offset === 0 ? 2 : 1;
  ctx.stroke();
}

export function WavetableDisplay({ frames, position, tone, className }: WavetableDisplayProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const draw = () => {
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      const dpr = window.devicePixelRatio || 1;
      const w = canvas.clientWidth || 240;
      const h = canvas.clientHeight || 96;
      canvas.width = Math.round(w * dpr);
      canvas.height = Math.round(h * dpr);
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.clearRect(0, 0, w, h);

      const color = getComputedStyle(canvas).getPropertyValue("--tone").trim() || "currentColor";
      const current = frameIndex(position, frames.length);

      if (frames.length === 0) {
        drawFrame(ctx, null, w, h, 0, 0.6, color);
        return;
      }
      // Vicini più lontani prima (dietro), poi il corrente sopra.
      for (let d = NEIGHBOURS; d >= 1; d--) {
        const back = current - d;
        const fwd = current + d;
        if (fwd < frames.length) drawFrame(ctx, frames[fwd] ?? null, w, h, d, 0.35 / d, color);
        if (back >= 0) drawFrame(ctx, frames[back] ?? null, w, h, -d, 0.35 / d, color);
      }
      drawFrame(ctx, frames[current] ?? null, w, h, 0, 1, color);
    };

    draw();
    const ro = new ResizeObserver(draw);
    ro.observe(canvas);
    return () => ro.disconnect();
  }, [frames, position]);

  return (
    <div
      data-testid="wavetable"
      data-slot="wavetable-display"
      className={cn("relative h-24 w-full overflow-hidden rounded-md bg-surface-0 ring-1 ring-border", className)}
      style={toneStyle(tone)}
    >
      <canvas ref={canvasRef} role="img" aria-label="Wavetable" className="size-full text-(--tone)" />
    </div>
  );
}
```

Run: `cd WebUI && pnpm --filter @xerum/ui test -- WavetableDisplay` → PASS, 5 test. Nota sul conteggio: con 5 frame e `position=0.5` il corrente è l'indice 2; i vicini a distanza 1 e 2 esistono su entrambi i lati, quelli a distanza 3 no → 1 + 4 = 5 stroke. Se jsdom non fornisce `clientWidth`, il fallback `|| 240` copre il test. Se `getComputedStyle(...).getPropertyValue("--tone")` lancia in jsdom, avvolgere in try/catch e usare `"currentColor"`.

- [ ] **Step 3: Story**

`WebUI/packages/ui/src/components/WavetableDisplay/WavetableDisplay.stories.tsx`:

```tsx
import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { WavetableDisplay } from "./WavetableDisplay";
import { Knob } from "@/components/Knob/Knob";

const N = 128;
const FRAMES = 16;
/** Morph da sine a saw: frame i = mix. */
const frames = Array.from({ length: FRAMES }, (_, f) => {
  const mix = f / (FRAMES - 1);
  return Float32Array.from({ length: N }, (_, i) => {
    const t = i / N;
    const sine = Math.sin(t * Math.PI * 2);
    const saw = 2 * t - 1;
    return sine * (1 - mix) + saw * mix;
  });
});

const meta = {
  title: "Display/WavetableDisplay",
  component: WavetableDisplay,
  args: { frames, position: 0.3, tone: "osc" },
  decorators: [(Story) => <div className="w-80"><Story /></div>],
} satisfies Meta<typeof WavetableDisplay>;
export default meta;
type Story = StoryObj<typeof meta>;

export const Default: Story = {};
export const Empty: Story = { args: { frames: [] } };
export const WithPositionKnob: Story = {
  render: (args) => {
    const [pos, setPos] = useState(args.position);
    return (
      <div className="flex items-center gap-4">
        <WavetableDisplay {...args} position={pos} />
        <Knob value={pos} onChange={setPos} label="WT pos" />
      </div>
    );
  },
};
```

- [ ] **Step 4: Export, verifica, commit**

`src/index.ts`: `export { WavetableDisplay, frameIndex, type WavetableDisplayProps } from "@/components/WavetableDisplay/WavetableDisplay";`

Storybook: forma d'onda in colore tono, vicini sfumati dietro, il knob la fa scorrere. `test`, `typecheck`, `check:colors` puliti.

```bash
git add WebUI/packages/ui/src/components/WavetableDisplay WebUI/packages/ui/src/index.ts
git commit -m "Add WavetableDisplay canvas component

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 13: Barrel finale, app shell su `@xerum/ui`, documentazione

**Files:**
- Modify: `WebUI/packages/ui/src/index.ts` (riesporti Tooltip/Separator/Label)
- Modify: `WebUI/vite.config.ts`, `WebUI/src/styles.css`, `WebUI/src/App.tsx`
- Modify: `README.md`, `docs/architecture.md`, `docs/build.md`

**Interfaces:**
- Consumes: tutto il barrel.
- Produces: `@xerum/ui` esporta anche `Tooltip, TooltipTrigger, TooltipContent, TooltipProvider, Separator, Label`. App shell che renderizza i componenti reali.

- [ ] **Step 1: Barrel definitivo**

`WebUI/packages/ui/src/index.ts` (contenuto completo):

```ts
import "./index.css";

export { cn } from "@/lib/utils";
export { TONES, toneStyle, type Tone } from "@/lib/tone";
export { useDragValue, type UseDragValueOptions, type UseDragValueResult } from "@/hooks/useDragValue";

export { Knob, type KnobProps } from "@/components/Knob/Knob";
export { Fader, type FaderProps } from "@/components/Fader/Fader";
export { Button, buttonVariants, type ButtonProps } from "@/components/Button/Button";
export { Toggle, type ToggleProps } from "@/components/Toggle/Toggle";
export { Select, type SelectOption, type SelectProps } from "@/components/Select/Select";
export { Tabs, type TabItem, type TabsProps } from "@/components/Tabs/Tabs";
export { ValueReadout, type ValueReadoutProps } from "@/components/ValueReadout/ValueReadout";
export { SectionHeader, type SectionHeaderProps } from "@/components/SectionHeader/SectionHeader";
export { Panel, type PanelProps } from "@/components/Panel/Panel";
export { WavetableDisplay, frameIndex, type WavetableDisplayProps } from "@/components/WavetableDisplay/WavetableDisplay";

// Primitive shadcn riesportate as-is
export { Tooltip, TooltipContent, TooltipProvider, TooltipTrigger } from "@/components/ui/tooltip";
export { Separator } from "@/components/ui/separator";
export { Label } from "@/components/ui/label";
```

Run: `cd WebUI && pnpm --filter @xerum/ui build` → `dist/index.d.ts` elenca tutti gli export; nessun errore.

- [ ] **Step 2: App shell — Vite e CSS**

`WebUI/vite.config.ts`:

```ts
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
```

`WebUI/src/styles.css` (sostituisce il contenuto attuale):

```css
@import "tailwindcss";
@import "@xerum/ui/theme.css";
@import "@xerum/ui/ui.css";

html,
body,
#root {
  margin: 0;
  min-height: 100%;
  background: radial-gradient(1200px 600px at 20% -10%, #1a2438 0%, var(--background) 55%);
  color: var(--foreground);
  font-family: var(--font-sans);
}
```

- [ ] **Step 3: App shell — demo con componenti reali**

`WebUI/src/App.tsx`:

```tsx
import { useState } from "react";
import {
  Button,
  Fader,
  Knob,
  Panel,
  Select,
  Tabs,
  Toggle,
  ValueReadout,
  WavetableDisplay,
} from "@xerum/ui";

const N = 128;
const frames = Array.from({ length: 16 }, (_, f) => {
  const mix = f / 15;
  return Float32Array.from({ length: N }, (_, i) => {
    const t = i / N;
    return Math.sin(t * Math.PI * 2) * (1 - mix) + (2 * t - 1) * mix;
  });
});

const hz = (v: number) => `${Math.round(20 * Math.pow(1000, v))} Hz`;
const pct = (v: number) => `${Math.round(v * 100)}%`;

export default function App() {
  const [osc, setOsc] = useState("a");
  const [wtPos, setWtPos] = useState(0.3);
  const [oscLevel, setOscLevel] = useState(0.8);
  const [table, setTable] = useState<string | null>("basic");
  const [cutoff, setCutoff] = useState(0.6);
  const [res, setRes] = useState(0.2);
  const [filterOn, setFilterOn] = useState(true);
  const [attack, setAttack] = useState(0.05);
  const [decay, setDecay] = useState(0.3);
  const [sustain, setSustain] = useState(0.7);
  const [release, setRelease] = useState(0.4);
  const [master, setMaster] = useState(0.75);

  return (
    <main className="flex min-h-screen flex-col gap-4 p-6">
      <header className="flex items-center justify-between">
        <div className="flex flex-col">
          <span className="text-2xs tracking-widest text-primary uppercase">SerumStyleSynth · Phase 1</span>
          <h1 className="text-xl font-semibold">Wavetable synth scaffold</h1>
        </div>
        <div className="flex items-center gap-3">
          <ValueReadout label="bridge" value="stub" />
          <Button variant="outline" size="sm">Init</Button>
        </div>
      </header>

      <div className="grid grid-cols-1 gap-4 md:grid-cols-3">
        <Panel title="Oscillator" tone="osc" actions={<Tabs value={osc} onChange={setOsc} items={[{ value: "a", label: "A" }, { value: "b", label: "B" }]} />}>
          <Select value={table} onChange={setTable} label="Wavetable" options={[{ value: "basic", label: "Basic Shapes" }, { value: "analog", label: "Analog Classics" }]} />
          <WavetableDisplay frames={frames} position={wtPos} />
          <div className="flex gap-4">
            <Knob value={wtPos} onChange={setWtPos} label="WT pos" />
            <Knob value={oscLevel} onChange={setOscLevel} label="Level" format={pct} />
          </div>
        </Panel>

        <Panel title="Filter" tone="filter" actions={<Toggle checked={filterOn} onChange={setFilterOn} label="on" />}>
          <div className="flex gap-4">
            <Knob value={cutoff} onChange={setCutoff} label="Cutoff" format={hz} size="lg" disabled={!filterOn} />
            <Knob value={res} onChange={setRes} label="Res" disabled={!filterOn} />
          </div>
        </Panel>

        <Panel title="Amp Env" tone="env">
          <div className="flex gap-4">
            <Fader value={attack} onChange={setAttack} label="A" />
            <Fader value={decay} onChange={setDecay} label="D" />
            <Fader value={sustain} onChange={setSustain} label="S" />
            <Fader value={release} onChange={setRelease} label="R" />
          </div>
        </Panel>
      </div>

      <Panel title="Master" tone="master" className="md:w-64">
        <Fader value={master} onChange={setMaster} label="Out" orientation="horizontal" format={(v) => `${(v * 12 - 6).toFixed(1)} dB`} defaultValue={0.5} />
      </Panel>
    </main>
  );
}
```

Run:

```bash
cd WebUI && pnpm install && pnpm --filter @xerum/ui build && pnpm build
```

Atteso: `tsc --noEmit` pulito nella app, `dist/` della app generato. Poi `pnpm dev` e apri `http://localhost:5173`: tre pannelli con i colori di sezione, i knob ereditano il tono, fader ADSR funzionanti, master orizzontale. Chiudi il server.

- [ ] **Step 4: Documentazione**

In `docs/architecture.md`, sostituisci la riga della tabella `| Web UI | WebUI/ | React + Vite (dev server → WebView) |` con due righe:

```
| Web UI | `WebUI/` | App shell React + Vite (dev server → WebView); consuma `@xerum/ui` |
| UI library | `WebUI/packages/ui/` | `@xerum/ui`: componenti synth (Tailwind v4, shadcn base-nova), Storybook, test |
```

In `docs/build.md` aggiungi in fondo:

```
## Web UI

Requires pnpm 11 (`corepack enable`).

    cd WebUI
    pnpm install
    pnpm ui:build        # @xerum/ui -> packages/ui/dist
    pnpm dev             # app shell on http://localhost:5173
    pnpm ui:storybook    # component catalogue on http://localhost:6006
    pnpm ui:test         # Vitest
```

In `README.md` aggiungi al fondo dell'elenco: `- UI library spec: [docs/superpowers/specs/2026-09-17-ui-library-design.md](docs/superpowers/specs/2026-09-17-ui-library-design.md)`.

- [ ] **Step 5: Verifica finale e commit**

```bash
cd WebUI
pnpm --filter @xerum/ui test
pnpm --filter @xerum/ui typecheck
pnpm --filter @xerum/ui check:colors
pnpm --filter @xerum/ui build-storybook
pnpm build
```

Tutti puliti. Poi:

```bash
cd /Users/andrea/Documents/personal/xerum
git add WebUI/packages/ui/src/index.ts WebUI/vite.config.ts WebUI/src/styles.css WebUI/src/App.tsx WebUI/pnpm-lock.yaml README.md docs/architecture.md docs/build.md
git commit -m "Consume @xerum/ui from the WebUI shell and document the library

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

## Dopo il piano

Con tutti i task chiusi, lanciare `/design-sync` dalla root del repo: shape `storybook`, `storybookConfigDir = WebUI/packages/ui/.storybook`, package `WebUI/packages/ui`. La libreria ha `dist/` compilato, Storybook con una story file per componente e token in `@theme static` visibili nel CSS.
