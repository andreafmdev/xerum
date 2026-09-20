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

  const press = useCallback((note: number) => {
    if (held.current === note) return;
    if (held.current !== null) latest.current.onNoteOff(held.current);
    held.current = note;
    latest.current.onNoteOn(note, latest.current.velocity);
  }, []);

  const release = useCallback(() => {
    if (held.current === null) return;
    latest.current.onNoteOff(held.current);
    held.current = null;
  }, []);

  const panic = useCallback(() => {
    held.current = null;
    latest.current.onAllNotesOff();
  }, []);

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
      for (const [note, el] of keys.current) el.dataset.active = String(bitOf(mask, note));
    });
  }, [subscribeNotes]);

  const register = useCallback((note: number) => (el: HTMLElement | null) => {
    if (el) keys.current.set(note, el);
    else keys.current.delete(note);
  }, []);

  const whiteNotes: number[] = [];
  const blackNotes: { note: number; index: number }[] = [];

  for (let o = 0; o < octaves; o++) {
    for (const semi of WHITE) whiteNotes.push(firstNote + o * 12 + semi);
    for (const semi of BLACK) blackNotes.push({ note: firstNote + o * 12 + semi, index: o * 7 + WHITE_BEFORE[semi]! });
  }

  const unit = 100 / whiteNotes.length;

  return (
    <div
      data-slot="keybed"
      className={cn("relative flex h-full w-full select-none touch-none", className)}
      onPointerDown={(e) => {
        if (e.button !== 0) return;
        e.currentTarget.setPointerCapture?.(e.pointerId);
      }}
      onPointerUp={release}
      onPointerLeave={release}
      onPointerCancel={panic}
    >
      {whiteNotes.map((note) => (
        <div
          key={note}
          ref={register(note)}
          data-testid="key-white"
          data-note={note}
          data-active="false"
          className={cn(
            "h-full flex-1 rounded-b-[3px] border-r border-edge-dark last:border-r-0",
            "bg-linear-to-b from-key-ivory-hi to-key-ivory-lo",
            "data-[active=true]:from-(--tone) data-[active=true]:to-key-ivory-active-lo",
          )}
          onPointerDown={(e) => { if (e.button === 0) press(note); }}
          onPointerEnter={(e) => { if (e.buttons === 1) press(note); }}
        />
      ))}

      {blackNotes.map(({ note, index }) => (
        <div
          key={note}
          ref={register(note)}
          data-testid="key-black"
          data-note={note}
          data-active="false"
          className={cn(
            "absolute top-0 z-10 h-[62%] rounded-b-[3px] shadow-cap",
            "bg-linear-to-b from-key-ebony-hi to-key-ebony-lo",
            "data-[active=true]:from-(--tone) data-[active=true]:to-key-ebony-active-lo",
          )}
          style={{ left: `${index * unit - unit * 0.3}%`, width: `${unit * 0.6}%` }}
          onPointerDown={(e) => { if (e.button === 0) press(note); }}
          onPointerEnter={(e) => { if (e.buttons === 1) press(note); }}
        />
      ))}
    </div>
  );
}
