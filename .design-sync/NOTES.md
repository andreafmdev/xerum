# design-sync notes — @xerum/ui

- Package: `WebUI/packages/ui` (pnpm workspace root `WebUI/`). Build: `cd WebUI && pnpm --filter @xerum/ui build` → `dist/index.js` (ESM, react/react-dom external) + `dist/ui.css` (tokens + utilities + fonts inline base64, scoped base reset, explicit `@layer theme, base, components, utilities;`).
- Storybook 10 (`@storybook/react-vite`) in `WebUI/packages/ui/.storybook`; `preview.tsx` wraps stories in `<div class="font-sans text-foreground">` because Storybook's preview CSS forces Nunito Sans on body.
- `Foundations/Tokens` is a swatch story, not a component → `titleMap.Tokens = null`.
- Fonts (IBM Plex Sans 400/500/600, Mono 400) are inlined as base64 in `ui.css`; no `fonts/` dir expected.
- Port 5173 is often taken on this machine by another project; dev app runs on 5174 (`pnpm exec vite --port 5174`).

## Learnings (2026-09-18, first sync)
- [GENERAL] The emitted preview card hardcodes `body{background:#fff}` (app contract). A dark-only DS must paint its own canvas: `index.css` has an unlayered `html body { background-color: var(--background); color: var(--foreground) }` (unlayered on purpose so it beats the card's inline body rule; also fixes Storybook's body font). Without it every card and every design renders light text on white.
- `! preview decorator bundle failed: Could not resolve "tw-animate-css"` — the `.storybook/preview.tsx` import graph pulls `../src/index.css` → `tw-animate-css` (CSS package) which the decorator bundler can't resolve. Harmless: the decorator only adds `font-sans text-foreground`, which `ui.css` already applies via `html body`/`html`. No `cfg.provider` needed (there is no provider component).
- `[GRID_OVERFLOW]` wide: Button (Sizes) and Knob (Tones) → `overrides.{Button,Knob}.cardMode = "column"`.
- Tailwind `@theme inline` values (`--font-sans`, `--font-mono`, semantic `--color-*` aliases) are NOT emitted as CSS variables in `ui.css`; the `:root` values (`--background`, `--primary`, …) and the `@theme static` extensions (`--color-surface-*`, `--color-osc`…, `--shadow-*`, `--spacing-knob-*`) are. conventions.md lists only what exists.
- `--font-*` don't exist as vars → conventions tell the design agent to use `font-sans`/`font-mono` classes.
- The README's auto token summary is noisy (Tailwind `--tw-*` internals); the conventions header carries the real vocabulary.
- All 10 components graded `match` on the first full compare (after the canvas fix). Framing differs by design (Storybook `layout: centered` shrink-wraps; previews are container-width) — not a delta.

## Re-sync risks
- `.design-sync/sb-reference` must be rebuilt whenever `WebUI/packages/ui/src` changes (stories or CSS): `cd WebUI/packages/ui && pnpm exec storybook build -c .storybook -o <repo>/.design-sync/sb-reference`.
- The canvas rule and the `@layer theme, base, components, utilities;` statement in `index.css` are load-bearing for standalone `ui.css`; a refactor that drops either silently breaks Claude Design rendering while the app (which imports full Tailwind) still looks fine. Verify with a Storybook probe (button computed background, body font).
- conventions.md enumerates compiled utility classes; adding/removing utilities in stories/components changes what exists in `ui.css` — re-validate the list (`grep '\.<class>{' ds-bundle/_ds_bundle.css`) after UI changes.
- Fonts are inlined base64 in `ui.css` (Vite lib mode); `[FONT_MISSING]` never fired. If the font setup moves to files, `fonts/` handling changes.
- Toolchain: converter deps in `.ds-sync/` (esbuild, ts-morph, playwright 1.5x with chromium-1200 in `~/Library/Caches/ms-playwright`). pnpm 11 / Node 24.
- No remote assets in any story (no `[ASSETS_BLOCKED]` exposure).
