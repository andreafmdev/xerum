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
