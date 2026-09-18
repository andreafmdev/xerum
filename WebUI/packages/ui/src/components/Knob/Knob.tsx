import { useId, useState, type DragEvent } from "react";
import { cva } from "class-variance-authority";
import { cn } from "@/lib/utils";
import { arcPath, knobAngles, polar, KNOB_START, KNOB_SWEEP } from "@/lib/arc";
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
  /** Anelli di modulazione disegnati fuori dall'arco del valore, uno per sorgente. */
  mods?: KnobMod[];
  /** Valore modulato istantaneo 0..1: il puntino sull'arco. Mostrato solo con `mods`. */
  liveValue?: number;
  /** Rende il knob bersaglio di drop per i chip sorgente (`dataTransfer` "text/x-mod"). */
  onDropMod?: (source: string) => void;
  /** Nasconde il readout testuale (aria-valuetext resta). */
  hideValue?: boolean;
  className?: string;
  id?: string;
};

export type KnobMod = {
  /** Colore della sorgente. */
  tone: Tone;
  /** Profondità 0..1 rispetto al range del parametro. */
  depth: number;
  /** Sorgente bipolare (LFO): l'anello copre entrambi i lati del valore. */
  bipolar?: boolean;
};

export const MOD_DRAG_TYPE = "text/x-mod";

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

/** Estremi 0..1 dell'anello di un mod attorno a `value`. */
export function modRange(value: number, mod: KnobMod): [number, number] {
  const d = Math.abs(mod.depth);
  return [clamp01(mod.bipolar ? value - d : value), clamp01(value + d)];
}

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
const CAP_R = 11;

/** Scala esterna: 11 tacche sui 270°, più lunghe agli estremi e al centro. */
const TICKS = Array.from({ length: 11 }, (_, i) => {
  const deg = KNOB_START + (KNOB_SWEEP * i) / 10;
  const major = i === 0 || i === 5 || i === 10;
  const [x1, y1] = polar(C, C, major ? 17.6 : 18.4, deg);
  const [x2, y2] = polar(C, C, 19.5, deg);
  return { x1, y1, x2, y2, major };
});

