/** Stato del synth: parametri normalizzati 0..1 più selettori discreti. */

export type FilterType = "LP" | "HP" | "BP";
export type VoiceMode = "Poly" | "Mono" | "Legato";
export type LfoShape = "Sine" | "Tri" | "Saw" | "Square" | "S&H";
export type ArpMode = "Up" | "Down" | "UpDn" | "Rand";

export type SynthParams = {
  oscOn: boolean;
  wtIndex: number;
  unison: number;
  oct: number;
  semi: number;
  wtpos: number;
  warp: number;
  detune: number;
  fine: number;
  level: number;
  filtOn: boolean;
  ftype: FilterType;
  slope: 12 | 24;
  cutoff: number;
  res: number;
  drive: number;
  keytrk: number;
  voiceMode: VoiceMode;
  pan: number;
  glide: number;
  volume: number;
  att: number;
  dec: number;
  sus: number;
  rel: number;
  envVel: number;
  envCurve: number;
  lshape: LfoShape;
  lsync: boolean;
  lretrig: boolean;
  lrate: number;
  lphase: number;
  lfade: number;
  fx1On: boolean;
  chRate: number;
  chDepth: number;
  chMix: number;
  fx2On: boolean;
  rvSize: number;
  rvDamp: number;
  rvMix: number;
  arpOn: boolean;
  arpMode: ArpMode;
  arpSteps: number[];
  arpRate: number;
  arpGate: number;
  arpOct: number;
  arpSwing: number;
};

/** Id dei parametri continui (bersagli di modulazione). */
export type KnobId = {
  [K in keyof SynthParams]: SynthParams[K] extends number ? K : never;
}[keyof SynthParams] &
  Exclude<keyof SynthParams, "wtIndex" | "unison" | "oct" | "semi" | "slope">;

export const DEFAULTS: SynthParams = {
  oscOn: true, wtIndex: 0, unison: 0, oct: 0, semi: 0, wtpos: 0.32, warp: 0.1, detune: 0.18, fine: 0.5, level: 0.85,
  filtOn: true, ftype: "LP", slope: 24, cutoff: 0.62, res: 0.3, drive: 0.15, keytrk: 0.5,
  voiceMode: "Poly", pan: 0.5, glide: 0, volume: 0.8,
  att: 0.12, dec: 0.4, sus: 0.7, rel: 0.35, envVel: 0.6, envCurve: 0.5,
  lshape: "Sine", lsync: false, lretrig: true, lrate: 0.45, lphase: 0, lfade: 0,
  fx1On: true, chRate: 0.3, chDepth: 0.4, chMix: 0.3, fx2On: true, rvSize: 0.6, rvDamp: 0.4, rvMix: 0.25,
  arpOn: false, arpMode: "Up", arpSteps: [0.8, 0, 0.6, 0.9, 0, 0.7, 0, 0.5, 0.8, 0, 0.6, 0, 0.9, 0.4, 0, 0.7],
  arpRate: 0.4, arpGate: 0.6, arpOct: 0.33, arpSwing: 0,
};

/** Nome leggibile "Sezione · Parametro" per la mod matrix. */
export const LABELS: Record<KnobId, string> = {
  wtpos: "Osc · Position", warp: "Osc · Warp", detune: "Osc · Detune", fine: "Osc · Fine", level: "Osc · Level",
  cutoff: "Filter · Cutoff", res: "Filter · Resonance", drive: "Filter · Drive", keytrk: "Filter · Key trk",
  pan: "Master · Pan", glide: "Master · Glide", volume: "Master · Volume",
  att: "Env · Attack", dec: "Env · Decay", sus: "Env · Sustain", rel: "Env · Release", envVel: "Env · Vel", envCurve: "Env · Curve",
  lrate: "LFO · Rate", lphase: "LFO · Phase", lfade: "LFO · Fade",
  chRate: "Chorus · Rate", chDepth: "Chorus · Depth", chMix: "Chorus · Mix",
  rvSize: "Reverb · Size", rvDamp: "Reverb · Damp", rvMix: "Reverb · Mix",
  arpRate: "Arp · Rate", arpGate: "Arp · Gate", arpOct: "Arp · Octaves", arpSwing: "Arp · Swing",
};
