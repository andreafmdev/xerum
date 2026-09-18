import { type KeyboardEvent } from "react";
import { ChevronLeft, ChevronRight } from "lucide-react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type StepperProps = {
  value: number;
  onChange: (value: number) => void;
  min: number;
  max: number;
  /** Nome accessibile dello spinbutton e base dei nomi dei bottoni. */
  label: string;
  /** Sigla a destra del valore (OCT, SEMI…). */
  unit?: string;
  format?: (v: number) => string;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
};

const defaultFormat = (v: number) => String(v);

/** Valore intero in una finestrella incassata con due frecce: ottave, semitoni, conteggi. */
export function Stepper({
  value,
  onChange,
  min,
  max,
  label,
  unit,
  format = defaultFormat,
  tone,
  disabled = false,
  className,
}: StepperProps) {
  const clamp = (v: number) => Math.min(max, Math.max(min, v));
  const set = (v: number) => {
    const next = clamp(v);
    if (next !== value) onChange(next);
  };
  const text = format(value);

  const onKeyDown = (e: KeyboardEvent<HTMLDivElement>) => {
    if (disabled) return;
    const targets: Record<string, number> = { ArrowUp: value + 1, ArrowRight: value + 1, ArrowDown: value - 1, ArrowLeft: value - 1, Home: min, End: max };
    const next = targets[e.key];
    if (next === undefined) return;
    e.preventDefault();
    set(next);
  };

  const arrow = "flex h-full w-4 items-center justify-center text-text-dim outline-none hover:text-foreground focus-visible:text-(--tone) disabled:cursor-not-allowed disabled:opacity-40 [&_svg]:size-3";

  return (
    <div
      data-slot="stepper"
      className={cn(
        "inline-flex h-5.5 w-fit items-center rounded-control bg-well font-mono text-(length:--text-label)/4 tabular-nums shadow-well",
        disabled && "opacity-50",
        className,
      )}
      style={toneStyle(tone)}
    >
      <button type="button" aria-label={`Decrease ${label}`} disabled={disabled || value <= min} onClick={() => set(value - 1)} className={arrow}>
        <ChevronLeft />
      </button>
      <div
        role="spinbutton"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={min}
        aria-valuemax={max}
        aria-valuenow={value}
        aria-valuetext={text}
        aria-disabled={disabled || undefined}
        onKeyDown={onKeyDown}
        data-testid="stepper-value"
        className="min-w-8 text-center text-foreground outline-none focus-visible:text-(--tone)"
      >
        {text}
      </div>
      <button type="button" aria-label={`Increase ${label}`} disabled={disabled || value >= max} onClick={() => set(value + 1)} className={arrow}>
        <ChevronRight />
      </button>
      {unit && <span className="pr-1.5 text-2xs tracking-wider text-text-dim">{unit}</span>}
    </div>
  );
}
