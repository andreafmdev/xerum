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
