import { Button } from "@xerum/ui";
import { ChevronLeft, ChevronRight, LayoutGrid, Redo2, Save, Settings, Undo2 } from "lucide-react";
import { useBackend } from "../../juce/provider";
import { useBoolParam } from "../../juce/hooks";
import type { Preset } from "../presets";
import { useDirty } from "./SynthContext";
import mark from "@branding/svg/xerum-mark-simple.svg";

type Props = {
  preset: Preset;
  dirty: boolean;
  onPrev: () => void;
  onNext: () => void;
  onBrowse: () => void;
  onSettings: () => void;
  /** Scala corrente dello chassis (SynthWindow): serve a convertire trafficLightWidth, che
      arriva in px di finestra, nei px CSS che il padding dell'header deve occupare. */
  scale: number;
  /** Spazio da lasciare al semaforo, in px di finestra. 0 fuori dallo Standalone: niente
      padding, i bottoni non ci sono. */
  trafficLightWidth: number;
};

const iconBtn = "sx-hbtn size-6.5! rounded-control! text-muted-foreground hover:text-foreground [&_svg]:size-3.5";

/** Un mousedown/doppio clic su questi elementi non deve trascinare né ingrandire la finestra:
    altrimenti girare una manopola o premere un bottone dell'header sposterebbe la finestra
    invece di agire sul controllo — l'errore che renderebbe la UI inusabile. */
const isInteractive = (target: EventTarget | null) =>
  target instanceof HTMLElement && target.closest("button, input, select, [role='slider']") !== null;

export function Header({ preset, dirty, onPrev, onNext, onBrowse, onSettings, scale, trafficLightWidth }: Props) {
  const backend = useBackend();
  const bypass = useBoolParam("bypass");
  const withDirty = useDirty();   // `dirty` è già la prop del preset
  const toggleBypass = withDirty(() => bypass.set(!bypass.checked));

  const handleMouseDown = (e: React.MouseEvent<HTMLElement>) => {
    if (isInteractive(e.target)) return;

    let x = e.clientX;
    let y = e.clientY;
    let released = false;

    // Osserva il rilascio da SUBITO, non solo dopo che la promise di beginWindowDrag si
    // risolve: WKWebView consegna i messaggi della bridge in modo asincrono, quindi la risposta
    // puo' arrivare quando il bottone del mouse e' gia' stato rilasciato (un semplice click e'
    // mousedown+mouseup, spesso piu' veloce del giro nativo). Senza questo listener immediato,
    // quel "false" tardivo armerebbe comunque il ripiego, che poi seguirebbe il cursore a
    // bottone alzato finche' un mouseup qualsiasi, altrove nella pagina, non lo fermasse per
    // puro caso: e' l'errore critico corretto in questo giro.
    const markReleased = () => { released = true; };
    window.addEventListener("mouseup", markReleased, { once: true });

    void backend.beginWindowDrag().then((started) => {
      window.removeEventListener("mouseup", markReleased);

      // Il trascinamento nativo e' partito (AppKit segue lui il puntatore da qui in poi), oppure
      // il bottone e' gia' stato rilasciato (vedi sopra): in entrambi i casi non c'e' niente da
      // avviare.
      if (started || released) return;

      // Il ripiego (vedi backend.beginWindowDrag): l'evento di mousedown originale non e' piu'
      // quello corrente lato nativo, e performWindowDragWithEvent non e' partito. Seguiamo noi
      // i mousemove e chiediamo lo spostamento a mano, un delta alla volta.
      const move = (ev: MouseEvent) => {
        // Difesa in profondita': se il bottone non e' piu' premuto ma il mouseup non e' ancora
        // passato di qui (intercettato da un altro elemento, o un ordine di eventi diverso da
        // quello previsto), fermiamoci comunque piuttosto che inseguire il cursore a vuoto.
        if (ev.buttons === 0) { stop(); return; }
        const dx = ev.clientX - x;
        const dy = ev.clientY - y;
        x = ev.clientX;
        y = ev.clientY;
        if (dx !== 0 || dy !== 0) void backend.moveWindowBy(dx, dy);
      };
      const stop = () => {
        window.removeEventListener("mousemove", move);
        window.removeEventListener("mouseup", stop);
      };

      window.addEventListener("mousemove", move);
      window.addEventListener("mouseup", stop);
    });
  };

  const handleDoubleClick = (e: React.MouseEvent<HTMLElement>) => {
    if (isInteractive(e.target)) return;
    void backend.toggleWindowZoom();
  };

  return (
    <header
      className="flex h-10 shrink-0 items-center gap-2.5 px-1"
      // Sempre un valore esplicito, mai `undefined`: a 0 il padding e' comunque "0px", non
      // l'assenza della proprieta' — la differenza conta per chi legge lo stile calcolato.
      style={{ paddingLeft: `${Math.round(trafficLightWidth / scale)}px` }}
      onMouseDown={handleMouseDown}
      onDoubleClick={handleDoubleClick}
    >
      <Logo />
      <Button variant="secondary" size="icon-xs" aria-label="Undo" className={iconBtn}><Undo2 /></Button>
      <Button variant="secondary" size="icon-xs" aria-label="Redo" className={iconBtn}><Redo2 /></Button>

      {/* Slot preset: incasso con frecce ai lati e il nome al centro. */}
      <div className="sx-preset mx-auto flex h-7 w-full max-w-95 items-center overflow-hidden rounded-control bg-well shadow-well">
        <button type="button" aria-label="Previous preset" onClick={onPrev} className="flex h-full w-7 items-center justify-center text-text-dim hover:bg-foreground/5 hover:text-foreground">
          <ChevronLeft className="size-3.5" />
        </button>
        <button type="button" onClick={onBrowse} className="flex h-full flex-1 items-center justify-center gap-2 font-mono text-xs text-foreground hover:bg-foreground/5">
          <small className="text-2xs tracking-wider text-text-dim uppercase">{preset.cat}</small>
          {preset.name}
          {dirty ? " *" : ""}
          <LayoutGrid className="size-3 text-text-dim" />
        </button>
        <button type="button" aria-label="Next preset" onClick={onNext} className="flex h-full w-7 items-center justify-center text-text-dim hover:bg-foreground/5 hover:text-foreground">
          <ChevronRight className="size-3.5" />
        </button>
      </div>

      <Button variant="secondary" size="icon-xs" aria-label="Save" className={iconBtn}><Save /></Button>
      <Button
        variant="secondary"
        size="xs"
        aria-pressed={bypass.checked}
        onClick={toggleBypass}
        className={`sx-hbtn h-6.5! rounded-control! text-2xs tracking-wider uppercase ${bypass.checked ? "sx-on text-env [text-shadow:var(--tglow)]" : "text-muted-foreground"}`}
      >
        Bypass
      </Button>
      <Button variant="secondary" size="icon-xs" aria-label="Settings" className={iconBtn} onClick={onSettings}><Settings /></Button>
    </header>
  );
}

