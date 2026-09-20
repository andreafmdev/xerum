import { cn } from "@/lib/utils";
import { toneStyle, type Tone } from "@/lib/tone";
import { useDragValue } from "@/hooks/useDragValue";

export type WheelProps = {
  /** 0..1. Per una rotella bipolare il centro e' 0.5. */
  value: number;
  onChange: (v: number) => void;
  /** Nome accessibile dello slider. */
  label: string;
  /** Valore di riposo: dove torna il doppio click e, con springBack, il rilascio. */
  defaultValue?: number;
  /** Riempimento e detent a partire dal centro invece che dal basso. */
  bipolar?: boolean;
  /** Al rilascio torna a defaultValue, come una rotella di pitch molleggiata. */
  springBack?: boolean;
  tone?: Tone;
  disabled?: boolean;
  className?: string;
};

/**
 * Rotella verticale: pitch e modulazione della striscia bassa.
 *
 * Non e' un Fader stretto. Un fader ha una scala — cinque tacche e un readout numerico — perche'
 * il suo valore si legge; una rotella si guarda solo per sapere dov'e' rispetto al riposo, e il
 * suo riposo per il pitch sta al centro, non in fondo. Da qui le due differenze che contano:
 * niente tacche, e un riempimento che puo' partire dal centro.
 */
export function Wheel({
  value,
  onChange,
  label,
  defaultValue = 0,
  bipolar = false,
  springBack = false,
  tone,
  disabled = false,
  className,
}: WheelProps) {
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue,
    onChange,
    disabled,
    axis: "y",
    // Il ritorno a riposo e' esattamente "la gesture e' finita": useDragValue chiama onChangeEnd
    // su pointer-up, pointer-cancel, rotella, tastiera e doppio click, cioe' in tutti i casi in
    // cui una rotella vera tornerebbe al centro.
    onChangeEnd: springBack ? () => onChange(defaultValue) : undefined,
  });

  // Bipolare: il riempimento cresce dal centro verso l'alto o verso il basso. Unipolare: dal
  // fondo, come un fader. In entrambi i casi e' `bottom` + `height`, mai un transform.
  const fill = bipolar
    ? { bottom: `${Math.min(value, defaultValue) * 100}%`, height: `${Math.abs(value - defaultValue) * 100}%` }
    : { bottom: "0%", height: `${value * 100}%` };

  return (
    <div
      data-slot="wheel"
      data-dragging={dragging}
      className={cn("group/wheel flex flex-col items-center gap-1", className)}
      style={toneStyle(tone)}
    >
      <div
        ref={ref}
        role="slider"
        tabIndex={disabled ? -1 : 0}
        aria-label={label}
        aria-orientation="vertical"
        aria-valuemin={0}
        aria-valuemax={1}
        aria-valuenow={value}
        aria-disabled={disabled || undefined}
        data-dragging={dragging}
        className={cn(
          "relative w-5 flex-1 cursor-ns-resize overflow-hidden rounded-control outline-none select-none touch-none",
          "bg-linear-to-r from-cap-lo via-cap-hi to-cap-lo shadow-well",
          "focus-visible:ring-2 focus-visible:ring-(--tone) focus-visible:ring-offset-2 focus-visible:ring-offset-background",
          disabled && "cursor-not-allowed opacity-50",
        )}
        {...handlers}
      >
        <div data-testid="wheel-fill" className="absolute inset-x-px rounded-[2px] bg-(--tone)" style={fill} />
        {/* Il segno del riposo: al centro per la bipolare, assente per l'altra. */}
        {bipolar && <span aria-hidden className="absolute inset-x-0 top-1/2 h-px bg-tick" />}
      </div>
      <span className={cn("text-(length:--text-label)/4", disabled ? "text-text-dim" : "text-muted-foreground")}>
        {label}
      </span>
    </div>
  );
}
