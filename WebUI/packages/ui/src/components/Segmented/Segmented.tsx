import { useLayoutEffect, useRef, useState, type KeyboardEvent } from "react";
import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { measureIndicator, type IndicatorBox } from "@/lib/indicator";

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
  const group = useRef<HTMLDivElement>(null);
  const [box, setBox] = useState<IndicatorBox>({ x: 0, width: 0 });
  const active = options.findIndex((o) => o.value === value);

  // La misura dopo il layout, e a ogni cambio di selezione o di larghezza del gruppo.
  useLayoutEffect(() => {
    const container = group.current;
    const item = buttons.current[active];
    if (!container || !item) return;
    const measure = () => setBox(measureIndicator(container, item));
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(container);
    return () => ro.disconnect();
  }, [active, options.length]);

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
      ref={group}
      role="radiogroup"
      aria-label={label}
      data-slot="segmented"
      className={cn(
        "relative inline-flex w-fit gap-0.5 rounded-control bg-well p-0.5 shadow-well",
        disabled && "opacity-50",
        className,
      )}
      style={toneStyle(tone)}
    >
      <span
        aria-hidden
        data-testid="segmented-indicator"
        data-measured={box.width > 0}
        className="pointer-events-none absolute top-0.5 bottom-0.5 left-0 -z-10 origin-left rounded-[2px] border border-edge-dark bg-linear-to-b from-cap-hi to-cap-lo shadow-cap transition-transform duration-(--dur-state) ease-glass data-[measured=false]:opacity-0"
        style={{ width: 1, transform: `translateX(${box.x}px) scaleX(${box.width})` }}
      />
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
                ? // Lo sfondo rialzato ora lo disegna l'indicatore che scorre sotto: qui resta solo il colore di sezione.
                  "text-(--tone) [text-shadow:var(--tglow,none)]"
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
