import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { Keybed } from "./Keybed";

const noop = () => {};

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
    fireEvent.pointerEnter(keys[1], { buttons: 1, pointerId: 1 });
    expect(onNoteOff).toHaveBeenCalledWith(48);
    expect(onNoteOn).toHaveBeenLastCalledWith(50, 0.8);
  });

  it("pointercancel chiama onAllNotesOff", () => {
    const onAllNotesOff = vi.fn();
    render(<Keybed firstNote={48} octaves={1} velocity={0.8} onNoteOn={noop} onNoteOff={noop} onAllNotesOff={onAllNotesOff} />);
    fireEvent.pointerCancel(screen.getAllByTestId("key-white")[0], { pointerId: 1 });
    expect(onAllNotesOff).toHaveBeenCalled();
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
