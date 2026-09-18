import type { ReactNode } from "react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type SectionHeaderProps = {
  title: string;
  tone?: Tone;
  /** Slot a destra (Toggle, Select, Button…). */
  actions?: ReactNode;
  /** Stato della sezione: LED acceso/spento. Default acceso. */
  on?: boolean;
  /** Se presente, il LED diventa un interruttore (`switch` "<title> on"). */
  onToggle?: (on: boolean) => void;
  className?: string;
};

export function SectionHeader({ title, tone, actions, on = true, onToggle, className }: SectionHeaderProps) {
  const led = cn(
    "size-1.5 shrink-0 rounded-full transition-colors",
    on ? "bg-(--tone) shadow-[0_0_4px_var(--tone),0_0_10px_color-mix(in_oklch,var(--tone)_50%,transparent)]" : "bg-led-off shadow-[inset_0_1px_1px_var(--color-edge-dark)]",
  );
  return (
    <div data-slot="section-header" className={cn("flex items-center gap-2", className)} style={toneStyle(tone)}>
      {onToggle ? (
        <button
          type="button"
          role="switch"
          aria-checked={on}
          aria-label={`${title} on`}
          data-testid="section-led"
          data-on={on}
          onClick={() => onToggle(!on)}
          // Area di click più larga del LED, senza spostare il layout.
          className="relative -m-1.5 flex size-4.5 shrink-0 items-center justify-center rounded-full outline-none focus-visible:ring-2 focus-visible:ring-(--tone)"
        >
          <span aria-hidden className={led} />
        </button>
      ) : (
        <span aria-hidden data-testid="section-led" data-on={on} className={led} />
      )}
      <span className={cn("text-2xs font-semibold tracking-widest uppercase [text-shadow:var(--tglow,none)]", on ? "text-(--tone)" : "text-muted-foreground")}>{title}</span>
      {/* Riga incisa: scuro sopra, chiaro sotto = solco inciso nella piastra. */}
      <span aria-hidden className="min-w-2 flex-1 self-center">
        <span className="block h-px bg-edge-dark" />
        <span className="block h-px bg-edge-light" />
      </span>
      {actions && <div className="flex items-center gap-2">{actions}</div>}
    </div>
  );
}
