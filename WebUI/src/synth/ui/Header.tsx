import { Button } from "@xerum/ui";
import { ChevronLeft, ChevronRight, LayoutGrid, Redo2, Save, Settings, Undo2 } from "lucide-react";
import { useBoolParam } from "../../juce/hooks";
import type { Preset } from "../presets";

type Props = {
  preset: Preset;
  dirty: boolean;
  onPrev: () => void;
  onNext: () => void;
  onBrowse: () => void;
};

const iconBtn = "size-6.5! rounded-control! text-muted-foreground hover:text-foreground [&_svg]:size-3.5";

export function Header({ preset, dirty, onPrev, onNext, onBrowse }: Props) {
  const bypass = useBoolParam("bypass");
  return (
    <header className="flex h-10 shrink-0 items-center gap-2.5 px-1">
      <Logo>XERUM</Logo>
      <Button variant="secondary" size="icon-xs" aria-label="Undo" className={iconBtn}><Undo2 /></Button>
      <Button variant="secondary" size="icon-xs" aria-label="Redo" className={iconBtn}><Redo2 /></Button>

      {/* Slot preset: incasso con frecce ai lati e il nome al centro. */}
      <div className="mx-auto flex h-7 w-full max-w-95 items-center overflow-hidden rounded-control bg-well shadow-well">
        <button type="button" aria-label="Previous preset" onClick={onPrev} className="flex h-full w-7 items-center justify-center text-text-dim hover:bg-foreground/5 hover:text-foreground">
          <ChevronLeft className="size-3.5" />
        </button>
        <button type="button" onClick={onBrowse} className="flex h-full flex-1 items-center justify-center gap-2 font-mono text-xs text-foreground hover:bg-foreground/5">
          <small className="text-2xs tracking-wider text-text-dim uppercase">{preset.cat}</small>
          {preset.name}
          {dirty ? " *" : ""}
          <LayoutGrid className="size-3 text-text-dim" />
        </button>
        <button type="button" aria-label="Next preset" onClick={onNext} className="flex h-full w-7 items-center justify-center text-text-dim hover:bg-foreground/5 hover:text-foreground">
          <ChevronRight className="size-3.5" />
        </button>
      </div>

      <Button variant="secondary" size="icon-xs" aria-label="Save" className={iconBtn}><Save /></Button>
      <Button
        variant="secondary"
        size="xs"
        aria-pressed={bypass.checked}
        onClick={() => bypass.set(!bypass.checked)}
        className={`h-6.5! rounded-control! text-2xs tracking-wider uppercase ${bypass.checked ? "text-env [text-shadow:var(--tglow)]" : "text-muted-foreground"}`}
      >
        Bypass
      </Button>
      <Button variant="secondary" size="icon-xs" aria-label="Settings" className={iconBtn}><Settings /></Button>
    </header>
  );
}

export function Logo({ children }: { children: string }) {
  return (
    <div className="flex items-center gap-2 text-[13px] font-semibold tracking-[0.22em]">
      <i className="inline-block size-3.5 rounded-[3px] bg-linear-to-br from-osc to-filter shadow-[0_0_8px_color-mix(in_oklch,var(--color-osc)_50%,transparent)]" />
      {children}
    </div>
  );
}
