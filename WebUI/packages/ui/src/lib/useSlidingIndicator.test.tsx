import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import { useSlidingIndicator } from "./indicator";

/**
 * Sostituto di `ResizeObserver` che registra ogni istanza creata e le sue chiamate, per
 * verificare che l'hook monti/scolleghi l'osservatore che possiede senza dipendere da un vero
 * layout (jsdom non ne ha: vedi src/test/setup.ts, che installa uno stub no-op di default).
 */
class MockResizeObserver {
  static instances: MockResizeObserver[] = [];
  observe = vi.fn();
  unobserve = vi.fn();
  disconnect = vi.fn();
  constructor(readonly callback: ResizeObserverCallback) {
    MockResizeObserver.instances.push(this);
  }
}

function withMockResizeObserver(run: () => void) {
  const Original = globalThis.ResizeObserver;
  MockResizeObserver.instances = [];
  globalThis.ResizeObserver = MockResizeObserver as unknown as typeof ResizeObserver;
  try {
    run();
  } finally {
    globalThis.ResizeObserver = Original;
  }
}

function Harness({ active = 0, count = 2, enabled }: { active?: number; count?: number; enabled?: boolean }) {
  const { containerRef, indicatorRef, itemRef, measured } = useSlidingIndicator<HTMLDivElement, HTMLButtonElement>(active, count, {
    enabled,
  });
  return (
    <div ref={containerRef} data-testid="container">
      <span ref={indicatorRef} data-testid="indicator" data-measured={measured} />
      {Array.from({ length: count }, (_, i) => (
        <button key={i} ref={itemRef(i)} data-testid={`item-${i}`} />
      ))}
    </div>
  );
}

describe("useSlidingIndicator", () => {
  it("reports not measured when jsdom performs no real layout", () => {
    render(<Harness />);
    expect(screen.getByTestId("indicator")).toHaveAttribute("data-measured", "false");
  });

  it("attaches a ResizeObserver on the container and the active item, and disconnects it on unmount", () => {
    withMockResizeObserver(() => {
      const { unmount } = render(<Harness active={0} count={2} />);
      // Un solo ResizeObserver per il ciclo di vita corrente, non uno per elemento osservato.
      expect(MockResizeObserver.instances).toHaveLength(1);
      const ro = MockResizeObserver.instances[0]!;
      expect(ro.observe).toHaveBeenCalledTimes(2);
      expect(ro.observe).toHaveBeenCalledWith(screen.getByTestId("container"));
      expect(ro.observe).toHaveBeenCalledWith(screen.getByTestId("item-0"));
      expect(ro.disconnect).not.toHaveBeenCalled();
      unmount();
      expect(ro.disconnect).toHaveBeenCalledTimes(1);
    });
  });

  it("does no work and attaches no observer when disabled (the `bar`/`plate` guard)", () => {
    withMockResizeObserver(() => {
      render(<Harness enabled={false} />);
      expect(MockResizeObserver.instances).toHaveLength(0);
      expect(screen.getByTestId("indicator")).toHaveAttribute("data-measured", "false");
    });
  });
});
