import type { ReactNode } from "react";
import { Card, CardContent } from "@/components/ui/card";
import { SectionHeader } from "@/components/SectionHeader/SectionHeader";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type PanelProps = {
  title?: string;
  /** Colore di sezione: LED e titolo dell'header, `--tone` ereditata da tutti i figli. */
  tone?: Tone;
  actions?: ReactNode;
  /** Stato della sezione, mostrato dal LED dell'header. */
  on?: boolean;
  /** Se presente, il LED dell'header diventa l'interruttore della sezione. */
  onToggle?: (on: boolean) => void;
  children: ReactNode;
  className?: string;
};

export function Panel({ title, tone, actions, on, onToggle, children, className }: PanelProps) {
  return (
    <Card
      data-testid="panel"
      data-slot="panel"
      size="sm"
      className={cn(
        "gap-2 overflow-visible rounded-plate! bg-surface-1 pt-2 pb-0 shadow-panel ring-0",
        className,
      )}
      style={toneStyle(tone)}
    >
      {title && (
        <div className="px-3">
          <SectionHeader title={title} actions={actions} on={on} onToggle={onToggle} />
        </div>
      )}
      <CardContent className="flex flex-1 flex-col gap-3 px-3 pb-3">{children}</CardContent>
    </Card>
  );
}
