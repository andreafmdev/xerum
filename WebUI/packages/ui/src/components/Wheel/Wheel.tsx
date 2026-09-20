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
  // Niente onChangeEnd qui: quel callback dell'hook scatta anche dopo tastiera, rotella e doppio
  // click, non solo al rilascio del puntatore. Con springBack a on, un tasto freccia emetterebbe
  // il valore mosso e poi (nello stesso batch sincrono) il valore di riposo, e chi ascolta vedrebbe
  // solo il secondo: la rotella diventerebbe immobile da tastiera. Il molleggio e' quindi composto
  // a mano sui soli handler di puntatore, sotto.
  const { ref, handlers, dragging } = useDragValue({
    value,
    defaultValue,
    onChange,
    disabled,
    axis: "y",
  });

  // Bipolare: il riempimento cresce dal centro verso l'alto o verso il basso. Unipolare: dal
  // fondo, come un fader. In entrambi i casi e' `bottom` + `height`, mai un transform.
  const fill = bipolar
    ? { bottom: `${Math.min(value, defaultValue) * 100}%`, height: `${Math.abs(value - defaultValue) * 100}%` }
    : { bottom: "0%", height: `${value * 100}%` };

  return (
    <div
      data-testid="wheel"
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
        // Il molleggio e' solo "al rilascio del puntatore": l'handler dell'hook aggiorna prima il
        // valore trascinato, poi torniamo a defaultValue. Tastiera, rotella e doppio click restano
        // sul percorso dell'hook, senza molla.
        onPointerUp={(e) => {
          handlers.onPointerUp(e);
          if (springBack && !disabled) onChange(defaultValue);
        }}
        onPointerCancel={(e) => {
          handlers.onPointerCancel(e);
          if (springBack && !disabled) onChange(defaultValue);
        }}
      >
        <div
          data-testid="wheel-fill"
          data-part="fill"
          // Il riempimento scatta, non anima. La sua unica proprieta' che varia e' l'inline
          // `bottom`/`height` qui sotto (fill, calcolato sopra), e la whitelist delle proprieta'
          // animabili vieta di animare proprio quelle due (forzano layout). `bg-(--tone)' e'
          // l'unico altro stile su questo nodo ed e' statico per tutta la vita del componente:
          // una `transition-[background-color]` non avrebbe mai avuto niente da animare. C'era,
          // in una revisione precedente, insieme a un commento che la spacciava per il rimbalzo
          // del ritorno a centro (springBack) — non lo era: quel rimbalzo non esiste, il
          // riempimento salta al nuovo bottom/height come qualsiasi altro cambio di valore. Un
          // vero rimbalzo animato richiederebbe un riempimento a `transform`, fuori dal
          // perimetro di questo componente; la decisione di lasciarlo scattare resta cosi'.
          className="absolute inset-x-px rounded-[2px] bg-(--tone)"
          style={fill}
        />
        {/* Il segno della posizione, per chi preferisce una riga a un riempimento (una rotella a
            rullo, come su una tastiera): nascosto di default, una app lo accende via CSS e spegne
            `fill`. Sta a `top`, non a `bottom`, perche' e' un punto e non un'altezza. */}
        <span aria-hidden data-part="marker" className="absolute inset-x-0.5 hidden h-0.5 -translate-y-1/2 rounded-px bg-(--tone)" style={{ top: `${(1 - value) * 100}%` }} />
        {/* Il segno del riposo: al centro per la bipolare, assente per l'altra. */}
        {bipolar && <span aria-hidden className="absolute inset-x-0 top-1/2 h-px bg-tick" />}
      </div>
      <span className={cn("text-(length:--text-label)/4", disabled ? "text-text-dim" : "text-muted-foreground")}>
        {label}
      </span>
    </div>
  );
}
