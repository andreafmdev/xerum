import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { describe, expect, it } from "vitest";

const css = readFileSync(resolve(import.meta.dirname, "index.css"), "utf8");

describe("reduced motion", () => {
  it("kills movement globally while keeping opacity legible", () => {
    // Una regola sola, non una per componente: ogni transizione nuova è coperta dal giorno uno.
    expect(css).toContain("@media (prefers-reduced-motion: reduce)");
    // reduced-motion-test-block:start
    // Il letterale "0.01ms" qui sotto è testo di un'asserzione, non una durata CSS scritta a
    // mano: check-motion.mjs non distingue un pattern in una regex di test dal valore reale
    // che verifica, quindi questa riga va marcata come le altre deroghe di questo file.
    expect(css).toMatch(/prefers-reduced-motion: reduce\)\s*\{[^}]*animation-duration:\s*0\.01ms/s);
    // reduced-motion-test-block:end
    expect(css).toMatch(/prefers-reduced-motion: reduce\)\s*\{[^}]*transition-property:\s*opacity/s);
  });
});