/** Zigrinatura: 12 intagli sul bordo del cappuccio. */
const GRIPS = Array.from({ length: 12 }, (_, i) => {
  const deg = i * 30;
  const [x1, y1] = polar(C, C, CAP_R - 1.2, deg);
  const [x2, y2] = polar(C, C, CAP_R - 0.1, deg);
  return { x1, y1, x2, y2 };
});

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
  mods,
  liveValue,
  onDropMod,
  hideValue = false,
  className,
  id,
}: KnobProps) {
  const [dropTarget, setDropTarget] = useState(false);
  const isModDrag = (e: DragEvent) => Array.from(e.dataTransfer?.types ?? []).includes(MOD_DRAG_TYPE);
  const dropHandlers = onDropMod
    ? {
        onDragOver: (e: DragEvent<HTMLDivElement>) => {
          if (!isModDrag(e)) return;
          e.preventDefault();
          setDropTarget(true);
        },
        onDragLeave: () => setDropTarget(false),
        onDrop: (e: DragEvent<HTMLDivElement>) => {
          if (!isModDrag(e)) return;
          e.preventDefault();
          setDropTarget(false);
          onDropMod(e.dataTransfer.getData(MOD_DRAG_TYPE));
        },
      }
    : undefined;
  const modulated = (mods?.length ?? 0) > 0;
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue: defaultValue ?? (bipolar ? 0.5 : 0),
    onChange,
    disabled,
  });

  // useId produce ":r0:": i due punti sono legali in un id SVG ma rompono url(#…) in alcuni motori.
  const uid = useId().replace(/:/g, "");
  const capFill = `knob-cap-${uid}`;
  const capShadow = `knob-shadow-${uid}`;

  const { start, end } = knobAngles(value, bipolar);
  const text = format(value);
  const pointerDeg = KNOB_START + KNOB_SWEEP * value;

  return (
    <div
      data-testid="knob"
      data-slot="knob"
      data-drop-target={onDropMod ? dropTarget : undefined}
      className={cn("group/knob flex flex-col items-center gap-1.5", className)}
      style={toneStyle(tone)}
      {...dropHandlers}
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
          "cursor-ns-resize focus-visible:ring-2 focus-visible:ring-(--tone) focus-visible:ring-offset-2 focus-visible:ring-offset-background",
          // Bersaglio di drop: alone della sorgente attorno al cappuccio.
          "group-data-[drop-target=true]/knob:ring-2 group-data-[drop-target=true]/knob:ring-lfo",
          disabled && "cursor-not-allowed",
        )}
        {...handlers}
      >
        <svg viewBox="0 0 40 40" className={cn("size-full overflow-visible", disabled && "opacity-50")}>
          <defs>
            {/* Luce da sopra a sinistra: il cappuccio è un solido tornito, non un disco piatto. */}
            <radialGradient id={capFill} cx="34%" cy="26%" r="78%">
              <stop offset="0%" stopColor="var(--color-cap-hi)" />
              <stop offset="100%" stopColor="var(--color-cap-lo)" />
            </radialGradient>
            <filter id={capShadow} x="-40%" y="-40%" width="180%" height="180%">
              <feDropShadow dx="0" dy="0.8" stdDeviation="0.9" floodColor="black" floodOpacity="0.6" />
            </filter>
          </defs>

          {TICKS.map((t, i) => (
            <line
              key={i}
              x1={t.x1}
              y1={t.y1}
              x2={t.x2}
              y2={t.y2}
              className="stroke-tick"
              strokeWidth={t.major ? 1.2 : 0.9}
            />
          ))}

          <path
            d={arcPath(C, C, R, KNOB_START, KNOB_START + KNOB_SWEEP)}
            className="fill-none stroke-surface-3"
            strokeWidth={2}
            strokeLinecap="round"
          />
          {/* Anelli di modulazione: fuori dalla scala, uno per sorgente, colorati dalla sorgente. */}
          {mods?.map((m, i) => {
            const [lo, hi] = modRange(value, m);
            return (
              <path
                key={i}
                data-testid="knob-mod-arc"
                data-range={`${lo},${hi}`}
                d={arcPath(C, C, R + 4.5 + i * 2.2, KNOB_START + KNOB_SWEEP * lo, KNOB_START + KNOB_SWEEP * hi)}
                className="fill-none opacity-80"
                style={{ stroke: `var(--color-${m.tone})` }}
                strokeWidth={1.6}
                strokeLinecap="round"
              />
            );
          })}
          <path
            data-testid="knob-value-arc"
            d={arcPath(C, C, R, start, end)}
            className="fill-none stroke-(--tone) transition-[d] ease-snap [filter:var(--glow,none)]"
            strokeWidth={2.5}
            strokeLinecap="round"
          />
          {modulated && liveValue !== undefined && (() => {
            const [lx, ly] = polar(C, C, R, KNOB_START + KNOB_SWEEP * clamp01(liveValue));
            return (
              <circle
                data-testid="knob-live"
                cx={lx}
                cy={ly}
                r={2.2}
                style={{ fill: `var(--color-${mods![0]!.tone})` }}
              />
            );
          })()}

          {/* Ombra di contatto + cappuccio + rialzo del bordo. */}
          <circle cx={C} cy={C} r={CAP_R + 0.6} className="fill-none stroke-edge-dark" strokeWidth={1} />
          <circle cx={C} cy={C} r={CAP_R} fill={`url(#${capFill})`} filter={`url(#${capShadow})`} />
          <circle
            cx={C}
            cy={C}
            r={CAP_R - 0.35}
            className="fill-none stroke-cap-rim opacity-80"
            strokeWidth={0.7}
          />
          {GRIPS.map((g, i) => (
            <line
              key={i}
              x1={g.x1}
              y1={g.y1}
              x2={g.x2}
              y2={g.y2}
              className="stroke-edge-dark opacity-60"
              strokeWidth={0.8}
            />
          ))}

          {/* Indicatore: solco scuro con il fondo lucido sopra. */}
          <g transform={`rotate(${pointerDeg - 270} ${C} ${C})`}>
            <line
              x1={C}
              y1={C - 4.2}
              x2={C}
              y2={C - 10.4}
              className="stroke-edge-dark"
              strokeWidth={2.2}
              strokeLinecap="round"
            />
            <line
              x1={C}
              y1={C - 4.8}
              x2={C}
              y2={C - 10}
              className="stroke-foreground"
              strokeWidth={1.2}
              strokeLinecap="round"
            />
          </g>
        </svg>
      </div>

      <div className="flex flex-col items-center">
        <span className={cn("text-(length:--text-label)/4", disabled ? "text-text-dim" : "text-muted-foreground")}>{label}</span>
        {!hideValue && (
          <span
            data-testid="knob-readout"
            data-dragging={dragging}
            className={cn(
              "font-mono text-(length:--text-label)/4 tabular-nums",
              disabled ? "text-text-dim" : dragging ? "text-(--tone)" : "text-foreground",
            )}
          >
            {text}
          </span>
        )}
      </div>
    </div>
  );
}
