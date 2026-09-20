import test from "node:test";
import assert from "node:assert/strict";
import { classifyTable, KNOWN_DUPLICATE_TABLES, KNOWN_TABLES } from "./import-serum-wavetables.mjs";

test("classifyTable riconosce una tavola nota", () => {
  const sha1 = Object.keys(KNOWN_TABLES)[0];
  const got = classifyTable(sha1);
  assert.equal(got.kind, "known");
  assert.equal(got.slug, KNOWN_TABLES[sha1]);
});

test("classifyTable segnala un duplicato noto, non lo confonde con uno slug sconosciuto", () => {
  for (const [sha1, expected] of Object.entries(KNOWN_DUPLICATE_TABLES)) {
    const got = classifyTable(sha1);
    assert.equal(got.kind, "duplicate");
    assert.equal(got.slug, expected.slug);
    assert.equal(got.duplicateOf, expected.duplicateOf);
    assert.equal(got.ofSha1, expected.ofSha1);
    assert.equal(got.maxDiff, expected.maxDiff);
  }
});

test("classifyTable non spedisce i due duplicati come tavole note", () => {
  for (const sha1 of Object.keys(KNOWN_DUPLICATE_TABLES)) assert.equal(sha1 in KNOWN_TABLES, false);
});

test("classifyTable restituisce unknown per uno sha1 mai visto", () => {
  const got = classifyTable("deadbeef");
  assert.equal(got.kind, "unknown");
});
