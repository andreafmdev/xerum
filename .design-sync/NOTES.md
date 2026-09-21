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

## 2026-09-18 — SynthWindow implemented from the design
- The Claude Design template `templates/synth-window/SynthWindow.dc.html` (prototype with its own `sx-*` primitives) was implemented in `WebUI/src/synth/` on top of `@xerum/ui`.
- Library extended to cover what the prototype needed: `Knob` (`mods`, `liveValue`, `onDropMod`, `hideValue`, glow via `--glow`), `Tabs` (`variant="bar"`, per-item `tone`), new `Segmented`, `Stepper`, `Meter`. All have stories → **re-sync required** (rebuild `sb-reference`, run the driver) so the design project gets the new cards and `conventions.md` can list them.
- `conventions.md` should mention: `Segmented` for exclusive choices (radiogroup), `Stepper` for integers, `Meter` for levels, `Knob` mod rings (`mods=[{tone,depth,bipolar}]`), and that `--glow`/`--tglow` are opt-in chassis variables (default `none`).

## 2026-09-18 — second sync (first re-sync)
- Driver verdict on the extended library: 3 changed (Knob, Panel, Tabs), 3 added (Meter, Segmented, Stepper), 7 unchanged; all 6 graded `match` from the sheets/raw shots; the driver-triggered `[SPOT_CHECK]` (Button, Toggle, ValueReadout, WavetableDisplay, Select) confirmed the recorded grades. Framing scale (~1.19× on the storybook side) is the same as on the first sync — not a delta.
- `[GRID_OVERFLOW]` Tabs (Bar story) → `overrides.Tabs.cardMode = "column"` (presentation-only; targeted `preview-rebuild.mjs --components Tabs`, no re-grade).
- `[STORY_CAP]` Knob has 8 stories (Modulated, Drop Target sit beyond the default cap of 6). Run the driver with `--max-stories 8` (it forwards the flag to compare) so those stories are captured and graded; a run without it re-keys Knob to the 6-story set.
- The dts extractor drops exported helper types: the emitted `Knob.d.ts`/`Tabs.d.ts`/`Segmented.d.ts` reference `KnobMod`, `TabItem`, `SegmentedOption` (and `Tone`) without declaring them. `conventions.md` declares their shapes — update it whenever those types change in `src/components/*/`.
- `--glow` / `--tglow` are consumed by Knob/SectionHeader/Segmented/Tabs as `var(--glow, none)` but never defined in `ui.css` — they are opt-in wrapper variables, not tokens; conventions.md documents them as such (do not list them under tokens).
- `SectionHeader` re-ships (new `on`/`onToggle` props → `.d.ts` hash) although its stories didn't change: the driver reports it under `upload.components` but not under `verification.changed`. Expected.

## Re-sync risks (added 2026-09-18, second sync)
- Helper-type drift: `KnobMod`/`TabItem`/`SegmentedOption`/`Tone` live only in `conventions.md` for the design agent — a change in source silently desyncs the header until re-validated.
- Knob story cap: always pass `--max-stories 8` (or higher as stories grow) to the driver, otherwise the modulation stories drop out of the verified set.

## 2026-09-18 — Knob.onChangeEnd added (JUCE bridge work)
- `Knob` gained an `onChangeEnd?: () => void` prop (fires when a drag/gesture ends, wired through `useDragValue`), added for the JUCE bridge's parameter gesture reporting (`beginChangeGesture`/`endChangeGesture`). No visual change, no new story — it's a behavioural prop.
- **Re-sync required**: the `.d.ts` hash for `Knob` changes, so the driver will re-ship it even though its stories/pixels are unchanged (same "expected, not a delta" pattern as `SectionHeader` above). Rebuild `sb-reference` and run the driver; `conventions.md`'s `Knob` prop list should mention `onChangeEnd` alongside the existing `mods`/`liveValue`/`onDropMod` entries.


