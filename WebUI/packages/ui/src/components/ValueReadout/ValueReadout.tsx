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
      className={cn(
        // Finestrella incassata: il valore si legge come su un display, non come un badge.
        "h-6 gap-1.5 rounded-control bg-well px-1.5 text-label text-(--tone) shadow-well",
        mono && "font-mono tabular-nums",
        className,
      )}
    >
      {label && <span className="text-label text-muted-foreground">{label}</span>}
      <span>{value}</span>
    </Badge>
  );
}
