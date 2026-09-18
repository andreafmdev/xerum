import type { ReactNode } from "react";
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
      <span aria-hidden className="size-1.5 shrink-0 rounded-full bg-(--tone) shadow-[0_0_4px_var(--tone)]" />
      <span className="text-2xs font-semibold tracking-widest text-(--tone) uppercase">{title}</span>
      {/* Riga incisa: scuro sopra, chiaro sotto = solco inciso nella piastra. */}
      <span aria-hidden className="flex-1 self-center">
        <span className="block h-px bg-edge-dark" />
        <span className="block h-px bg-edge-light" />
      </span>
      {actions && <div className="flex items-center gap-2">{actions}</div>}
    </div>
  );
}
