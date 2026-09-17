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
