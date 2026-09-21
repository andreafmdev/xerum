import { useState, type CSSProperties, type DragEvent } from "react";
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
  /** Fine di una modifica: pointer-up/cancel, oppure subito dopo un cambio da rotella, tastiera o doppio click. */
  onChangeEnd?: () => void;
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

/**
 * Le parti del disegno portano un `data-part` (track, value, cap, cap-sheen, pointer, pointer-tip,
 * label, readout): e' il contratto con cui una app cambia materiale al knob via CSS (vetro, metallo
 * spazzolato) senza aggiungere prop. I token --color-cap-* restano la via per ricolorare il
 * cappuccio; `data-part` serve per cio' che i token non coprono.
 *
 * Il cappuccio e' un <div>, non un <circle>. Il disegno di riferimento lo vuole di vetro: gradiente
 * radiale trasparente, alone interno, `backdrop-filter` che sfoca cio' che sta dietro, e un anello
 * a tre strati (riga bianca, anello del colore di sezione, alone). Di tutto questo un <circle> SVG
 * non fa niente — niente box-shadow, niente backdrop-filter, niente gradiente conico — e la resa
 * restava un disco piatto per quanto si limassero i token. Gli archi (corsa, valore, anelli di
 * modulazione) restano in SVG, dove stanno bene.
 *
 * Tre custom property, opzionali, sono il modo in cui una app riveste il cappuccio senza toccare
 * la libreria. Non sono token: senza di loro valgono i default qui sotto, costruiti sui token.
 * - `--cap`       il `background` del cappuccio (default: gradiente fra --color-cap-hi e -lo)
 * - `--cap-ring`  l'anello attorno (default: bordo scuro fuori, --color-cap-rim dentro)
 * - `--pointer`   il colore dell'indice (default: --foreground)
 * `--shadow-cap`, che e' un token, resta l'ombra del cappuccio e si somma sempre a `--cap-ring`.
 */

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

// viewBox 40×40.
const C = 20;
// Raggio degli archi. 18.7 non e' una scelta estetica ma aritmetica: il disegno tiene l'arco a 5 px
// dal bordo del cappuccio, sempre, su tutte e tre le taglie. Cappuccio e scatola crescono insieme
// (vedi CAP_RATIO), quindi un raggio unico in unita' di viewBox rende quei 5 px su tutte: con le
// scatole 38/48/68 dell'app vengono 5.4, 5.0 e 4.6 px. Mezzo pixel di scarto, un numero solo.
export const KNOB_ARC_R = 18.7;
const R = KNOB_ARC_R;

/** Quanto del quadrante occupa il disco del cappuccio, per taglia. */
const CAP_RATIO = { sm: 0.652, md: 0.724, lg: 0.8 } as const;

