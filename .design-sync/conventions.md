## How to build with XerumUI (read before styling)

XerumUI is the UI of a wavetable synthesizer plugin: dark only, one visual world (physical-looking controls on flat panels). Every design you build with it must read as a synth panel, not a web dashboard.

### Setup and wrapping
- No provider is needed. Link `styles.css`, load `_ds_bundle.js`, mount into your own container. The stylesheet paints `html`/`body` with `var(--background)` / `var(--foreground)` and ships the fonts (IBM Plex Sans, IBM Plex Mono) inline — never override the page background with white.
- Group controls inside `Panel` (props `title`, `tone`, `actions`). Section colour is the `tone` prop: `"osc" | "filter" | "env" | "lfo" | "fx" | "master"`. A `Panel` sets the CSS variable `--tone`; every `Knob`, `Fader`, `Toggle`, `Tabs`, `SectionHeader` inside inherits it automatically, so pass `tone` to the `Panel`, not to each child. Without a `Panel`, `--tone` falls back to `--primary`.
- All control values are normalised numbers `0..1` (`value` + `onChange`); the display text comes from `format(value)`. `Fader` requires `label`; `Knob` requires `label`.
- Only the bar of `Tabs` is provided (`items`, `value`, `onChange`) — render the active content yourself.

### Styling idiom: precompiled Tailwind utilities + tokens
`styles.css` is a compiled Tailwind v4 stylesheet. **There is no Tailwind build at design time, so only class names already present in the stylesheet work.** Anything else silently does nothing. Use this vocabulary for your own layout glue:
- Layout: `flex`, `inline-flex`, `flex-col`, `flex-row`, `flex-1`, `flex-wrap`, `grid`, `grid-cols-3`, `items-center`, `items-start`, `items-end`, `justify-between`, `justify-center`, `gap-1`, `gap-2`, `gap-3`, `gap-4`, `gap-6`, `p-1`, `p-6`, `px-2`, `px-3`, `py-1`, `py-2`, `pb-3`, `w-48`, `w-72`, `w-80`, `w-full`, `w-fit`, `h-24`, `size-full`, `relative`, `absolute`, `shrink-0`.
- Surfaces and colour: `bg-background`, `bg-surface-0`, `bg-surface-1`, `bg-well`, `bg-card`, `bg-popover`, `bg-primary`, `bg-secondary`, `bg-(--tone)`, `text-(--tone)`, `text-muted-foreground`, `text-text-dim`, `text-primary`, `border`, `border-border`, `border-edge-light`, `border-edge-dark`, `ring-1`, `ring-border`.
- Depth (the instrument look): `shadow-panel` (a plate), `shadow-well` (a recessed slot or screen), `shadow-cap` (a raised cap), `rounded-control`, `rounded-md`, `rounded-lg`, `rounded-full`.
- Type: `font-sans`, `font-mono`, `tabular-nums`, `text-2xs`, `text-xs`, `text-sm`, `text-base`, `font-medium`, `font-semibold`, `uppercase`, `tracking-widest`. Section titles are uppercase `text-2xs tracking-widest`; parameter labels are sentence-case `text-xs text-muted-foreground`; values are `font-mono text-xs tabular-nums`.
For anything not in that list, use inline `style` with tokens — never a hex colour: `style={{ background: "var(--color-surface-2)", color: "var(--color-text-dim)" }}`.

Tokens (all defined in `styles.css` → `_ds_bundle.css`):
- Semantic: `--background`, `--foreground`, `--card`, `--popover`, `--primary`, `--secondary`, `--muted`, `--muted-foreground`, `--border`, `--input`, `--ring`, `--destructive`, `--radius`.
- Surfaces: `--color-surface-0` … `--color-surface-3`, `--color-well`, `--color-line-strong`, `--color-text-dim`, `--color-edge-light`, `--color-edge-dark`.
- Section tones: `--color-osc`, `--color-filter`, `--color-env`, `--color-lfo`, `--color-fx`, `--color-master`; runtime `--tone`.
- Hardware: `--color-cap-hi`, `--color-cap-lo`, `--color-cap-rim`, `--color-cap-sheen`, `--color-tick`, `--color-led-off`, `--shadow-panel`, `--shadow-well`, `--shadow-cap`, `--shadow-control`, `--radius-plate`, `--radius-control`, `--spacing-knob-sm|md|lg`, `--spacing-fader`.
- Fonts: no CSS variable is exposed; use the classes `font-sans` (IBM Plex Sans) and `font-mono` (IBM Plex Mono).

