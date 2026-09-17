/**
 * Placeholder for JUCE 8 WebView bridge (phase 2).
 * Will wrap window.__JUCE__.backend event listeners and native functions.
 */

export type JuceBackend = {
  addEventListener: (eventId: string, fn: (payload: unknown) => void) => void;
  emitEvent: (eventId: string, payload?: unknown) => void;
};

declare global {
  interface Window {
    __JUCE__?: {
      backend?: JuceBackend;
    };
  }
}

export function getJuceBackend(): JuceBackend | undefined {
  return window.__JUCE__?.backend;
}