## 2026-09-21 — third sync (motion pass + Keybed/Wheel)
- `Motion/Bench` (`src/stories/Motion.stories.tsx`) is a dev bench, not a shippable component: it imports the app's `WebUI/src/synth/ui/synth.css` (outside the package) and renders through `sx-chassis`, which does not exist in `ui.css`. Excluded via `titleMap.Bench = null` — keep it excluded when the bench grows.
- [GENERAL] **A story wrapper that is a plain block div silently kills any component whose body is `flex-1`.** `Wheel` shipped invisible on the first capture of this sync: its root is `flex flex-col` with the slider as `flex-1`, so inside `<div className="h-fader">` the body collapsed to 0px and only the label rendered — in the reference storybook as much as in the preview, which is why the compare sheets "agreed". Fixed in the story: `<div className="flex h-fader">`. The app was never affected (`BottomStrip` already wraps the wheels in a flex row with a fixed height). When a new control renders as a bare label, check the wrapper before the component.
- [GENERAL] `.storybook/preview.tsx` sets `layout: "centered"` globally, which shrink-wraps the canvas: a decorator using `w-full` collapses to a few pixels. `Keybed` hit this (its reference render was ~110px wide against a full-width preview). Fixed with a per-story `parameters: { layout: "padded" }` on the Keybed meta. Any future full-width component needs the same override, otherwise its oracle is degenerate and cannot be graded.
- Despite the whole motion pass (knob pointer, tab underline, segmented indicator, button/toggle physics), the driver reported 13 unchanged / 0 changed: motion lives in CSS, which re-ships as `upload.styling` without moving any grade contract. Both panels rebuilt together, and the canary spot-checks (Button, Panel, Meter, ValueReadout, Tabs, then SectionHeader, WavetableDisplay) confirmed the recorded grades. Expected, not a miss.
- `Keybed.d.ts` references `KeybedNoteMask` without declaring it — the same dts-extractor drift as `KnobMod`/`TabItem`/`SegmentedOption`. Its shape is declared in `conventions.md`; update it whenever the mask type changes.
- `Keybed`'s `Playing` story animates via `setInterval` at 30fps, so the lit cluster lands on a different frame in each panel. Graded `match` with a note: the mechanism and the lit-key styling coincide, only the note positions differ. Do not chase it.

## Re-sync risks (added 2026-09-21, third sync)
- Two story-level layout fixes are now load-bearing for the oracle: `flex` on the Wheel story wrapper and `layout: "padded"` on the Keybed meta. A refactor that drops either makes that component's reference render degenerate again while the app stays fine.
- `Motion/Bench` depends on a relative import that escapes the package (`../../../../src/synth/ui/synth.css`). If the app's path moves, the storybook build breaks and with it the whole reference — the sync's oracle, not just one card.
- `conventions.md` now documents `Wheel`'s parent requirement (flex + definite height) and `Keybed`'s (`h-full w-full` needs a sized parent). Both are the kind of thing the design agent cannot infer from the `.d.ts`; keep them if the components' root classes change.

