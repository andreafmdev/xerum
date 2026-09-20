import test from "node:test";
import assert from "node:assert/strict";
import { CREDITS_TAIL_MARKER, mergeCredits, preservedTail, renderAutoCredits } from "./fetch-wavetables.mjs";

const SERUM_NOTE = [
  CREDITS_TAIL_MARKER,
  "",
  "## Tavole importate da Serum",
  "",
  "**Licenza: non risolta.** Queste sette tavole non sono ridistribuibili senza permesso scritto.",
  "",
].join("\n");

test("preservedTail e' vuoto senza marcatore: niente da preservare alla prima migrazione", () => {
  assert.equal(preservedTail(""), "");
  assert.equal(preservedTail("# Wavetables\n\nqualcosa senza marcatore\n"), "");
});

test("preservedTail porta tutto cio' che segue il marcatore, marcatore incluso", () => {
  const existing = `${renderAutoCredits("2026-01-01")}${SERUM_NOTE}`;
  assert.equal(preservedTail(existing), SERUM_NOTE);
});

// Il fix che questo test chiude: fetch-wavetables.mjs riscriveva CREDITS.md da zero, ed era
// l'unico posto dove sta scritto che le sette tavole retro-* non sono ridistribuibili senza
// permesso. Rigenerarlo non deve poter cancellare quella nota.
test("mergeCredits sopravvive alla nota sulla licenza Serum attraverso una rigenerazione", () => {
  const existing = `${renderAutoCredits("2026-01-01")}${SERUM_NOTE}`;
  const regenerated = mergeCredits(existing, "2026-02-02");

  assert.match(regenerated, /## Tavole importate da Serum/);
  assert.match(regenerated, /non sono ridistribuibili senza permesso scritto/);
  // La parte generata e' cambiata (nuova data)...
  assert.match(regenerated, /2026-02-02/);
  // ...ma la nota preservata e' testuale, non rigenerata.
  assert.ok(regenerated.endsWith(SERUM_NOTE));
});

test("mergeCredits su un file senza marcatore lascia comunque il marcatore per la prossima volta", () => {
  const merged = mergeCredits("qualsiasi cosa senza marcatore", "2026-01-01");
  assert.ok(merged.includes(CREDITS_TAIL_MARKER));
});

test("mergeCredits su un file inesistente produce solo la parte generata piu' il marcatore", () => {
  const merged = mergeCredits("", "2026-01-01");
  assert.equal(merged, `${renderAutoCredits("2026-01-01")}${CREDITS_TAIL_MARKER}\n`);
});
