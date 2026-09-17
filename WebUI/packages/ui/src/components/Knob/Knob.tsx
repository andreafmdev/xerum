import { cva } from "class-variance-authority";
import { cn } from "@/lib/utils";
import { arcPath, knobAngles, KNOB_START, KNOB_SWEEP } from "@/lib/arc";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type KnobProps = {
  /** 0..1 */
  value: number;
  /** Valore del doppio click. Default 0 (0.5 se bipolar). */
  defaultValue?: number;
  onChange: (v: number) => void;
  label: string;
  /** Testo del readout e di aria-valuetext. Default: percentuale. */
  format?: (v: number) => string;
  size?: "sm" | "md" | "lg";
  tone?: Tone;
  /** Arco disegnato dal centro invece che da zero. */
  bipolar?: boolean;
  disabled?: boolean;
  className?: string;
  id?: string;
};

const knobSize = cva("relative shrink-0 rounded-full outline-none select-none touch-none", {
  variants: {
    size: { sm: "size-knob-sm", md: "size-knob-md", lg: "size-knob-lg" },
  },
  defaultVariants: { size: "md" },
});

const defaultFormat = (v: number) => `${Math.round(v * 100)}%`;

// viewBox 40×40, arco a raggio 16
const C = 20;
const R = 16;

export function Knob({
  value,
  defaultValue,
  onChange,
  label,
  format = defaultFormat,
  size = "md",
  tone,
  bipolar = false,
  disabled = false,
  className,
  id,
}: KnobProps) {
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue: defaultValue ?? (bipolar ? 0.5 : 0),
    onChange,
    disabled,
  });

  const { start, end } = knobAngles(value, bipolar);
  const text = format(value);
  const pointerDeg = KNOB_START + KNOB_SWEEP * value;

  return (
    <div
      data-testid="knob"
      data-slot="knob"
      className={cn("group/knob flex flex-col items-center gap-1", disabled && "opacity-50", className)}
      style={toneStyle(tone)}
    >
      <div
        ref={ref}
        id={id}
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-valuetext={text}
        aria-disabled={disabled || undefined}
        aria-orientation="vertical"
        data-dragging={dragging}
        className={cn(
          knobSize({ size }),
          "cursor-ns-resize focus-visible:ring-2 focus-visible:ring-(--tone)/50",
          disabled && "cursor-not-allowed",
        )}
        {...handlers}
      >
        <svg viewBox="0 0 40 40" className="size-full">
          <path
            d={arcPath(C, C, R, KNOB_START, KNOB_START + KNOB_SWEEP)}
            className="fill-none stroke-surface-3"
            strokeWidth={3}
            strokeLinecap="round"
          />
          <path
            data-testid="knob-value-arc"
            d={arcPath(C, C, R, start, end)}
            className="fill-none stroke-(--tone) transition-[d] ease-snap"
            strokeWidth={3}
            strokeLinecap="round"
          />
          <circle cx={C} cy={C} r={11} className="fill-surface-1 stroke-border" strokeWidth={1} />
          <line
            x1={C}
            y1={C}
            x2={C}
            y2={C - 9}
            className="stroke-foreground"
            strokeWidth={2}
            strokeLinecap="round"
            transform={`rotate(${pointerDeg - 270} ${C} ${C})`}
          />
        </svg>
        <span
          data-testid="knob-readout"
          className={cn(
            "pointer-events-none absolute -top-5 left-1/2 -translate-x-1/2 rounded-sm bg-popover px-1 py-px font-mono text-2xs whitespace-nowrap text-foreground tabular-nums opacity-0 transition-opacity",
            "group-hover/knob:opacity-100 group-focus-within/knob:opacity-100 data-[dragging=true]:opacity-100",
          )}
          data-dragging={dragging}
        >
          {text}
        </span>
      </div>
      <span className="text-2xs uppercase tracking-wider text-muted-foreground">{label}</span>
    </div>
  );
}