export function Knob({
  value,
  defaultValue,
  onChange,
  onChangeEnd,
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
    onChangeEnd,
    disabled,
  });

  const { start, end } = knobAngles(value, bipolar);
  const text = format(value);
  const pointerDeg = KNOB_START + KNOB_SWEEP * value;

  return (
    <div
      data-testid="knob"
      data-slot="knob"
      data-drop-target={onDropMod ? dropTarget : undefined}
      // Duplicato rispetto a quello sul quadrante qui sotto: il selettore CSS
      // `group-data-[dragging=true]/knob:…` richiede l'attributo sullo STESSO elemento che porta
      // la classe `group/knob`, cioe' questo contenitore, non il quadrante interattivo.
      data-dragging={dragging}
      className={cn("group/knob flex flex-col items-center gap-1.5", className)}
      style={toneStyle(tone)}
      {...dropHandlers}
    >
      <div
        ref={ref}
        id={id}
        data-testid="knob-dial"
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
          // La transizione vive qui perche' e' qui, su questo stesso elemento, che il
          // box-shadow del ring cambia davvero: altrove sarebbe CSS morto.
          "transition-[box-shadow] duration-(--dur-state) ease-glass",
          disabled && "cursor-not-allowed",
        )}
        // Il diametro del cappuccio come frazione del lato: lo legge il <div> qui sotto con una
        // size calcolata, cosi' resta una sola tabella (CAP_RATIO) invece di tre classi per taglia.
        style={{ "--knob-cap": CAP_RATIO[size] } as CSSProperties}
        {...handlers}
      >
        {/* Solo gli archi. Niente scala di tacche: il disegno non ne ha, e con l'arco portato a
            ridosso del bordo (R = 18.7) non ci sarebbe piu' lo spazio dove stavano. */}
        <svg viewBox="0 0 40 40" className={cn("size-full overflow-visible", disabled && "opacity-50")}>
          <path
            data-part="track"
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
                d={arcPath(C, C, R + 2.4 + i * 2.2, KNOB_START + KNOB_SWEEP * lo, KNOB_START + KNOB_SWEEP * hi)}
                // `origin-center` risolve al centro del viewBox (20,20 su "0 0 40 40"), che qui
                // coincide con C: e' il centro geometrico vero del knob, non l'angolo in alto a
                // sinistra a cui scala() atterrerebbe di default su un elemento SVG. L'animazione
                // e' un ingresso una tantum (nasce quando l'anello compare): niente `transition`
                // qui, che altrimenti reinterpolerebbe ogni ri-render a 30 Hz insieme al valore
                // (vedi il div del punto live piu' sotto, che per lo stesso motivo non ne ha).
                className="fill-none opacity-80 origin-center motion-safe:animate-[sx-ring-in_var(--dur-state)_var(--ease-glass)]"
                style={{ stroke: `var(--color-${m.tone})` }}
                strokeWidth={1.6}
                strokeLinecap="round"
              />
            );
          })}
          <path
            data-testid="knob-value-arc"
            data-part="value"
            d={arcPath(C, C, R, start, end)}
            // `d` e' interpolabile qui: arcPath produce sempre "M x y A r r 0 large 1 x y" (stessi
            // comandi, stesso numero di punti) per ogni valore 0..1, quindi i motori CSS
            // interpolano le coordinate. L'unica eccezione e' il flag "large-arc" (0/1, discreto:
            // non si anima, scatta di netto) quando il bipolar supera i 180°, un singolo istante
            // e non un lag percepibile. Durante il drag la transizione sparisce del tutto: il
            // valore deve seguire il dito, non una curva che lo insegue in ritardo.
            className="fill-none stroke-(--tone) transition-[d] duration-(--dur-state) ease-glass group-data-[dragging=true]/knob:transition-none [filter:var(--glow,none)]"
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

        </svg>

        {/* Il cappuccio. Un <div> centrato sul quadrante, largo CAP_RATIO del lato: da qui in giu'
            e' tutto CSS, che e' il punto — gradienti, backdrop-filter e anelli a piu' strati sono
            cio' che un <circle> non sa fare.

            Il rimbalzo al rilascio vive solo qui, mai sull'arco del valore o sull'indice: il
            cappuccio e' un oggetto fisico che puo' molleggiare, il valore e' un dato che non deve
            mai mostrare un numero diverso da quello che il motore ha davvero. */}
        <div
          data-part="cap"
          aria-hidden
          className={cn(
            "pointer-events-none absolute top-1/2 left-1/2 rounded-full",
            "size-[calc(var(--knob-cap)*100%)] -translate-x-1/2 -translate-y-1/2",
            "[background:var(--cap,radial-gradient(circle_at_34%_26%,var(--color-cap-hi),var(--color-cap-lo)))]",
            "[box-shadow:var(--shadow-cap),var(--cap-ring,0_0_0_1px_var(--color-edge-dark),inset_0_0_0_1px_var(--color-cap-rim))]",
            "transition-transform duration-(--dur-press) ease-settle group-data-[dragging=true]/knob:scale-[0.985]",
            disabled && "opacity-50",
          )}
        >
          {/* Il riflesso: un secondo strato sopra il fondo, cosi' una app puo' cambiare l'uno
              senza riscrivere l'altro. */}
          <span
            data-part="cap-sheen"
            className="pointer-events-none absolute inset-0 rounded-full [background:var(--cap-sheen,radial-gradient(circle_at_50%_20%,var(--color-cap-sheen),transparent_55%))]"
          />
          {/* Indice. L'elemento sale dal centro (bottom-1/2 + origin-bottom), quindi a rotazione 0
              punta in alto e l'angolo e' lo stesso che aveva la versione SVG: pointerDeg - 270.
              Il segno visibile non parte dal centro ne' arriva al bordo — sta fra il 25% e il 56%
              del raggio, come nel disegno. La' quelle due misure sono in px assoluti (8 e 18) e su
              un knob `sm` il segno usciva dal cappuccio; in frazioni la proporzione del knob grande
              vale per tutte e tre. */}
          <span
            data-part="pointer"
            className="absolute bottom-1/2 left-1/2 h-1/2 w-0.5 origin-bottom transition-transform duration-(--dur-state) ease-glass group-data-[dragging=true]/knob:transition-none"
            style={{ transform: `translateX(-50%) rotate(${pointerDeg - 270}deg)` }}
          >
            <span
              data-part="pointer-tip"
              className="absolute inset-x-0 bottom-[25%] h-[31%] rounded-full shadow-[0_0_2px_#000] [background:var(--pointer,var(--foreground))]"
            />
          </span>
        </div>
      </div>

      <div className="flex flex-col items-center">
        <span data-part="label" className={cn("text-(length:--text-label)/4", disabled ? "text-text-dim" : "text-muted-foreground")}>{label}</span>
        {!hideValue && (
          <span
            data-testid="knob-readout"
            data-part="readout"
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
