// GENERATED da Source/parameters/parameters.json — non modificare a mano.
// Rigenera con: pnpm gen:params

export type ParamId = "oscOn" | "wtIndex" | "unison" | "oct" | "semi" | "wtpos" | "warp" | "detune" | "fine" | "level" | "filtOn" | "ftype" | "slope" | "cutoff" | "res" | "drive" | "keytrk" | "voiceMode" | "pan" | "glide" | "volume" | "bypass" | "att" | "dec" | "sus" | "rel" | "envVel" | "envCurve" | "lshape" | "lsync" | "lretrig" | "lrate" | "lphase" | "lfade" | "fx1On" | "chRate" | "chDepth" | "chMix" | "fx2On" | "rvSize" | "rvDamp" | "rvMix" | "arpOn" | "arpMode" | "arpRate" | "arpGate" | "arpOct" | "arpSwing";
export type ParamKind = "float" | "int" | "bool" | "choice";
export type MapType = "linear" | "log" | "db" | "ms-squared";
export type LabelKind = "hz" | "time" | "pan" | "signed" | "arp-rate";
export interface ParamSpec {
  id: ParamId; name: string; group: string; kind: ParamKind;
  map?: { type: MapType; min: number; max: number; offset?: number };
  default: number | boolean; unit?: string; decimals?: number; labelKind?: LabelKind; bipolar?: boolean;
  options?: { value: string; label: string }[];
}
export const GROUPS: Record<string, string> = {"osc":"Osc","filter":"Filter","master":"Master","env":"Env","lfo":"LFO","fx1":"Chorus","fx2":"Reverb","arp":"Arp"};
export const PARAM_IDS = ["oscOn","wtIndex","unison","oct","semi","wtpos","warp","detune","fine","level","filtOn","ftype","slope","cutoff","res","drive","keytrk","voiceMode","pan","glide","volume","bypass","att","dec","sus","rel","envVel","envCurve","lshape","lsync","lretrig","lrate","lphase","lfade","fx1On","chRate","chDepth","chMix","fx2On","rvSize","rvDamp","rvMix","arpOn","arpMode","arpRate","arpGate","arpOct","arpSwing"] as const satisfies readonly ParamId[];
export const PARAM_SPECS: Record<ParamId, ParamSpec> = {
  "oscOn": {"id":"oscOn","name":"Oscillator on","group":"osc","kind":"bool","default":true},
  "wtIndex": {"id":"wtIndex","name":"Wavetable","group":"osc","kind":"choice","default":0,"options":[{"value":"basic","label":"Basic Shapes"},{"value":"saws","label":"Analog Saws"},{"value":"grit","label":"Digital Grit"},{"value":"vocal","label":"Vocal Formant"},{"value":"bells","label":"Glass Bells"},{"value":"pwm","label":"PWM Sweep"}]},
  "unison": {"id":"unison","name":"Unison","group":"osc","kind":"choice","default":0,"options":[{"value":"1","label":"1"},{"value":"2","label":"2"},{"value":"4","label":"4"},{"value":"8","label":"8"}]},
  "oct": {"id":"oct","name":"Octave","group":"osc","kind":"int","map":{"type":"linear","min":-3,"max":3},"default":0,"unit":"OCT"},
  "semi": {"id":"semi","name":"Semitones","group":"osc","kind":"int","map":{"type":"linear","min":-12,"max":12},"default":0,"unit":"SEMI"},
  "wtpos": {"id":"wtpos","name":"Position","group":"osc","kind":"float","map":{"type":"linear","min":1,"max":64},"default":0.32,"decimals":1},
  "warp": {"id":"warp","name":"Warp","group":"osc","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.1,"unit":"%","decimals":0},
  "detune": {"id":"detune","name":"Detune","group":"osc","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.18,"unit":"ct","decimals":0},
  "fine": {"id":"fine","name":"Fine","group":"osc","kind":"float","map":{"type":"linear","min":-100,"max":100},"default":0.5,"unit":"ct","decimals":0,"bipolar":true},
  "level": {"id":"level","name":"Level","group":"osc","kind":"float","map":{"type":"db","min":0,"max":1},"default":0.85,"unit":"dB","decimals":1},
  "filtOn": {"id":"filtOn","name":"Filter on","group":"filter","kind":"bool","default":true},
  "ftype": {"id":"ftype","name":"Filter type","group":"filter","kind":"choice","default":0,"options":[{"value":"LP","label":"LP"},{"value":"HP","label":"HP"},{"value":"BP","label":"BP"}]},
  "slope": {"id":"slope","name":"Slope","group":"filter","kind":"choice","default":1,"options":[{"value":"12","label":"12"},{"value":"24","label":"24"}]},
  "cutoff": {"id":"cutoff","name":"Cutoff","group":"filter","kind":"float","map":{"type":"log","min":20,"max":20000},"default":0.62,"unit":"Hz","decimals":0,"labelKind":"hz"},
  "res": {"id":"res","name":"Resonance","group":"filter","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.3,"unit":"%","decimals":0},
  "drive": {"id":"drive","name":"Drive","group":"filter","kind":"float","map":{"type":"linear","min":0,"max":24},"default":0,"unit":"dB","decimals":1},
  "keytrk": {"id":"keytrk","name":"Key trk","group":"filter","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.5,"unit":"%","decimals":0},
  "voiceMode": {"id":"voiceMode","name":"Voice mode","group":"master","kind":"choice","default":0,"options":[{"value":"Poly","label":"Poly"},{"value":"Mono","label":"Mono"},{"value":"Legato","label":"Legato"}]},
  "pan": {"id":"pan","name":"Pan","group":"master","kind":"float","map":{"type":"linear","min":-50,"max":50},"default":0.5,"decimals":0,"labelKind":"pan","bipolar":true},
  "glide": {"id":"glide","name":"Glide","group":"master","kind":"float","map":{"type":"ms-squared","min":0,"max":2000},"default":0,"unit":"ms","decimals":0},
  "volume": {"id":"volume","name":"Volume","group":"master","kind":"float","map":{"type":"db","min":0,"max":1},"default":0.8,"unit":"dB","decimals":1},
  "bypass": {"id":"bypass","name":"Bypass","group":"master","kind":"bool","default":false},
  "att": {"id":"att","name":"Attack","group":"env","kind":"float","map":{"type":"ms-squared","min":1,"max":8001},"default":0.12,"labelKind":"time"},
  "dec": {"id":"dec","name":"Decay","group":"env","kind":"float","map":{"type":"ms-squared","min":1,"max":8001},"default":0.4,"labelKind":"time"},
  "sus": {"id":"sus","name":"Sustain","group":"env","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.7,"unit":"%","decimals":0},
  "rel": {"id":"rel","name":"Release","group":"env","kind":"float","map":{"type":"ms-squared","min":1,"max":8001},"default":0.35,"labelKind":"time"},
  "envVel": {"id":"envVel","name":"Vel → amp","group":"env","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.6,"unit":"%","decimals":0},
  "envCurve": {"id":"envCurve","name":"Curve","group":"env","kind":"float","map":{"type":"linear","min":-100,"max":100},"default":0.5,"decimals":0,"labelKind":"signed","bipolar":true},
  "lshape": {"id":"lshape","name":"LFO shape","group":"lfo","kind":"choice","default":0,"options":[{"value":"Sine","label":"Sine"},{"value":"Tri","label":"Tri"},{"value":"Saw","label":"Saw"},{"value":"Square","label":"Square"},{"value":"S&H","label":"S&H"}]},
  "lsync": {"id":"lsync","name":"LFO sync","group":"lfo","kind":"bool","default":false},
  "lretrig": {"id":"lretrig","name":"LFO retrig","group":"lfo","kind":"bool","default":true},
  "lrate": {"id":"lrate","name":"Rate","group":"lfo","kind":"float","map":{"type":"log","min":0.05,"max":20},"default":0.45,"unit":"Hz","decimals":2},
  "lphase": {"id":"lphase","name":"Phase","group":"lfo","kind":"float","map":{"type":"linear","min":0,"max":360},"default":0,"unit":"°","decimals":0},
  "lfade": {"id":"lfade","name":"Fade in","group":"lfo","kind":"float","map":{"type":"linear","min":0,"max":4000},"default":0,"unit":"ms","decimals":0},
  "fx1On": {"id":"fx1On","name":"Chorus on","group":"fx1","kind":"bool","default":true},
  "chRate": {"id":"chRate","name":"Rate","group":"fx1","kind":"float","map":{"type":"linear","min":0.1,"max":5.1},"default":0.3,"unit":"Hz","decimals":2},
  "chDepth": {"id":"chDepth","name":"Depth","group":"fx1","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.4,"unit":"%","decimals":0},
  "chMix": {"id":"chMix","name":"Mix","group":"fx1","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.3,"unit":"%","decimals":0},
  "fx2On": {"id":"fx2On","name":"Reverb on","group":"fx2","kind":"bool","default":true},
  "rvSize": {"id":"rvSize","name":"Size","group":"fx2","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.6,"unit":"%","decimals":0},
  "rvDamp": {"id":"rvDamp","name":"Damp","group":"fx2","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.4,"unit":"%","decimals":0},
  "rvMix": {"id":"rvMix","name":"Mix","group":"fx2","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.25,"unit":"%","decimals":0},
  "arpOn": {"id":"arpOn","name":"Arp on","group":"arp","kind":"bool","default":false},
  "arpMode": {"id":"arpMode","name":"Arp mode","group":"arp","kind":"choice","default":0,"options":[{"value":"Up","label":"Up"},{"value":"Down","label":"Down"},{"value":"UpDn","label":"UpDn"},{"value":"Rand","label":"Rand"}]},
  "arpRate": {"id":"arpRate","name":"Rate","group":"arp","kind":"float","map":{"type":"linear","min":0,"max":1},"default":0.4,"labelKind":"arp-rate"},
  "arpGate": {"id":"arpGate","name":"Gate","group":"arp","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0.6,"unit":"%","decimals":0},
  "arpOct": {"id":"arpOct","name":"Octaves","group":"arp","kind":"float","map":{"type":"linear","min":1,"max":4},"default":0.33,"decimals":0},
  "arpSwing": {"id":"arpSwing","name":"Swing","group":"arp","kind":"float","map":{"type":"linear","min":0,"max":100},"default":0,"unit":"%","decimals":0},
};
