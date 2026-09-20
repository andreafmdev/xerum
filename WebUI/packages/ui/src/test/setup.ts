import "@testing-library/jest-dom/vitest";
import { afterEach } from "vitest";
import { cleanup } from "@testing-library/react";

afterEach(() => cleanup());

// jsdom non implementa pointer capture né ResizeObserver.
const proto = Element.prototype as Element & {
  setPointerCapture?: (id: number) => void;
  releasePointerCapture?: (id: number) => void;
  hasPointerCapture?: (id: number) => boolean;
};
proto.setPointerCapture ??= () => {};
proto.releasePointerCapture ??= () => {};
proto.hasPointerCapture ??= () => false;

if (typeof globalThis.ResizeObserver === "undefined") {
  class ResizeObserverStub {
    observe() {}
    unobserve() {}
    disconnect() {}
  }
  globalThis.ResizeObserver = ResizeObserverStub as unknown as typeof ResizeObserver;
}

// jsdom non implementa matchMedia: senza, ogni componente che chiede prefers-reduced-motion
// solleva invece di rispondere.
let reduced = false;

export function setReducedMotion(on: boolean) {
  reduced = on;
}

globalThis.matchMedia ??= ((query: string) =>
  ({
    matches: reduced && query.includes("prefers-reduced-motion"),
    media: query,
    onchange: null,
    addEventListener: () => {},
    removeEventListener: () => {},
    addListener: () => {},
    removeListener: () => {},
    dispatchEvent: () => false,
  }) as unknown as MediaQueryList) as typeof matchMedia;

afterEach(() => setReducedMotion(false));
