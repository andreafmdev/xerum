import { useCallback, useEffect, useRef } from "react";
import { cn } from "@/lib/utils";

export type KeybedNoteMask = readonly [number, number, number, number];

export type KeybedProps = {
  /** Nota MIDI del primo tasto bianco visibile. */
  firstNote: number;
  /** Quante ottave mostrare. */
  octaves?: number;
  /** Velocity 0..1 assegnata a ogni nota suonata col puntatore. */
  velocity: number;
  onNoteOn: (note: number, velocity: number) => void;
  onNoteOff: (note: number) => void;
  onAllNotesOff: () => void;
  /**
   * Sottoscrizione al mask delle note attive. NON e' una prop di valore: se lo fosse, i 30 frame
   * al secondo dell'host farebbero rirenderizzare 48 nodi trenta volte al secondo dentro una
   * WebView. Il componente si iscrive una volta e muta `data-active` via ref.
   */
  subscribeNotes?: (cb: (mask: KeybedNoteMask) => void) => () => void;
  className?: string;
};

/** I semitoni dei tasti bianchi e di quelli neri dentro un'ottava. */
const WHITE = [0, 2, 4, 5, 7, 9, 11];
const BLACK = [1, 3, 6, 8, 10];

/** Quanti tasti bianchi stanno alla sinistra di un nero: decide la sua posizione orizzontale. */
const WHITE_BEFORE: Record<number, number> = { 1: 1, 3: 2, 6: 4, 8: 5, 10: 6 };

// Stesso bit test di `isNoteActive` in WebUI/src/juce/backend.ts, duplicato apposta: @xerum/ui
// non puo' dipendere dalla app shell, e questa primitiva non deve sapere nulla del bridge JUCE.
const bitOf = (mask: KeybedNoteMask, note: number) =>
  ((mask[note >> 5] ?? 0) & (1 << (note & 31))) !== 0;

