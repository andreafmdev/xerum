import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Keybed } from "./Keybed";

const noop = () => {};

// jsdom non implementa affatto la cattura del puntatore: setPointerCapture non esiste, e
// fireEvent.pointerEnter spara l'evento sintetico sul nodo che gli si passa, senza hit testing.
// Il glissando pero' vive proprio li': i tasti chiamano press() da onPointerEnter con
// buttons === 1. Per la spec Pointer Events, finche' un elemento tiene la cattura
// pointerover/enter/out/leave arrivano SOLO al target della cattura, e i discendenti sotto il
// puntatore non ricevono nulla — cioe' un setPointerCapture rimesso sul contenitore ucciderebbe
// il glissando nella WebView vera senza che un test "sintetico" se ne accorga.
//
// Quindi qui la cattura si modella a mano: uno stub che registra chi la prende, e un drag che
// consegna il pointerenter al target della cattura quando ce n'e' uno, esattamente come farebbe
// un browser. Se qualcuno reintroduce la cattura, il test sotto fallisce.
let captured: Element | null = null;

beforeEach(() => {
  captured = null;
  Element.prototype.setPointerCapture = function (this: Element) { captured = this; };
  Element.prototype.releasePointerCapture = function () { captured = null; };
  Element.prototype.hasPointerCapture = function (this: Element) { return captured === this; };
});

afterEach(() => {
  delete (Element.prototype as Partial<Element>).setPointerCapture;
  delete (Element.prototype as Partial<Element>).releasePointerCapture;
  delete (Element.prototype as Partial<Element>).hasPointerCapture;
});

/** Il puntatore entra in `key` col tasto premuto, ritargettato sulla cattura come da spec. */
const glideOnto = (key: Element, pointerId = 1) =>
  fireEvent.pointerEnter(captured ?? key, { buttons: 1, pointerId });

describe("Keybed", () => {
  it("disegna sette tasti bianchi e cinque neri per ottava", () => {
    render(<Keybed firstNote={48} octaves={2} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={noop} />);
    expect(screen.getAllByTestId("key-white")).toHaveLength(14);
    expect(screen.getAllByTestId("key-black")).toHaveLength(10);
  });

  it("il primo tasto bianco è firstNote", () => {
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={noop} />);
    expect(screen.getAllByTestId("key-white")[0]).toHaveAttribute("data-note", "48");
  });

  it("premere un tasto manda noteOn con la velocity data", () => {
    const onNoteOn = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.6} onNoteOn={onNoteOn} onNoteOff={noop} onAllNotesOff={noop} />);
    fireEvent.pointerDown(screen.getAllByTestId("key-white")[2], { button: 0, pointerId: 1 });
    expect(onNoteOn).toHaveBeenCalledWith(52, 0.6);
  });

  it("rilasciare manda noteOff", () => {
    const onNoteOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.6} onNoteOn={noop} onNoteOff={onNoteOff} onAllNotesOff={noop} />);
    const key = screen.getAllByTestId("key-white")[0];
    fireEvent.pointerDown(key, { button: 0, pointerId: 1 });
    fireEvent.pointerUp(key, { pointerId: 1 });
    expect(onNoteOff).toHaveBeenCalledWith(48);
  });

  it("trascinare da un tasto all'altro spegne il primo e accende il secondo", () => {
    const onNoteOn = vi.fn();
    const onNoteOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={onNoteOn} onNoteOff={onNoteOff} onAllNotesOff={noop} />);
    const keys = screen.getAllByTestId("key-white");
    fireEvent.pointerDown(keys[0], { button: 0, pointerId: 1 });

    // Nessuno deve aver preso la cattura: se l'avesse presa il contenitore, glideOnto
    // consegnerebbe il pointerenter a lui (che non ha handler) e le due attese sotto
    // fallirebbero — che e' il punto.
    expect(captured).toBeNull();
    glideOnto(keys[1]);
    expect(onNoteOff).toHaveBeenCalledWith(48);
    expect(onNoteOn).toHaveBeenLastCalledWith(50, 0.8);

    // E il glissando continua: terzo tasto, sempre senza un nuovo pointerdown.
    glideOnto(keys[2]);
    expect(onNoteOff).toHaveBeenCalledWith(50);
    expect(onNoteOn).toHaveBeenLastCalledWith(52, 0.8);
  });

  it("pointercancel chiama onAllNotesOff", () => {
    const onAllNotesOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={onAllNotesOff} />);
    fireEvent.pointerCancel(screen.getAllByTestId("key-white")[0], { pointerId: 1 });
    expect(onAllNotesOff).toHaveBeenCalled();
  });

  it("pointerleave dal contenitore rilascia la nota premuta", () => {
    const onNoteOff = vi.fn();
    const { container } = render(
      <Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={onNoteOff} onAllNotesOff={noop} />,
    );
    const keybed = container.querySelector('[data-slot="keybed"]')!;
    fireEvent.pointerDown(screen.getAllByTestId("key-white")[0], { button: 0, pointerId: 1 });
    fireEvent.pointerLeave(keybed, { pointerId: 1 });
    expect(onNoteOff).toHaveBeenCalledWith(48);
  });

  it("blur della finestra chiama onAllNotesOff, come pointercancel", () => {
    const onAllNotesOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={onAllNotesOff} />);
    fireEvent.pointerDown(screen.getAllByTestId("key-white")[0], { button: 0, pointerId: 1 });
    fireEvent(window, new Event("blur"));
    expect(onAllNotesOff).toHaveBeenCalled();
  });

  it("smontato, non ascolta piu' il blur della finestra", () => {
    const onAllNotesOff = vi.fn();
    const { unmount } = render(
      <Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={onAllNotesOff} />,
    );
    unmount();
    fireEvent(window, new Event("blur"));
    expect(onAllNotesOff).not.toHaveBeenCalled();
  });

  it("accende i tasti dal mask senza rirenderizzare", () => {
    let emit: ((m: readonly [number, number, number, number]) => void) | null = null;
    const subscribeNotes = (cb: (m: readonly [number, number, number, number]) => void) => {
      emit = cb;
      return () => { emit = null; };
    };
    render(
      <Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop}
              onAllNotesOff={noop} subscribeNotes={subscribeNotes} />,
    );
    const keys = screen.getAllByTestId("key-white");
    expect(keys[0]).toHaveAttribute("data-active", "false");
    emit!([0, 1 << 16, 0, 0]); // nota 48: word n1 (32..63), bit 16 = 48 - 32
    expect(keys[0]).toHaveAttribute("data-active", "true");
    emit!([0, 0, 0, 0]);
    expect(keys[0]).toHaveAttribute("data-active", "false");
  });
});
