import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type FaderProps = {
  /** 0..1 */
  value: number;
  defaultValue?: number;
  onChange: (v: number) => void;
  /** Nome del parametro: visibile e usato come nome accessibile dello slider. */
  label: string;
  format?: (v: number) => string;
  orientation?: "vertical" | "horizontal";
  tone?: Tone;
  disabled?: boolean;
  className?: string;
  id?: string;
};

const defaultFormat = (v: number) => `${Math.round(v * 100)}%`;

const TICKS = [0, 1, 2, 3, 4];

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
      data-dragging={dragging}
      className={cn("group/fader flex items-center gap-2", vertical ? "flex-col" : "flex-row", className)}
      style={toneStyle(tone)}
    >
      <div
        data-slot="fader-track"
        data-dragging={dragging}
        className={cn(
          "flex items-stretch gap-1.5",
          vertical ? "flex-row" : "flex-col",
          disabled && "opacity-50",
        )}
      >
        {/* Fessura incassata nella piastra. */}
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
            "relative shrink-0 rounded-control bg-well shadow-well outline-none select-none touch-none",
            "focus-visible:ring-2 focus-visible:ring-(--tone) focus-visible:ring-offset-2 focus-visible:ring-offset-background",
            vertical ? "h-fader w-2.5 cursor-ns-resize" : "h-2.5 w-fader cursor-ew-resize",
            disabled && "cursor-not-allowed",
          )}
          {...handlers}
        >
          <div
            data-testid="fader-fill"
            data-part="fill"
            // Il riempimento scatta, non anima: la sua unica proprieta' che varia e' l'inline
            // `height`/`width` qui sotto (style, calcolato da `pct`), e la whitelist delle
            // proprieta' animabili vieta di animare proprio `height`/`width` (forzano layout).
            // `bg-(--tone)` e' l'unico altro stile su questo nodo ed e' statico per tutta la
            // vita del componente: una `transition-[background-color]` non avrebbe mai avuto
            // niente da animare, e infatti prima c'era solo per nome. Il vero movimento del
            // Fader vive sul cappuccio qui sotto (`data-part="cap"`), che scala davvero.
            className={cn(
              "absolute rounded-[2px] bg-(--tone)",
              vertical ? "inset-x-px bottom-px" : "inset-y-px left-px",
            )}
            style={vertical ? { height: pct } : { width: pct }}
          />
          {/* Cappuccio rettangolare con la riga centrale incisa. */}
          <div
            data-testid="fader-thumb"
            data-slot="fader-thumb"
            data-part="cap"
            className={cn(
              "absolute flex items-center justify-center rounded-[2px] border border-edge-dark bg-linear-to-b from-cap-hi to-cap-lo shadow-cap transition-transform duration-(--dur-press) ease-snap group-data-[dragging=true]/fader:scale-105",
              vertical ? "left-1/2 h-3 w-5 -translate-x-1/2 translate-y-1/2" : "top-1/2 h-5 w-3 -translate-x-1/2 -translate-y-1/2",
            )}
            style={vertical ? { bottom: pct } : { left: pct }}
          >
            <span aria-hidden className={cn("bg-foreground", vertical ? "h-px w-3" : "h-3 w-px")} />
          </div>
        </div>

        {/* Scala laterale: 5 tacche, come sui fader di un mixer. */}
        <div
          aria-hidden
          className={cn("flex justify-between", vertical ? "flex-col py-px" : "flex-row px-px")}
        >
          {TICKS.map((t) => (
            <span key={t} className={cn("bg-tick", vertical ? "h-px w-1.5" : "h-1.5 w-px")} />
          ))}
        </div>
      </div>

      <div className="flex flex-col items-center">
        <span className={cn("text-(length:--text-label)/4", disabled ? "text-text-dim" : "text-muted-foreground")}>{label}</span>
        <span
          data-testid="fader-readout"
          className={cn(
            "font-mono text-(length:--text-label)/4 tabular-nums",
            disabled ? "text-text-dim" : dragging ? "text-(--tone)" : "text-foreground",
          )}
        >
          {text}
        </span>
      </div>
    </div>
  );
}