/** Tastiera suonabile col puntatore, con i tasti che si accendono su cio' che il motore suona. */
export function Keybed({
  firstNote,
  octaves = 4,
  velocity,
  onNoteOn,
  onNoteOff,
  onAllNotesOff,
  subscribeNotes,
  className,
}: KeybedProps) {
  // La nota attualmente premuta col puntatore. Un ref e non uno stato: cambiarla non deve
  // ridisegnare la tastiera, e il gestore di pointermove la legge sempre aggiornata.
  const held = useRef<number | null>(null);
  const keys = useRef(new Map<number, HTMLElement>());
  const latest = useRef({ velocity, onNoteOn, onNoteOff, onAllNotesOff });
  latest.current = { velocity, onNoteOn, onNoteOff, onAllNotesOff };

  // `data-pressed` e' l'UNICO attributo che pilota l'animazione del tasto (transform + filter),
  // ed e' alimentato da due fonti che non devono mai divergere: il puntatore qui sotto (feedback
  // immediato, senza aspettare il motore) e la mask di `subscribeNotes` piu' in basso (una nota
  // arrivata via MIDI). Se i due percorsi scrivessero attributi diversi, un tasto suonato dal
  // vivo e uno suonato col mouse potrebbero apparire diversi senza che nessun test se ne accorga.
  // `data-active` resta separato apposta: quello conferma che il motore sta davvero suonando la
  // nota (puo' non arrivare mai, per voice stealing), `data-pressed` e' solo il feedback tattile.
  const setPressed = useCallback((note: number, pressed: boolean) => {
    const el = keys.current.get(note);
    if (el) el.dataset.pressed = String(pressed);
  }, []);

  const press = useCallback((note: number) => {
    if (held.current === note) return;
    if (held.current !== null) {
      latest.current.onNoteOff(held.current);
      setPressed(held.current, false);
    }
    held.current = note;
    latest.current.onNoteOn(note, latest.current.velocity);
    setPressed(note, true);
  }, [setPressed]);

  const release = useCallback(() => {
    if (held.current === null) return;
    latest.current.onNoteOff(held.current);
    setPressed(held.current, false);
    held.current = null;
  }, [setPressed]);

  const panic = useCallback(() => {
    if (held.current !== null) setPressed(held.current, false);
    held.current = null;
    latest.current.onAllNotesOff();
  }, [setPressed]);

  // Una nota appesa non e' un difetto estetico: suona per sempre. Il puntatore puo' sparire
  // senza pointerup — cambio di finestra, pointercancel della WebView — quindi si chiude anche
  // sul blur della finestra.
  useEffect(() => {
    window.addEventListener("blur", panic);
    return () => window.removeEventListener("blur", panic);
  }, [panic]);

  useEffect(() => {
    if (!subscribeNotes) return;
    return subscribeNotes((mask) => {
      for (const [note, el] of keys.current) {
        const on = String(bitOf(mask, note));
        el.dataset.active = on;
        el.dataset.pressed = on;
      }
    });
  }, [subscribeNotes]);

  // Un solo callback ref stabile per tutti i tasti, invece di una fabbrica chiamata dentro il
  // map: `ref={registerKey}` costruirebbe una funzione nuova a ogni render per ognuno dei
  // 48 tasti, e React li scollegherebbe e riattaccherebbe tutti ad ogni render. La nota si legge
  // da `data-note` all'attach; la funzione di cleanup restituita (React 19) chiude su quella
  // stessa nota, non sulla prop del map.
  const registerKey = useCallback((el: HTMLElement | null) => {
    if (!el) return;
    const note = Number(el.dataset.note);
    keys.current.set(note, el);
    return () => { keys.current.delete(note); };
  }, []);

  const whiteNotes: number[] = [];
  const blackNotes: { note: number; index: number }[] = [];

  for (let o = 0; o < octaves; o++) {
    for (const semi of WHITE) whiteNotes.push(firstNote + o * 12 + semi);
    for (const semi of BLACK) blackNotes.push({ note: firstNote + o * 12 + semi, index: o * 7 + WHITE_BEFORE[semi]! });
  }

  const unit = 100 / whiteNotes.length;
  // `data-key` e' il gancio con cui una app ridisegna il materiale dei tasti via CSS (tasti di
  // vetro sopra un'aurora, ad esempio) senza toccare la primitiva: i `data-testid` restano per i
  // test e non vanno usati come selettori di stile.

  return (
    <div
      data-slot="keybed"
      className={cn("relative flex h-full w-full select-none touch-none", className)}
      // Niente setPointerCapture qui, ed e' una scelta, non una dimenticanza. Il glissando vive
      // sui pointerenter dei singoli tasti, e per la spec Pointer Events finche' un elemento
      // tiene la cattura pointerover/enter/out/leave arrivano SOLO al target della cattura: i
      // discendenti sotto il puntatore non ricevono piu' nulla. Nella WebView vera, con la
      // cattura sul contenitore, trascinare suonava il primo tasto e poi silenzio.
      //
      // La sicurezza che la cattura voleva dare — non lasciare una nota appesa se il puntatore
      // se ne va — la da' gia' onPointerLeave qui sotto, e senza cattura un puntatore che
      // rientra nella striscia col tasto premuto riprende a suonare invece di essere ignorato.
      onPointerUp={release}
      onPointerLeave={release}
      onPointerCancel={panic}
    >
      {whiteNotes.map((note) => (
        <div
          key={note}
          ref={registerKey}
          data-testid="key-white"
          data-key="white"
          data-note={note}
          data-active="false"
          data-pressed="false"
          className={cn(
            "relative flex h-full flex-1 items-end justify-center rounded-b-[3px] border-r border-edge-dark pb-1 last:border-r-0",
            "bg-linear-to-b from-key-ivory-hi to-key-ivory-lo",
            "data-[active=true]:from-(--tone) data-[active=true]:to-key-ivory-active-lo",
            // Un tasto premuto dal mouse e una nota arrivata via MIDI passano per lo stesso
            // attributo (vedi `setPressed`/subscribeNotes sopra): la stessa transizione, quindi,
            // vale identica per entrambi i percorsi, senza che i due possano divergere in modo
            // visibile solo mettendoli a confronto.
            "transition-[transform,filter] duration-(--dur-press) ease-snap",
            "data-[pressed=true]:translate-y-px data-[pressed=true]:brightness-90",
          )}
          onPointerDown={(e) => { if (e.button === 0) press(note); }}
          onPointerEnter={(e) => { if (e.buttons === 1) press(note); }}
        >
          {/* Il nome dell'ottava sul DO, come su una master keyboard: e' l'unico riferimento
              con cui si legge dove si e' sulla tastiera dopo uno shift di ottava. */}
          {note % 12 === 0 && (
            <span aria-hidden data-part="key-label" className="pointer-events-none font-mono text-[8px] tracking-wider text-key-ebony-lo/45 select-none">
              C{note / 12 - 1}
            </span>
          )}
        </div>
      ))}

      {blackNotes.map(({ note, index }) => (
        <div
          key={note}
          ref={registerKey}
          data-testid="key-black"
          data-key="black"
          data-note={note}
          data-active="false"
          data-pressed="false"
          className={cn(
            "absolute top-0 z-10 h-[62%] rounded-b-[3px] shadow-cap",
            "bg-linear-to-b from-key-ebony-hi to-key-ebony-lo",
            "data-[active=true]:from-(--tone) data-[active=true]:to-key-ebony-active-lo",
            "transition-[transform,filter] duration-(--dur-press) ease-snap",
            "data-[pressed=true]:translate-y-px data-[pressed=true]:brightness-90",
          )}
          style={{ left: `${index * unit - unit * 0.3}%`, width: `${unit * 0.6}%` }}
          onPointerDown={(e) => { if (e.button === 0) press(note); }}
          onPointerEnter={(e) => { if (e.buttons === 1) press(note); }}
        />
      ))}
    </div>
  );
}