## 2026-09-21 — SynthWindow riallineato al nuovo progetto design (non è un sync)
Progetto Claude Design `f17f6ac6-f18c-45f5-8b61-ae72cfda7fec` (`SynthWindow.dc.html` + `app.jsx`, `panels.jsx`, `knob.jsx`, `tabs.jsx`, `keyboard.jsx`, `synth.css`): è un *design* che consuma questo DS, non il DS stesso — il driver non c'entra, `config.json` resta su `3a8cfc98-…`. Il lavoro sta tutto in `WebUI/src/synth/` + `Source/plugin/PluginEditor.cpp`, quindi **nessun re-sync richiesto**.
- Chassis da 680 a **690**, gap da 6 a **8**, con la somma dei figli esatta: 20 di padding + Header 40 + WaveDisplay 118 + pannelli 228 + TabArea 144 + BottomStrip 108 + quattro gap. Le tre copie di `H` (`SynthWindow.tsx`, la regola `.sx-chassis`, `kChassisHeight`) sono cambiate insieme, e i test che le rileggono ora pinnano anche il gap e le classi di altezza dei cinque figli.
- [GENERAL] **Le alpha esadecimali abbreviate del design non sono i decimali che sembrano.** `#0009` è 0x99/255 = 0.6, `#000b` è 0.733, `#000c` 0.8, `#000d` 0.867 — non 0.55/0.7/0.75/0.85, che è come erano state trascritte la prima volta. Su una ventina di `box-shadow` la somma di quegli errori è esattamente la "profondità in meno" che si vedeva confrontando con il prototipo. Tutte riportate al valore vero.
- [GENERAL] **Un `<circle>` non prende `box-shadow`.** Il cappuccio del knob nel design è un `<div>` con quattro strati (due inset e due ombre portate); nella libreria è un cerchio SVG. Gli inset li fa il gradiente del cappuccio, le ombre portate ora sono `filter: drop-shadow(...)` sul `[data-part="cap"]`. Le lunghezze del design sono px assoluti (le stesse su tutte e tre le taglie) mentre il `viewBox` è `0 0 40 40` scalato sulla scatola: da qui `--knob-u` (= 40/scatola) su `.size-knob-*`, usata anche per gli spessori degli anelli.
- L'ombra di contatto della libreria (`[data-part="cap-shadow"]`, r = CAP_R + 0.7) **spariva**: il CSS dell'app ridefiniva `r` su `cap`/`cap-edge`/`rim` ma non su di lei, quindi il disco restava a 11.7 sotto un cappuccio da 14.6. Chi ridefinisce un raggio del knob deve ridefinirli tutti e quattro.
- Gli anelli del cappuccio erano nel verso sbagliato: il tono stava sul cerchio interno (`rim`) e il bianco su quello esterno (`cap-edge`), mentre nel design l'anello luminoso è il più esterno. Invertiti in `glass` e in `metal`.
- Il rapporto disco/scatola del design **non è costante** fra le taglie (0.652 / 0.724 / 0.800): un solo `r` per tutte rimpicciolisce i `lg` e ingrossa i `sm`. Ora c'è una terna per classe di taglia; essendo un rapporto, resta valida qualunque sia la scatola.
- [GENERAL] **Padding doppio nei pannelli.** `CardContent` di `Panel` mette già `px-3 pb-3`; le classi dell'app ne aggiungevano altri 10 px, cioè 22 dove il design ne vuole 10. Erano 20 px per pannello buttati, e sono la ragione principale per cui i knob "non entravano". Tolti da `Panels.tsx`.

### Deviazioni dal design che restano (verificate, non sviste)
- **Scatole dei knob 38/48/68 invece di 46/58/80.** Misurato in Chromium: con le misure del design i tre pannelli chiedono 976 px dove ce ne sono 860 (900 meno padding e gap), e i knob Level e Key trk escono dal pannello — il prototipo stesso trabocca. Risolvendo per la scatola grande tenendo le proporzioni, `3L + 7M + 2S ≤ 628`, cioè `L ≤ 68.1`: 68 è il massimo che entra. Per avere 80 servirebbe togliere la colonna wavetable/OCT/SEMI dalla riga dei knob (libera 128 px) o allargare lo chassis a 1016.
- **Pannelli 430 / flex / 176 invece di 384 / flex / 200.** Il Filter ha bisogno dei 24 px del Master per le etichette lunghe ("Resonance"): a 200 trabocca di 22.
- **TabArea 144 invece di 126**: la tab FX è la sola a impilare intestazione e knob dentro uno slot.
- **WaveDisplay 118 invece di 138, curva del filtro 56 invece di 66**: è quel che avanza nel budget dei 690 una volta pagati header, pannelli, tab e striscia.
- **Raggio dell'arco del valore** più interno del design (0.80 della scatola contro 0.87–0.93): è calcolato in JS dentro il `d` del path, non è una proprietà CSS — allinearlo vuol dire toccare `@xerum/ui` e rifare il sync.
- **Nessun bagliore sotto il tab attivo**: l'indicatore scorrevole della libreria usa `scaleX`, e un glow radiale si stirerebbe in orizzontale. La scelta è già argomentata in `packages/ui/src/components/Tabs/Tabs.tsx`.
- **Cappuccio `metal` con la zigrinatura** al posto del `repeating-conic-gradient` del design: su un cerchio SVG servirebbe un pattern, cioè di nuovo la libreria.
