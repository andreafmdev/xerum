import { describe, expect, it } from "vitest";
import { fmt } from "./format";

describe("fmt", () => {
  it("percent", () => expect(fmt.pct(0.256)).toBe("26 %"));
  it("frame index 1..64", () => {
    expect(fmt.frame(0)).toBe("1.0");
    expect(fmt.frame(1)).toBe("64.0");
  });
  it("cents and signed cents", () => {
    expect(fmt.cents(0.5)).toBe("50 ct");
    expect(fmt.fine(0.5)).toBe("0 ct");
    expect(fmt.fine(1)).toBe("100 ct");
    expect(fmt.fine(0)).toBe("-100 ct");
  });
  it("level in dB with -inf at zero", () => {
    expect(fmt.db(0)).toBe("-inf");
    expect(fmt.db(1)).toBe("0.0 dB");
    expect(fmt.db(0.5)).toBe("-6.0 dB");
  });
  it("master volume with +6 dB headroom", () => {
    expect(fmt.volume(1)).toBe("6.0 dB");
    expect(fmt.volume(0)).toBe("-inf");
  });
  it("cutoff 20 Hz .. 20 kHz", () => {
    expect(fmt.hz(0)).toBe("20 Hz");
    expect(fmt.hz(1)).toBe("20.00 kHz");
    expect(fmt.hz(0.5)).toBe("632 Hz");
  });
  it("drive in dB", () => expect(fmt.drive(0.5)).toBe("12.0 dB"));
  it("pan centre / left / right", () => {
    expect(fmt.pan(0.5)).toBe("C");
    expect(fmt.pan(0)).toBe("50 L");
    expect(fmt.pan(1)).toBe("50 R");
  });
  it("glide quadratic ms", () => {
    expect(fmt.glide(0)).toBe("0 ms");
    expect(fmt.glide(1)).toBe("2000 ms");
  });
  it("envelope time ms / s", () => {
    expect(fmt.envMs(0)).toBe("1 ms");
    expect(fmt.envMs(1)).toBe("8.00 s");
  });
  it("signed curve", () => {
    expect(fmt.curve(0.5)).toBe("0");
    expect(fmt.curve(1)).toBe("+100");
  });
  it("lfo rate free or synced", () => {
    expect(fmt.lfoRate(0, false)).toBe("0.05 Hz");
    expect(fmt.lfoRate(1, false)).toBe("20.00 Hz");
    expect(fmt.lfoRate(0, true)).toBe("1/16");
    expect(fmt.lfoRate(1, true)).toBe("2");
  });
  it("phase degrees", () => expect(fmt.deg(0.5)).toBe("180°"));
  it("fade ms", () => expect(fmt.fadeMs(0.5)).toBe("2000 ms"));
  it("chorus rate", () => expect(fmt.chorusHz(0)).toBe("0.10 Hz"));
  it("arp rate and octaves", () => {
    expect(fmt.arpRate(0)).toBe("1/32");
    expect(fmt.arpRate(1)).toBe("1/4");
    expect(fmt.octaves(0)).toBe("1");
    expect(fmt.octaves(1)).toBe("4");
  });
  it("signed integers for steppers", () => {
    expect(fmt.signedInt(2)).toBe("+2");
    expect(fmt.signedInt(0)).toBe("0");
    expect(fmt.signedInt(-3)).toBe("-3");
  });
});