### Where the truth lives
Read `styles.css` and its import `_ds_bundle.css` for the exact classes and tokens, and `components/<group>/<Name>/<Name>.prompt.md` + `<Name>.d.ts` for each component's props and story JSX. Groups: `controls` (Button, Fader, Knob, Segmented, Select, Stepper, Toggle), `layout` (Panel, SectionHeader, Tabs), `display` (Meter, ValueReadout, WavetableDisplay).

### One idiomatic panel
```jsx
const { Panel, Knob, Toggle, Select } = window.XerumUI;
<Panel title="Filter" tone="filter" actions={<Toggle checked={on} onChange={setOn} label="on" />}>
  <Select value={type} onChange={setType} label="Filter type"
    options={[{ value: "lp24", label: "LP 24" }, { value: "hp12", label: "HP 12" }]} />
  <div className="flex gap-4">
    <Knob value={cutoff} onChange={setCutoff} label="Cutoff" size="lg" format={(v) => `${Math.round(20 * Math.pow(1000, v))} Hz`} />
    <Knob value={res} onChange={setRes} label="Res" />
  </div>
</Panel>
```
Panels sit side by side in a `flex gap-4` or `grid grid-cols-3 gap-4` row on the `bg-background` chassis.

### Newer controls and the types the `.d.ts` files leave undeclared
- `Segmented` — one exclusive choice (a radiogroup): `value`, `onChange`, `options=[{ value, label }]`, required `label`, optional `tone`/`disabled`. Use it for wave shape, filter type, voice mode — never a hand-built row of `Button`s.
- `Stepper` — integer values with `<`/`>` buttons: `value`, `onChange`, `min`, `max`, required `label`, optional `unit` ("OCT", "SEMI"), `format`, `tone`.
- `Meter` — an LED level bar, display only: `level` 0..1, required `label`, `segments` (default 24), `tone`.
- `Knob` modulation: `mods=[{ tone, depth, bipolar? }]` draws one ring per source outside the value arc (`tone` = the source's section colour, `depth` 0..1, `bipolar: true` for LFO-like sources); `liveValue` (0..1) adds the moving dot and is only shown with `mods`; `onDropMod(source)` makes the knob a drop target for a draggable chip that calls `e.dataTransfer.setData("text/x-mod", "lfo")`; `hideValue` hides the readout.
- `Tabs` `variant="bar"`: a full-width bar with an underline on the active item. Each item may carry its own `tone` (`items=[{ value, label, tone? }]`) so every page lights up in its section colour. Default `variant="plate"`.
- `Panel` / `SectionHeader` `on` + `onToggle`: with `onToggle` the header LED becomes the section's on/off switch (a `role="switch"`); without it the LED is a status light (`on` defaults to true). Prefer this over a separate `Toggle` in `actions` for enabling a section.
- Helper types referenced by the props but not declared in the `.d.ts` files: `Tone = "osc" | "filter" | "env" | "lfo" | "fx" | "master"`, `KnobMod = { tone: Tone; depth: number; bipolar?: boolean }`, `TabItem = { value: string; label: string; tone?: Tone }`, `SegmentedOption = { value: string; label: string }`.
- Optional glow: the components read `--glow` (an SVG `filter` on the value arc) and `--tglow` (a `text-shadow` on lit labels), both defaulting to none — they are NOT tokens in `styles.css`. Set them on a wrapper with inline style for the lit look: `style={{ "--glow": "drop-shadow(0 0 4px var(--tone))", "--tglow": "0 0 8px var(--tone)" }}`.
- Restyling hooks (no props): every drawn part of `Knob` carries `data-part` (`tick`, `track`, `value`, `cap-shadow`, `cap-edge`, `cap`, `rim`, `grip`, `pointer`, `pointer-groove`, `pointer-tip`, `label`, `readout`); each `Keybed` key carries `data-key="white" | "black"` plus `data-active`, and the C keys a `data-part="key-label"` octave name; `Wheel` exposes `data-part="fill"` (the default bar) and a hidden `data-part="marker"` line at the value, for a roller look. A consuming app changes a control's material (glass, brushed metal) with CSS on these attributes and on `data-slot`; the `data-testid` attributes are for tests only and are not a styling contract.
