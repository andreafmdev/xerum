import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type KeyboardEventHandler,
  type MouseEventHandler,
  type PointerEventHandler,
} from "react";

export type UseDragValueOptions = {
  /** Valore corrente, normalizzato 0..1. */
  value: number;
  /** Valore ripristinato dal doppio click. Default 0. */
  defaultValue?: number;
  onChange: (v: number) => void;
  /** Incremento per rotella e frecce. Default 0.01. Shift = step/10. */
  step?: number;
  /** Pixel di trascinamento per percorrere l'intero range. Default 200. Shift = ×10 (fine). */
  sensitivity?: number;
  /** "y": su = +. "x": destra = +. Default "y". */
  axis?: "y" | "x";
  disabled?: boolean;
};

export type UseDragValueResult = {
  ref: (el: HTMLElement | null) => void;
  handlers: {
    onPointerDown: PointerEventHandler<HTMLElement>;
    onPointerMove: PointerEventHandler<HTMLElement>;
    onPointerUp: PointerEventHandler<HTMLElement>;
    onPointerCancel: PointerEventHandler<HTMLElement>;
    onDoubleClick: MouseEventHandler<HTMLElement>;
    onKeyDown: KeyboardEventHandler<HTMLElement>;
  };
  dragging: boolean;
};

type Origin = { pos: number; value: number; shift: boolean };

const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

export function useDragValue({
  value,
  defaultValue = 0,
  onChange,
  step = 0.01,
  sensitivity = 200,
  axis = "y",
  disabled = false,
}: UseDragValueOptions): UseDragValueResult {
  const [dragging, setDragging] = useState(false);
  const origin = useRef<Origin | null>(null);
  const [node, setNode] = useState<HTMLElement | null>(null);

  // Ultimi valori senza rifare i listener a ogni render.
  const latest = useRef({ value, onChange, disabled, step });
  latest.current = { value, onChange, disabled, step };

  const emit = useCallback((next: number) => {
    const v = clamp01(next);
    if (v !== latest.current.value) latest.current.onChange(v);
  }, []);

  const readPos = useCallback(
    (e: { clientX: number; clientY: number }) => (axis === "y" ? e.clientY : e.clientX),
    [axis],
  );

  const onPointerDown: PointerEventHandler<HTMLElement> = useCallback(
    (e) => {
      if (latest.current.disabled || e.button !== 0) return;
      e.preventDefault();
      e.currentTarget.setPointerCapture?.(e.pointerId);
      origin.current = { pos: readPos(e), value: latest.current.value, shift: e.shiftKey };
      setDragging(true);
    },
    [readPos],
  );

  const onPointerMove: PointerEventHandler<HTMLElement> = useCallback(
    (e) => {
      const o = origin.current;
      if (!o) return;
      if (o.shift !== e.shiftKey) {
        // Cambio modalità fine/normale: riancora per evitare salti.
        origin.current = { pos: readPos(e), value: latest.current.value, shift: e.shiftKey };
        return;
      }
      const sens = e.shiftKey ? sensitivity * 10 : sensitivity;
      const delta = axis === "y" ? o.pos - e.clientY : e.clientX - o.pos;
      emit(o.value + delta / sens);
    },
    [axis, emit, readPos, sensitivity],
  );

  const endDrag: PointerEventHandler<HTMLElement> = useCallback((e) => {
    if (!origin.current) return;
    origin.current = null;
    if (e.currentTarget.hasPointerCapture?.(e.pointerId)) {
      e.currentTarget.releasePointerCapture?.(e.pointerId);
    }
    setDragging(false);
  }, []);

  const onDoubleClick: MouseEventHandler<HTMLElement> = useCallback(() => {
    if (latest.current.disabled) return;
    emit(defaultValue);
  }, [defaultValue, emit]);

  const onKeyDown: KeyboardEventHandler<HTMLElement> = useCallback(
    (e) => {
      if (latest.current.disabled) return;
      const s = e.shiftKey ? latest.current.step / 10 : latest.current.step;
      const v = latest.current.value;
      const targets: Record<string, number> = {
        ArrowUp: v + s,
        ArrowRight: v + s,
        ArrowDown: v - s,
        ArrowLeft: v - s,
        PageUp: v + 10 * latest.current.step,
        PageDown: v - 10 * latest.current.step,
        Home: 0,
        End: 1,
      };
      const next = targets[e.key];
      if (next === undefined) return;
      e.preventDefault();
      emit(next);
    },
    [emit],
  );

  // Wheel come listener nativo non-passive: React registra `wheel` passive e
  // preventDefault non fermerebbe lo scroll della pagina.
  // Il nodo è tenuto in state (non in un ref semplice) così l'effetto sotto
  // riparte quando l'elemento cambia identità (mount tardivo, remount per
  // `key`, rendering condizionale), anziché leggere una volta sola.
  const ref = useCallback((el: HTMLElement | null) => {
    setNode(el);
  }, []);

  useEffect(() => {
    if (!node) return;
    const onWheel = (e: WheelEvent) => {
      if (latest.current.disabled) return;
      e.preventDefault();
      const s = e.shiftKey ? latest.current.step / 10 : latest.current.step;
      emit(latest.current.value + (e.deltaY < 0 ? s : -s));
    };
    node.addEventListener("wheel", onWheel, { passive: false });
    return () => node.removeEventListener("wheel", onWheel);
  }, [node, emit]);

  return {
    ref,
    handlers: {
      onPointerDown,
      onPointerMove,
      onPointerUp: endDrag,
      onPointerCancel: endDrag,
      onDoubleClick,
      onKeyDown,
    },
    dragging,
  };
}
