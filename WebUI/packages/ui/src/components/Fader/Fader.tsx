import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type FaderProps = {
  /** 0..1 */
  value: number;
  defaultValue?: number;
  onChange: (v: number) => void;
  label?: string;
  format?: (v: number) => string;
  orientation?: "vertical" | "horizontal";
  tone?: Tone;
  disabled?: boolean;
  className?: string;
  id?: string;
};

const defaultFormat = (v: number) => `${Math.round(v * 100)}%`;

export function Fader({
  value,
  defaultValue = 0,
  onChange,
  label,
  format = defaultFormat,
  orientation = "vertical",
  tone,
  disabled = false,
  className,
  id,
}: FaderProps) {
  const vertical = orientation === "vertical";
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue,
    onChange,
    disabled,
    axis: vertical ? "y" : "x",
  });
  const text = format(value);
  const pct = `${value * 100}%`;

  return (
    <div
      data-slot="fader"
      className={cn("group/fader flex items-center gap-2", vertical ? "flex-col" : "flex-row", disabled && "opacity-50", className)}
      style={toneStyle(tone)}
    >
      <div
        ref={ref}
        id={id}
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-orientation={orientation}
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-valuetext={text}
        aria-disabled={disabled || undefined}
        data-dragging={dragging}
        className={cn(
          "relative rounded-full bg-surface-2 outline-none select-none touch-none focus-visible:ring-2 focus-visible:ring-(--tone)/50",
          vertical ? "h-fader w-2 cursor-ns-resize" : "h-2 w-fader cursor-ew-resize",
          disabled && "cursor-not-allowed",
        )}
        {...handlers}
      >
        <div
          data-testid="fader-fill"
          className={cn("absolute rounded-full bg-(--tone)", vertical ? "inset-x-0 bottom-0" : "inset-y-0 left-0")}
          style={vertical ? { height: pct } : { width: pct }}
        />
        <div
          data-slot="fader-thumb"
          className={cn(
            "absolute size-4 rounded-full border border-line-strong bg-surface-3 shadow-sm transition-transform ease-snap group-data-[dragging=true]/fader:scale-110",
            vertical ? "left-1/2 -translate-x-1/2 translate-y-1/2" : "top-1/2 -translate-x-1/2 -translate-y-1/2",
          )}
          style={vertical ? { bottom: pct } : { left: pct }}
        />
      </div>
      <div className={cn("flex items-center gap-1", vertical ? "flex-col" : "flex-row")}>
        <span
          data-testid="fader-readout"
          className="font-mono text-2xs text-foreground tabular-nums"
        >
          {text}
        </span>
        {label && <span className="text-2xs uppercase tracking-wider text-muted-foreground">{label}</span>}
      </div>
    </div>
  );
}
