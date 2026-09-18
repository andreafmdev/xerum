import { useState } from "react";
import { describe, expect, it, vi } from "vitest";
import { fireEvent, render, screen } from "@testing-library/react";
import { useDragValue, type UseDragValueOptions } from "./useDragValue";

type HarnessProps = Partial<Omit<UseDragValueOptions, "value" | "onChange">> & {
  initial?: number;
  onChange?: (v: number) => void;
};

function Harness({ initial = 0.5, onChange, ...opts }: HarnessProps) {
  const [value, setValue] = useState(initial);
  const { ref, handlers, dragging } = useDragValue({
    value,
    onChange: (v) => {
      setValue(v);
      onChange?.(v);
    },
    ...opts,
  });
  return (
    <div ref={ref} data-testid="target" data-dragging={dragging} tabIndex={0} {...handlers}>
      {value.toFixed(4)}
    </div>
  );
}

function LateMountHarness({ initial = 0.5, onChange, ...opts }: HarnessProps) {
  const [show, setShow] = useState(false);
  const [value, setValue] = useState(initial);
  const { ref, handlers, dragging } = useDragValue({
    value,
    onChange: (v) => {
      setValue(v);
      onChange?.(v);
    },
    ...opts,
  });
  return (
    <div>
      <button type="button" onClick={() => setShow(true)}>
        mount
      </button>
      {show && (
        <div ref={ref} data-testid="target" data-dragging={dragging} tabIndex={0} {...handlers}>
          {value.toFixed(4)}
        </div>
      )}
    </div>
  );
}

const target = () => screen.getByTestId("target");
const drag = (fromY: number, toY: number, extra: Record<string, unknown> = {}) => {
  fireEvent.pointerDown(target(), { clientY: fromY, clientX: 0, button: 0, pointerId: 1 });
  fireEvent.pointerMove(target(), { clientY: toY, clientX: 0, pointerId: 1, ...extra });
  fireEvent.pointerUp(target(), { clientY: toY, clientX: 0, pointerId: 1 });
};

describe("useDragValue", () => {
  it("increases when dragging up (default sensitivity 200px = full range)", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    drag(100, 60);
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("decreases when dragging down", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    drag(100, 140);
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.3, 5));
  });

  it("clamps to 0..1", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.9} onChange={onChange} />);
    drag(100, 0);
    expect(onChange).toHaveBeenLastCalledWith(1);
  });

  it("uses x axis when axis is 'x' (right = +)", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} axis="x" />);
    fireEvent.pointerDown(target(), { clientX: 100, clientY: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(target(), { clientX: 140, clientY: 0, pointerId: 1 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.7, 5));
  });

  it("is 10x finer with shift held during drag", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    fireEvent.pointerDown(target(), { clientY: 100, clientX: 0, button: 0, pointerId: 1, shiftKey: true });
    fireEvent.pointerMove(target(), { clientY: 60, clientX: 0, pointerId: 1, shiftKey: true });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.52, 5));
  });

  it("re-anchors when shift toggles mid-drag (no jump)", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    fireEvent.pointerDown(target(), { clientY: 100, clientX: 0, button: 0, pointerId: 1 });
    fireEvent.pointerMove(target(), { clientY: 60, clientX: 0, pointerId: 1 });      // -> 0.7
    fireEvent.pointerMove(target(), { clientY: 60, clientX: 0, pointerId: 1, shiftKey: true }); // re-anchor, no change
    fireEvent.pointerMove(target(), { clientY: 40, clientX: 0, pointerId: 1, shiftKey: true }); // +20px fine = +0.01
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.71, 5));
  });

  it("exposes dragging state", () => {
    render(<Harness />);
    expect(target()).toHaveAttribute("data-dragging", "false");
    fireEvent.pointerDown(target(), { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(target()).toHaveAttribute("data-dragging", "true");
    fireEvent.pointerUp(target(), { clientY: 0, clientX: 0, pointerId: 1 });
    expect(target()).toHaveAttribute("data-dragging", "false");
  });

  it("wheel up adds step, wheel down subtracts, shift makes it fine", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} step={0.05} />);
    fireEvent.wheel(target(), { deltaY: -100 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.55, 5));
    fireEvent.wheel(target(), { deltaY: 100 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.5, 5));
    fireEvent.wheel(target(), { deltaY: -100, shiftKey: true });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.505, 5));
  });

  it("double click resets to defaultValue", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.8} defaultValue={0.25} onChange={onChange} />);
    fireEvent.doubleClick(target());
    expect(onChange).toHaveBeenLastCalledWith(0.25);
  });

  it("keyboard: arrows, page, home, end, shift fine", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} step={0.01} />);
    const t = target();
    fireEvent.keyDown(t, { key: "ArrowUp" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.51, 5));
    fireEvent.keyDown(t, { key: "ArrowRight" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.52, 5));
    fireEvent.keyDown(t, { key: "ArrowDown" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.51, 5));
    fireEvent.keyDown(t, { key: "ArrowLeft" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.5, 5));
    fireEvent.keyDown(t, { key: "PageUp" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.6, 5));
    fireEvent.keyDown(t, { key: "PageDown" });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.5, 5));
    fireEvent.keyDown(t, { key: "ArrowUp", shiftKey: true });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.501, 5));
    fireEvent.keyDown(t, { key: "Home" });
    expect(onChange).toHaveBeenLastCalledWith(0);
    fireEvent.keyDown(t, { key: "End" });
    expect(onChange).toHaveBeenLastCalledWith(1);
  });

  it("does not call onChange when the value would not change", () => {
    const onChange = vi.fn();
    render(<Harness initial={0} onChange={onChange} />);
    fireEvent.keyDown(target(), { key: "Home" });
    fireEvent.keyDown(target(), { key: "ArrowDown" });
    drag(100, 200);
    expect(onChange).not.toHaveBeenCalled();
  });

  it("ignores everything when disabled", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} disabled />);
    drag(100, 0);
    fireEvent.wheel(target(), { deltaY: -100 });
    fireEvent.doubleClick(target());
    fireEvent.keyDown(target(), { key: "End" });
    expect(onChange).not.toHaveBeenCalled();
    expect(target()).toHaveAttribute("data-dragging", "false");
    expect(target()).not.toHaveFocus();
  });

  it("focuses the element on pointer down so keyboard control works after a grab", () => {
    render(<Harness initial={0.5} />);
    fireEvent.pointerDown(target(), { clientY: 0, clientX: 0, button: 0, pointerId: 1 });
    expect(target()).toHaveFocus();
  });

  it("ignores non-primary buttons", () => {
    const onChange = vi.fn();
    render(<Harness initial={0.5} onChange={onChange} />);
    fireEvent.pointerDown(target(), { clientY: 100, clientX: 0, button: 2, pointerId: 1 });
    fireEvent.pointerMove(target(), { clientY: 0, clientX: 0, pointerId: 1 });
    expect(onChange).not.toHaveBeenCalled();
  });

  it("attaches the wheel listener to an element mounted later", () => {
    const onChange = vi.fn();
    render(<LateMountHarness initial={0.5} onChange={onChange} step={0.05} />);
    fireEvent.click(screen.getByRole("button", { name: "mount" }));
    fireEvent.wheel(target(), { deltaY: -100 });
    expect(onChange).toHaveBeenLastCalledWith(expect.closeTo(0.55, 5));
  });
});
