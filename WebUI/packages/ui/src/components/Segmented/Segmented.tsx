import { useRef, type KeyboardEvent } from "react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";

export type SegmentedOption<V extends string = string> = { value: V; label: string };

export type SegmentedProps<V extends string = string> = {
  value: V;
  onChange: (value: V) => void;
  options: SegmentedOption<V>[];
  /** Nome accessibile del gruppo. */
  label: string;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
};

/**
 * Selettore a piastrine in un incasso: una sola opzione premuta alla volta.
 * Semantica radiogroup (scelta esclusiva, non navigazione come `Tabs`).
 */
export function Segmented<V extends string = string>({
  value,
  onChange,
  options,
  label,
  tone,
  disabled = false,
  className,
}: SegmentedProps<V>) {
  const buttons = useRef<(HTMLButtonElement | null)[]>([]);

  const onKeyDown = (e: KeyboardEvent<HTMLButtonElement>, index: number) => {
    const delta = e.key === "ArrowRight" || e.key === "ArrowDown" ? 1 : e.key === "ArrowLeft" || e.key === "ArrowUp" ? -1 : 0;
    if (!delta) return;
    e.preventDefault();
    const next = (index + delta + options.length) % options.length;
    onChange(options[next]!.value);
    buttons.current[next]?.focus();
  };

  return (
    <div
      role="radiogroup"
      aria-label={label}
      data-slot="segmented"
      className={cn(
        "inline-flex w-fit gap-0.5 rounded-control bg-well p-0.5 shadow-well",
        disabled && "opacity-50",
        className,
      )}
      style={toneStyle(tone)}
    >
      {options.map((o, i) => {
        const checked = o.value === value;
        return (
          <button
            key={o.value}
            ref={(el) => {
              buttons.current[i] = el;
            }}
            type="button"
            role="radio"
            aria-checked={checked}
            tabIndex={checked ? 0 : -1}
            disabled={disabled}
            onClick={() => onChange(o.value)}
            onKeyDown={(e) => onKeyDown(e, i)}
            className={cn(
              "h-5 rounded-[2px] px-1.5 text-(length:--text-label)/4 font-medium whitespace-nowrap transition-colors outline-none",
              "focus-visible:ring-2 focus-visible:ring-(--tone)",
              checked
                ? // Piastrina rialzata sopra l'incasso, scritta nel colore di sezione.
                  "border border-edge-dark bg-linear-to-b from-cap-hi to-cap-lo text-(--tone) shadow-cap [text-shadow:var(--tglow,none)]"
                : "text-text-dim hover:text-muted-foreground",
              disabled && "cursor-not-allowed",
            )}
          >
            {o.label}
          </button>
        );
      })}
    </div>
  );
}