/**
 * Il marchio: simbolo e wordmark da Resources/branding, la sorgente unica degli asset (la stessa
 * che fa le icone del plugin). Il simbolo e' la variante `simple`, senza filtri SVG: a 30 px il
 * bagliore lo da' il CSS (`.sx-mark`), non un feGaussianBlur rasterizzato a ogni frame. Il
 * wordmark e' la geometria tracciata delle lettere di build.mjs, non testo: cosi' la E a tre
 * barre resta quella del logo su qualsiasi font di sistema.
 *
 * Con `children` il wordmark lascia il posto a una scritta (il titolo del browser dei preset).
 */
export function Logo({ children }: { children?: string }) {
  return (
    <div className="sx-logo flex items-center gap-2.5 text-[13px] font-semibold tracking-[0.22em] text-foreground">
      <img src={mark} alt="Xerum" width={30} height={30} className="sx-mark size-7.5 shrink-0" />
      {children ? <span>{children}</span> : <Wordmark />}
    </div>
  );
}

/** "XERUM" come nel logo: le cinque lettere tracciate, viewBox ritagliato sul wordmark. */
function Wordmark() {
  return (
    <svg className="sx-wordmark h-[11px] w-auto" viewBox="188 869 879 88" role="img" aria-label="XERUM">
      <defs>
        <linearGradient id="sx-letters" x1="0" y1="0" x2="0" y2="1">
          <stop stopColor="#f5efff" />
          <stop offset=".53" stopColor="#c3e5fa" />
          <stop offset="1" stopColor="#edf8ff" />
        </linearGradient>
      </defs>
      <g fill="url(#sx-letters)" fillRule="evenodd">
        <path d="M188 869 H207 L242 902 L278 869 H297 L253 912 L297 956 H278 L242 922 L207 956 H188 L231 912 Z" />
        <path d="M392 869 H481 V882 H392 Z M392 906 H478 V919 H392 Z M392 943 H481 V956 H392 Z" />
        <path d="M580 956 V869 H637 Q672 869 672 897 Q672 917 650 924 L675 956 H657 L633 926 H594 V956 Z M594 882 V913 H635 Q658 913 658 897 Q658 882 636 882 Z" />
        <path d="M770 869 H784 V922 Q784 943 817 943 Q851 943 851 922 V869 H865 V923 Q865 957 817 957 Q770 957 770 923 Z" />
        <path d="M962 956 V869 H980 L1015 911 L1049 869 H1067 V956 H1053 V888 L1015 934 L976 888 V956 Z" />
      </g>
    </svg>
  );
}
