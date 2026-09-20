import { beforeEach, describe, expect, it } from "vitest";
import { consumeFirstBoot, resetFirstBoot } from "./boot";

describe("consumeFirstBoot", () => {
  beforeEach(() => resetFirstBoot());

  it("is true once and false afterwards", () => {
    expect(consumeFirstBoot()).toBe(true);
    expect(consumeFirstBoot()).toBe(false);
    expect(consumeFirstBoot()).toBe(false);
  });
});
