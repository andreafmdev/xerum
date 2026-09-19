# Architecture — SerumStyleSynth

## Principle

**JUCE lives at the boundary.** The real-time synth core (`engine/`, `dsp/`) stays free of UI and host glue so wavetable / mod-matrix work can grow without rewriting the plugin adapter.

## Layers

| Layer | Path | Responsibility |
|-------|------|----------------|
| Plugin adapter | `Source/plugin/` | `AudioProcessor`, APVTS, WebView editor |
| Parameters | `Source/parameters/` | Single source of parameter IDs + layout |
| Engine | `Source/engine/` | MIDI dispatch, voice pool, block render, mod matrix snapshot |
| DSP | `Source/dsp/` | Wavetable oscillator + mipmaps, TPT filter, ADSR envelope, LFO, `WavetableStore` |
| Web UI | `WebUI/` | App shell React + Vite (dev server → WebView): finestra plugin `src/synth/` (900×600, stato, mod matrix, preset) costruita con `@xerum/ui` |
| UI library | `WebUI/packages/ui/` | `@xerum/ui`: componenti synth (Tailwind v4, shadcn base-nova), Storybook, test |
| Bridge | `Source/bridge/` | Web relays, state channel, meters, embedded assets (message thread only) |

```
DAW MIDI ──► PluginProcessor ──► SynthEngine ──► VoiceManager ──► SynthVoice
                    │                                      │
                 APVTS                              dsp:: oscillator/filter/envelope
                    │
             PluginEditor ─┬─ WebView ◄── React (localhost:5173 / fallback HTML)
                           └─ MidiKeyboardComponent (native strip) ──► MidiKeyboardState ──► processBlock MIDI
```

## Real-time rules

On the audio thread (`processBlock` → `SynthEngine::process`):

- No heap allocations
- No mutexes / locks
- No file or network I/O
- No logging

Cross-thread assets (wavetables, presets) must use lock-free handoff — double buffer, `AbstractFifo`, or atomic pointer swap (`WavetableStore`, phase 2).

## Plugin window (WebUI/src/synth)

The front panel is `SynthWindow` (`WebUI/src/synth/ui/`): a fixed 900×600 chassis scaled to fit the WebView, laid out as header → wavetable display → Oscillator / Filter / Master plates → tab strip (Envelope, LFO, Mod matrix, Effects, Arpeggiator) → footer meters, plus a preset overlay. It is the implementation of the Claude Design template `templates/synth-window/SynthWindow.dc.html` of the Xerum Synth UI project.

- Pure logic lives beside it and is unit-tested: `params.ts` (defaults, labels), `format.ts` (value → text), `mod.ts` (LFO shapes, live modulated values, assignments), `curves.ts` (wave morph, filter/envelope/LFO paths, spectrum), `presets.ts`.
- `useSynth` holds the window state (params, mod matrix, preset, tab, bypass, dirty); `useClock` drives LFO/meter/arp animation. Values are normalised `0..1`.
- **Bridge** (`Source/bridge/`, `WebUI/src/juce/`): every control is a JUCE web relay on an APVTS parameter (`WebRelays`); mod matrix and arp steps live in the APVTS `ValueTree` (`MODS`/`ARP`) behind `StateChannel` (`getState`/`setMods`/`setArpSteps` + `stateChanged`); `MeterChannel` streams `MeterFrame` atomics at 30 Hz as `meters`. The audio thread only loads/stores atomics. `parameters.json` is the single source of truth; `scripts/gen-params.mjs` regenerates `ParameterTable.h` and `params.generated.ts`.
- Without a JUCE host (browser, tests) the UI runs on `FakeBackend` (`?demo` clock on in the browser).
- **Scaling uses `transform: scale()`, and `zoom` was tried and rejected.** `zoom` looks like the better tool — it redoes layout instead of stretching a bitmap — but WebKit, the engine inside the WKWebView the plugin actually runs in, does not implement it the way Chromium does. Measured with Playwright/WebKit at a 1309×873 viewport with `zoom: 1.4544` on the chassis: the chassis' own `getBoundingClientRect()` stayed 900×600 (unscaled) while its descendants were *divided* by the factor rather than multiplied (the wave display, 130 px in layout, measured 89.4 = 130 / 1.4544). In the plugin that showed up as a ~270 px empty band between the panel and the keyboard. Don't reach for `zoom` again without measuring in WebKit first — Chrome will not show the problem.
- **The `<canvas>` in `WaveDisplay` needs the scale handed to it.** `transform` does not touch a canvas backing store, so the wave display stayed at 1× resolution while everything around it grew — it is the largest element in the window, which is most of why the UI read as soft at high scale. The backing store is now multiplied by the applied scale, read back from `getBoundingClientRect().width / clientWidth`. The scale also arrives as a prop, and that part is not redundant: it is the *dependency* that makes the draw effect re-run on resize. Without it the effect ran once at mount, while `sc` was still 1, and the canvas kept that resolution forever (verified: the backing store stayed pinned at 1752 px at every window size).
- Three materials via `variant` (`deep` default, `soft`, `glow`) — `synth.css` overrides the `@xerum/ui` hardware tokens on the chassis. In the browser: `?variant=glow&tab=lfo`.
- Every control is an `@xerum/ui` primitive (Knob with modulation rings and drop target, Segmented, Stepper, Meter, Tabs `bar`, Toggle, Panel, Button). Displays specific to the window (filter response, envelope, LFO scope, wavetable stack + spectrum) are app-level canvases/SVGs.
- **Known divergence:** `WaveDisplay` (`WebUI/src/synth/ui/WaveDisplay.tsx`) draws a procedural curve from `WebUI/src/synth/curves.ts`, not the real `.xwt` table data — deliberate, not an oversight.
- **Known gap:** the twelve factory presets (`Source/parameters/presets.json`) have been recalibrated against the current gain staging — each one now states `level`, `drive` and `volume` explicitly, the three that decide whether it clips — but they still have not been auditioned by ear, and the preset browser (`PresetOverlay`) has not been clicked through end to end in the Standalone.

## On-screen keyboard

The editor is a `WebBrowserComponent` with a native keyboard strip underneath, Serum/Vital style. It drives a `MidiKeyboardState` owned by the processor, merged into the host MIDI buffer at the top of `processBlock` (`processNextMidiBuffer`). QWERTY mapping (A W S E D F T G Y H U J K …) plays from middle C; click height sets velocity. The keyboard is plugin chrome, not part of `@xerum/ui`, so it does not go through design-sync.

**Skin** — `ui::XerumKeyboard` (`Source/ui/`) subclasses `juce::MidiKeyboardComponent` and overrides `drawWhiteNote` / `drawBlackNote` / `paintOverChildren`: gradient keys, accent `#6ee7c5` on press, octave labels, bottom corners rounded like `.sx-chassis`. Its palette mirrors `WebUI/packages/ui/src/theme.css` and lives in `XerumKeyboard.cpp`; the editor no longer sets the base `ColourId`s, except the three the `final` `drawKeyboardBackground` reads (set in the keyboard's own constructor).

**Layout contract** — the window's *width* is the only free variable. The editor derives everything from it (`PluginEditor.cpp`):

| | |
|---|---|
| scale | `width / 900`, clamped to `0.72 … 1.5` (same ceiling as the web fit) |
| WebView | full width × `ceil(600 × scale)` |
| keyboard | full width × `round(78 × scale)`, right below |
| window height | the sum of the two, enforced by `ChassisConstrainer` |

The editor loads the UI with `?gutter=0`, which tells `SynthWindow` to fit the chassis with no margin (default `16`) and marks it `data-attached` so its bottom corners go square. Chassis and keyboard then share the same width and touch, with no dead band between them. Changing either side of this contract (the 900×600 chassis, the `gutter` default, the fit formula) desyncs the two: keep `SynthWindow.tsx` and `PluginEditor.cpp` in step.

## Phase 1 behaviour

- Instrument plugin: AU + VST3 + Standalone
- MIDI note on/off allocates voices (16-voice pool, round-robin steal)
- The engine is audible: a wavetable oscillator with band-limited mipmaps (`dsp::WavetableOscillator` / `dsp::MipTable`) feeds an exponential ADSR (`dsp::ADSREnvelope`) into a TPT state-variable filter (`dsp::StateVariableFilter`, 1 or 2 stages). 30 parameters are wired end to end: `oscOn`, `wtIndex`, `wtpos`, `oct`, `semi`, `fine`, `level`, `filtOn`, `ftype`, `slope`, `cutoff`, `res`, `drive`, `keytrk`, `att`, `dec`, `sus`, `rel`, `envVel`, `volume`, `pan`, `bypass`, plus the six LFO parameters `lshape`, `lrate`, `lsync`, `lphase`, `lfade`, `lretrig`, and `unison` / `detune`
- The mod matrix is live: four sources (`lfo`, `env`, `vel`, `mw`) onto seven targets (`cutoff`, `res`, `wtpos`, `level`, `pan`, `fine`, `drive`), at control rate — see **Modulation** below
- Still inert (accepted by the APVTS, no effect on sound yet): `envCurve`, `glide`, `voiceMode`, `warp`, `fx1On`/`ch*` (chorus), `fx2On`/`rv*` (reverb), every `arp*`

## Parameter mapping

`Source/parameters/ParamCollect.h` holds `collectEngineParams`, a header-only template that turns raw 0..1 parameter values into an `engine::EngineParams`. It takes an accessor keyed by `params::ParamSlot` (an enum, not a parameter id string) instead of reading the APVTS directly, for two reasons: the conversion arithmetic can be exercised in `XerumTests` with a fake accessor, no `juce_audio_processors` link needed; and at runtime `PluginProcessor` resolves one array of raw parameter pointers once in its constructor (`paramSlots_`, indexed by `ParamSlot`), so the per-block cost is a single array read per parameter — no name lookup, no string comparison.

This split exists because of a bug: an early version cast a denormalised `oct`/`semi` value with `(int)` instead of `juce::roundToInt`, truncating toward zero and landing on the wrong note for several settings (`denormalise()` can return a hair under the true integer, e.g. `7.999998`). The regression test written against that bug reimplemented the `Map::Linear` formula by hand, because at the time the real conversion lived inside `PluginProcessor.cpp`, which pulls in `juce_audio_processors` — a library `XerumTests` doesn't link — so the test verified its own copy of the arithmetic, not the code that ships. `ParamCollect.h` was carved out precisely so a test could call the real `collectEngineParams` (`Tests/EngineTests.cpp`, `ParamCollectTests`): if the truncating cast came back, that test would catch it; the earlier, hand-rolled one would not. Don't collapse this back into `PluginProcessor` for the sake of one fewer header.

## Voice engine

**Headroom.** `Source/engine/SynthVoice.cpp` applies a fixed per-voice gain, `kVoiceHeadroomGain = 0.4f` (-8 dB), to every sample before panning — not a `1 / numVoices` divisor, which would pump the overall level up and down every time a note starts or ends. Measured at the *plugin* output (engine + default `volume` of 0.8 linear, filter at Butterworth Q): a single note at `level = 1.0` sits at -13.8 dBFS, a four-note chord at -6.0 dBFS. Both numbers come from `Tests/EngineTests.cpp`, "gain staging: una nota e un accordo normale stanno sotto il soft clipper", which also pins them below the output soft clipper's 0.8 threshold and above an audibility floor.

This constant used to be `0.1f` (-20 dB), chosen when two other stages could each add uncontrolled gain: the filter multiplied its resonance peak stage by stage (see below), and `saturate()` had a small-signal slope of 1.5 so it added +3.5 dB even at drive zero. With those two fixed, -20 dB left a single note at -26 dBFS — an unusably quiet instrument. Anything that still exceeds full scale is caught by the output soft clipper rather than by keeping every voice far from it.

**Unison** (`SynthVoice`, `engine::unisonSpread`). Each voice holds a fixed array of eight `WavetableOscillator`s and runs the first N, where N is 1, 2, 4 or 8 from the `unison` choice. The copies are detuned symmetrically around the note — with N = 4 and `detune` at 18 cents: −18, −6, +6, +18 — and `unisonSpread(i, N)` returns that position on −1..+1. The same function drives both the detuning and the stereo placement, which is what keeps the outermost copy outermost in both senses.

Three things that are easy to get wrong and are pinned by tests:

*Starting phases are spread over `i / N` of the cycle.* Without that the N copies are identical at note-on and sum in phase: √N times louder with no beating until the detuning pulls them apart. Measured at `detune = 0`, unison 8: the fundamental is 0.079× the single-copy level with spread phases, and would be 2.83× (√8) without. The spreading only happens when N > 1 — at N = 1 the phase is left alone, because `start()` does not reset the oscillator and zeroing it would change the signal of a voice taking over a pool slot.

*The gain compensation is `1 / sqrt(N)`.* Summing N uncorrelated oscillators grows as the root, not linearly; without it, going from unison 1 to 8 would add about 9 dB. Measured: −20.34 dBFS RMS at unison 1 against −21.31 at unison 8.

*Unison needs a second filter.* The SVF is single-in single-out. With the copies spread across the stereo field the two channels carry different mixtures, and one filter would fold them back into one — the spread would not survive the filter, which is the point of having it. `filterRight_` exists for that and never sees a sample at unison 1, so there is no stale state to restart from when unison goes up.

The stereo width is fixed at ±0.6 (`kUnisonSpreadWidth`) because there is no `width` parameter and adding one would touch the UI, the presets and the generated files. At full width the outermost copies land entirely in one channel, and — more decisively — the spread adds to the voice's own `pan` before clamping to ±1, so at full width the cluster would already touch both edges with `pan` centred and **the pan knob would stop doing anything**. At 0.6 there are 0.4 of travel per side before the clamp bites. Constant-power panning makes the compensation exact regardless: over a symmetric set of positions the cos² terms sum to N·cos²(pan). Measured L/R correlation: 1.000 at unison 1, 0.847 at unison 8.

**Cost.** Release build, 128-sample blocks at 48 kHz, 16 held notes, 24 dB filter at Q 2 — budget is 2.67 ms per block:

| | µs/block | % of budget |
|---|---|---|
| unison 1 × 16 voices | 19.2 | 0.72 % |
| unison 8 × 16 voices | 77.1 | 2.89 % |

Eight times the oscillators cost four times the block, not eight: the envelope, level, master gain and soft clipper do not scale, and the filter only doubles. There is no polyphony cap.

**Filter resonance** (`dsp::StateVariableFilter`). Two deliberate choices keep resonance from becoming a gain stage. First, in the 24 dB configuration the resonance sits on the *last* stage only and the first stays Butterworth: applying it to both made the peak grow as Q², over +40 dB at Q 12. Second, the filter input is attenuated by `(Qbutter / Q)^(1/32)` (`kResonanceCompensation`) — a shaving off the peak that leaves the passband where it was.

That exponent used to be `1/2`, and it was wrong. A lowpass passband does not depend on Q, so *any* input attenuation simply lowers it: the cost is exactly `20·p·log10(Q/Qbutter)`, which at `p = 1/2` meant **−14.7 dB of passband at Q 24**. Turning resonance up made the whole instrument quieter and thinner instead of making the filter ring — measured on the engine at −9.99 dB of output RMS at Q 24, against +4.37 dB after the change. At `1/32` the worst-case passband deviation is 0.64 dB across 44.1/48/96 kHz and cutoffs from 100 Hz to 12 kHz, while the peak still reaches about +29.7 dB over Butterworth at the top of the range. That peak is the instrument's loudest source of transient level: the output soft clipper below is what keeps it in bounds. Since `res` became a modulation target this is no longer a corner case reachable only by turning a knob — a route can drive it there every LFO cycle, and the full-matrix stress test (see **Modulation**) measures about +8 dBFS arriving at the clipper because of it.

`res` maps to Q exponentially, 0.707 to 12 (`params::resonanceQFromRaw`), not linearly to 20: with the old linear map, `res` at its 30% default already meant Q 6.5.

**Output soft clipper** (`SynthEngine::process`). After the master gain, both channels pass through a soft clipper that is bit-transparent below 0.8 and asymptotes to 1.0 above it. It exists because the per-voice gain is a constant: a dense chord, or a high master volume, can still exceed full scale, and without it the host would receive hard-truncated samples. Its derivative is 1 at the threshold, so there is no corner where it engages, and it never fires during ordinary playing (see the gain-staging test above). `Tests/EngineTests.cpp`, "il soft clipper d'uscita non tocca il segnale sotto soglia", pins the transparency: doubling the master gain must double the peak exactly.

With the mod matrix in the picture it is no longer only a safety net for dense chords. A route on `res` or `drive` reaches the same places a hand on the knob would, but repeatedly and without anyone watching the meter, so the clipper is now a stage the instrument leans on in normal use of the matrix — it is the single reason a full matrix stays inside full scale at all. If the per-voice headroom or the resonance compensation is ever retuned, the full-matrix stress test is the measurement that says whether this stage still has anything left to give.

**A silent sustain ends the note** (`dsp::ADSREnvelope::enterSustain`). Entering the sustain stage is funnelled through one method, which drops straight to `Stage::idle` when the effective level `peak × sustain` falls below `kSilence` (−80 dB). Without it `Stage::sustain` had no path to `idle` — only release did — so a patch with `sustain = 0` left every voice occupied rendering silence until `VoiceManager` stole it with `kill()`, which is a step to zero between two adjacent samples: the click heard on percussive patches. The comparison is on the *effective* level, so a small but legitimate sustain (0.01, −40 dB) keeps holding until note-off, while a low velocity that drives the product below the threshold ends the note. It is re-evaluated every sample, so turning the `sus` knob to zero on a held note frees the voice instead of leaving it hanging.

**Retriggering a sounding note** (`VoiceManager::noteOn` → `SynthVoice::retrigger`). A second note-on for a pitch that is already sounding restarts the envelope on the same voice and touches nothing else: the oscillator phase and the two SVF integrators keep running. `noteOn` used to `kill()` the existing voice and start it from scratch, which zeroed all three at once — a step from full amplitude to zero between two adjacent samples, measured at 0.24 at full scale. That was the click heard on every repeated note, including ordinary legato. Pinned by `Tests/EngineTests.cpp`, "ribattere una nota che suona gia' non produce un gradino".

**Voice stealing still clicks.** `VoiceManager::stealVoice` round-robins and calls `kill()`, so the 17th simultaneous note cuts whatever that slot was playing dead: a 0.13 step at full scale. Unlike the retrigger case there is no free lunch — the slot is needed immediately — so fixing it means either stealing the quietest voice or giving the stolen voice a short fade-out before reuse. Not done yet.

**Envelope timing.** `dsp::ADSREnvelope` (`Source/dsp/ADSREnvelope.cpp`) derives each stage's per-sample coefficient from an exact time constant, not a rounded approximation: attack reaches 0.99 of the peak in the nominal `att` seconds, decay covers 99% of its distance in `dec`, release falls under -80 dBFS — and the voice is freed — in `rel`. The constants are `ln(100)` for attack and decay and `ln(10000)` for release. Decay's 1% tolerance is checked against `decayDistance_`, the distance from peak to the sustain target *at the start of the decay stage*, not against the peak itself: an earlier version compared against the peak, and with sustain near 1.0 the peak-relative tolerance was wider than the actual (tiny) distance left to travel, so the stage's exit condition was already satisfied on the first sample and the decay curve never ran.

## Modulation

**From the `MODS` node to the snapshot.** An assignment is made in the web UI by dragging a source onto a knob. It travels `setMods` → `bridge::StateChannel` → `state::setMods`, which rewrites the `MODS` child of the APVTS `ValueTree` as one `MOD` node per assignment, each carrying `src`, `target` and `depth`. Nothing on the audio thread ever reads that tree. `PluginProcessor` listens on the APVTS root, filters for `MOD`/`MODS` nodes — every knob movement in the plugin also passes through that listener, and rebuilding on each of them would be pure waste — and calls `engine::buildModSnapshot`, which translates the tree into a fixed-size `engine::ModSnapshot`: 32 routes in a plain array, no pointers, no allocation.

That translation happens on the message thread on purpose. It is where the string `"cutoff"` becomes index 0 in `kModTargets`, where a source name the engine doesn't know is dropped, and where `depth` is clamped to ±1 — a hand-edited preset or a newer UI could send anything, and the audio thread should not have to distrust its own input. What reaches `SynthVoice` is an array index into `modBase` and a float: no string comparison, no lookup, no validation, nothing that could fail. **The audio thread never sees a route it cannot apply.**

**Four ring slots, not a double buffer.** `SynthEngine::setMods` copies the snapshot into one of four slots (`modRing_`) and publishes only the pointer; `process()` loads it once per block. Two slots would be the obvious choice, and it is the wrong one here: mod assignments change far more often than a wavetable does — dragging a depth slider produces a stream of publishes — and with two slots the writer can come back around and overwrite the very slot the reader is holding. With four, the message thread would have to publish four times inside a single audio block to catch up with the reader, which does not happen in practice; and even then the damage would be one block of wrong modulation, never a read of freed memory, because the slots live as long as the engine. The write index is a `fetch_add`, not a plain increment, so two concurrent publishers pick different slots instead of interleaving into one.

**Rebuilds are coalesced, but `replaceState` is not.** `state::setMods` rewrites the whole list as `removeAllChildren` + N appends, so a single depth drag produces 2N notifications — and one of the intermediate states is *the empty list*, right after `removeAllChildren`. Publishing a snapshot per notification would publish that state too: if an audio block lands in that window the modulation disappears for 128 samples, which on `level` or `pan` is a click. So the listener only calls `triggerAsyncUpdate()` and `handleAsyncUpdate` rebuilds once per message-loop turn, seeing only the final state. (`bridge::StateChannel` coalesces its `emitState` for the same reason.)

`setStateInformation` is the deliberate exception. Loading a project or a preset replaces the tree wholesale while the audio thread is still rendering with the *previous* patch's assignments; waiting for the next message-loop turn would mean playing the new sound through the old modulation. So that path cancels any pending update and rebuilds synchronously, in a fixed order: recreate missing children (a project saved before `MODS` existed has none), re-attach the listener to the new tree — staying on the old one would mean never seeing a mod change again — then rebuild.

**Four sources, and their scope is the point.** `env` and `vel` are **per voice**: two held notes sit at different points of their own envelopes, and the velocity that opened each of them is its own. `mw` (CC 1) is **global** — one wheel, all voices. `lfo` is whichever of the two the patch asks for: with `lfoRetrig` on, each voice runs its own `dsp::Lfo`, restarted from `lphase` at note-on, which is what makes a vibrato repeatable; with it off, all voices read the engine's free-running LFO, advanced once per block in `process()` whether or not anything is sounding. That free LFO costs nothing when unused — voices only read its level if a route points somewhere — so it runs unconditionally rather than being switched on and off. `SynthEngine::getLfoLevel` follows the same fork when it feeds the editor's LFO dot: the first sounding voice's LFO when `lfoRetrig` is on (zero when nothing is playing), the free one when it is off (`MeterFrame::lfo`, overwritten rather than peak-held, because a bipolar LFO has no meaningful block peak). When `lsync` is on its rate comes from the host tempo via `dsp::syncedRateHz`; without a playhead, or with a host that exposes no BPM, it falls back to 120, the same fallback `syncedRateHz` applies internally, so there is no special case to keep alive. The four levels are not all the same shape: `lfo` is bipolar −1..1, the other three are 0..1.

**Seven targets; the rest are dropped on the message thread.** `engine::kModTargets` lists `cutoff`, `res`, `wtpos`, `level`, `pan`, `fine`, `drive` — the parameters the engine can currently move per block. The UI, by contrast, lets a source be dropped on *any* knob (`ModAssignment.target` is a `ParamId`), so an assignment to, say, `att` is written into the tree, survives a save and is then discarded by `buildModSnapshot`. **Known defect:** the UI keeps drawing that assignment as active, ring and all, while it does nothing. Exposing the modulatable set to the UI is on the list and is not done.

**The sum happens on the normalised value, and the clamp is part of the contract.** `EngineParams::modBase` carries the raw 0..1 value of each of the seven targets alongside the already-denormalised fields. `SynthVoice::modulated` computes `clamp01(base + Σ depth × sourceLevel)` and only then converts to Hz, to Q, to a gain. The order matters because it is exactly what `liveValue()` in `WebUI/src/synth/mod.ts` does, so the knob's modulation ring on screen and the sound describe the same number. The clamp is not a precaution against overflow: `liveValue` clamps at the same place, and without it the screen and the ears would disagree at the ends of the travel — the knob pinned at maximum while the filter kept climbing.

The conversion itself is shared, not re-derived: `params::cutoffHzFromRaw` and its six siblings were carved out of `collectEngineParams` precisely so the modulated path could call the same function the unmodulated one does. Two copies of the same formula diverge at the first edit, and the symptom would be a cutoff landing somewhere else depending on whether an LFO or a hand moved it. `Tests/EngineTests.cpp`, "le conversioni estratte coincidono con quelle di collectEngineParams", is the guard.

**The `kModTargetIds` trap.** There are two parallel tables: `kModTargets` (a `ParamSlot` per target) and `kModTargetIds` (the string the UI sends). It is tempting to delete the second and derive the id by indexing `params::kTable` with the slot. That would silently resolve every target to the wrong parameter: `ParamSlot`'s order is an internal convention declared in `ParamSlot.h`, `kTable`'s order follows `parameters.json`, and the two are unrelated — `kTable[(int) ParamSlot::cutoff]` is in fact `"level"`. The `static_assert` on the two tables' sizes is what catches somebody extending one and not the other; nothing catches indexing one with the other, which is why the tables are separate.

**Modulation is evaluated in `render()`, not in `setParams()`.** `setParams` runs once per block, when the processor deposits the parameters; at that moment the voice may not yet have the note it is about to play, so its velocity is unknown, and `env` will have moved by the time the first sample comes out. Both are per-voice sources that change *inside* the block — a note-on can arrive halfway through the buffer, and `process()` splits the render at every MIDI event for exactly that reason. So `setParams` only caches the struct and precomputes the target mask, and `applyModulation()` runs where the voice is about to sound: at the top of each render slice, and once in `start()` before the smoothed values are snapped, so a new note begins on its own modulated values rather than inheriting the previous occupant's.

**Only targets with a route take the modulated path.** `modMask_` holds one bit per target, rebuilt once per block from the snapshot. A target with no route keeps using the already-denormalised field from `EngineParams` — which is, line for line, the code that existed before the mod matrix. That makes non-regression a *structural* property rather than a numerical coincidence between two formulas that happen to agree: with an empty matrix the output is bit-identical to the pre-modulation engine, and `Tests/EngineTests.cpp` pins it sample by sample. The side benefit is that an empty matrix costs seven conversions less per block per voice, two of them `std::pow`.

**It is a control-rate system, and that is a stated limit.** Everything above is evaluated once per block, or once per slice between two MIDI events — 375 Hz at 48 kHz with 128-sample buffers, ample for an LFO that tops out at 20 Hz, and no coarser than `cutoff` and `wtpos` already were (both cost a `tan()` or a frame-index recomputation and have never updated per sample). The price is explicit: **FM and AM are not expressible in this matrix, and they are not a goal.** A per-sample modulation path would be a different design, not a tuning of this one.

**What it survives.** `Tests/EngineTests.cpp`, "mod matrix a pieno carico", fills the snapshot to its 32-route ceiling — all 28 source × target combinations plus four duplicates, depths alternating ±1 so each target is pulled by two positive and two negative routes at once — and then moves the parameters underneath: `cutoff`, `wtpos`, `level`, `pan` and `fine` on coprime triangle ramps, `res` and `drive` as hard steps between the ends of their travel every few blocks, all five LFO shapes, the master gain alternating per block, mod wheel jumps mid-buffer and notes retriggering halfway through a block. It runs that at 44.1/48/96 kHz, across the three filter types and both slopes, with the LFO synced and free, with 16 voices and with 24 (which forces stealing), republishing the snapshot on every block, plus five fixed-seed fuzz passes that randomise the matrix, every parameter and the block length. The contract checked is per sample, not per peak: every sample finite, every sample within full scale, and no voice left hanging after an all-notes-off.

It holds, and not by a wide margin: the worst case measures **−0.18 dBFS at the output, which is about +8 dBFS arriving at the soft clipper**. With `res` modulated to the top of its travel that is the filter's resonant peak — the +29.7 dB over Butterworth described above — landing on a sixteen-note chord. The soft clipper is the only thing between that and truncated samples, so the test asserts that it *does* engage (peak above the 0.8 threshold): a stress test that quietly stopped exercising the stage it exists to protect would still pass, and would be worth nothing.

## Wavetables

Six tables ship under `Resources/wavetables/*.xwt` — `basic`, `saws`, `grit`, `vocal`, `bells`, `pwm`, in the order of the `wtIndex` choice options in `Source/parameters/parameters.json` — sourced from **Adventure Kid Waveforms (AKWF)**, CC0-1.0, provenance tracked in `Resources/wavetables/CREDITS.md`. Regenerate them with:

    node scripts/fetch-wavetables.mjs

This re-downloads the AKWF families from GitHub and overwrites the `.xwt` files and `CREDITS.md`. It runs rarely — the `.xwt` files are committed, and this only refreshes them from source. `scripts/realign-wavetables.mjs` runs the same treatment on the committed files without touching the network.

**The pipeline is what makes Position a morph instead of a slideshow.** AKWF ships single-cycle waves that are individually peak-normalised and mutually unrelated in phase, so taking 64 of them and crossfading between neighbours produced comb filtering, not intermediate timbres. Measured on the shipped tables: adjacent-frame correlation as low as 0.299, and up to −4.6 dB of RMS lost halfway through a crossfade. The steps, in order, all in `scripts/wavetable-dsp.mjs`:

1. **Resample** each cycle to 2048 samples through its Fourier series — exact for a periodic signal, where linear interpolation would invent harmonics.
2. **Phase-align** each frame to its predecessor by the circular shift that maximises cross-correlation (computed by FFT). A circular shift of a single cycle is a pure phase offset: the amplitude spectrum is untouched, so this step costs nothing in timbre. Asserted in `scripts/wavetable-dsp.test.mjs`.
3. **Per-harmonic phase continuity** where step 2 is not enough: each harmonic's phase is unwrapped across the frames and rewritten as a least-squares line, amplitudes untouched. This one *does* alter individual frames, so it only runs where the mid-morph loss would otherwise exceed 1 dB.
4. **Anchor interpolation** where the family has 64 or more waves. The original `selectIndices` picked exactly `FRAMES` waves, which made the interpolation factor in `selectFrames` always zero — the crossfade never happened, and the 64 frames were 64 unrelated waves in a row. Now a smaller set of anchors is interpolated up to 64 frames, trading distinct waveforms for a continuous morph.
5. **Partial RMS equalisation**, gain `(rms_median / rms_k)^0.7` per frame. Not 1.0 on purpose: a narrowing pulse in a PWM sweep *should* get quieter, and flattening completely would erase differences in intensity that are musically correct. A constant gain per frame leaves the harmonic ratios alone, so it is timbrally neutral.
6. **One global scale** so the loudest sample in the whole table is 1.0 — never per frame, which is what produced the loudness pumping in the first place. This mirrors Vital's `Wavetable::postProcess`.

Measured end to end on the engine, RMS across a 32-step Position sweep: `pwm` went from 16.73 dB of swing to 4.19 dB, `grit` from 14.97 to 5.29; worst mid-morph loss across all six tables is now −0.81 dB and the lowest adjacent-frame correlation 0.923.

Steps 2–4 change which decisions depend on which: because equalisation brings neighbouring frames to similar levels, it *unmasks* cancellation that a loud frame next to a quiet one used to hide. The script therefore evaluates steps 3 and 4 against the finished table, not the intermediate one. Step 5 is not idempotent — running the script on its own output compresses twice.

**Format** (`dsp::parseXwt`, `Source/dsp/WavetableBlob.h/.cpp`): a 12-byte header — magic `"XWT1"`, little-endian `uint32` frame count, little-endian `uint32` frame size (must be a power of two) — followed by `frames × frameSize` `float32` samples, one frame after another. `parseXwt` rejects a wrong magic, a non-power-of-two frame size, a truncated buffer, sizes above its sanity caps (256 frames, 4096 samples/frame), or a misaligned pointer.

**Mipmaps** (`dsp::MipTable` / `buildMipTable`, `Source/dsp/MipTable.h/.cpp`, `Source/dsp/WavetableStore.cpp`): each frame gets `MipTable::kMaxLevel + 1` (11) band-limited copies built with an FFT. Level *k* keeps `(frameSize >> k) / 2` harmonics, half those of level *k − 1*.

**The level is fractional, and the oscillator crossfades between two of them.** `levelForFrequency` returns `t + 1`, where `t = log2((frameSize / 2) / maxHarmonics)`: the integer part is the level to read, the fractional part is the weight towards the next one down. Picking the integer level `ceil(t)` — the highest-bandwidth level that stays under Nyquist — froze the brightness for a whole octave and then dropped it in one step. Measured as spectral centroid over the fundamental, the profile was flat within each octave with an 18% cliff at every boundary: note 54 and note 56 had the same absolute centroid, 601 Hz, so going up a whole tone made the sound duller instead of brighter. With the crossfade the largest step between adjacent semitones is 2.59% and the profile decreases monotonically across all 97 notes.

It costs brightness, deliberately: the effective harmonic count is `maxHarmonics / 2` rather than `maxHarmonics`, about 6% duller on the low notes (centroid over fundamental 4.04 → 3.79 at note 24). The alternative — weighting towards the *brighter* neighbour — would read a level whose harmonics are above Nyquist, which is the aliasing the pyramid exists to prevent. Aliasing is unchanged where it was worst (−72.7 dB) and up to 4 dB better from note 48 upwards. `getSample` therefore does four table reads per sample, two frames × two levels, and `log2` runs only in `levelForFrequency`, never per sample.

**The pyramid has to reach a single harmonic.** `kMaxLevel` is 10 because that is the first level that keeps exactly one harmonic on a 2048-sample frame — a plain sine. It is needed: at 12.5 kHz (MIDI 127) only the fundamental fits under Nyquist. The earlier value, 6, stopped the pyramid at 16 harmonics, so `levelForFrequency` had nothing narrower to pick for any note above ~1.4 kHz and every harmonic above Nyquist folded back. Measured against the harmonic energy of the note: −25 dB at MIDI 90, −17 dB at MIDI 96, −6 dB at MIDI 120 — more fold-back than note. With 11 levels the same measurement sits at −88 dB across the whole keyboard (`MipTable / nessun alias udibile su tutta l'estensione della tastiera`). The band-limiting loop also had an off-by-one — `bin < harmonics` instead of `bin <= harmonics` — which was invisible while the top level kept 16 harmonics but would have made the one-harmonic level silent.

**Every level stays `frameSize` samples long.** The band limiting lives in the spectrum, not in the buffer length. An earlier version also decimated the length (`frameSize >> k` samples), which meant a note around A4 played a 64-sample table and anything higher a 32-sample one: at that length the oscillator's linear interpolation between adjacent samples adds far more distortion than the band limiting removes, and it was the single largest contributor to the instrument sounding bad. The cost is memory — `(kMaxLevel + 1) × frameSize` per frame instead of ≈ `2 × frameSize`, about 5.8 MB for a 64 × 2048 table — and only one table is live at a time. `buildMipTable` returns `nullptr` when a frame is shorter than 2^(`kMaxLevel` + 1) (2048) samples, because the top level would keep zero harmonics; the shipped tables are 2048 samples/frame, so this path never triggers on real material.

**Loading and lifecycle** (`dsp::WavetableStore`): tables are embedded at build time via `juce_add_binary_data(WavetableAssets ... HEADER_NAME WavetableData.h)` — the explicit header name avoids colliding with the `WebUIAssets` target, which already generates its own `BinaryData.h` for the web-bundle embed. `WavetableStore::lookupBlob` copies each embedded blob into an aligned buffer before parsing it, because JUCE's generated resource arrays give no alignment guarantee and `parseXwt` refuses a misaligned pointer. Built `MipTable`s are never freed: the audio thread reads `WavetableStore::active()` (an atomic pointer) at any time, so their memory must outlive every possible concurrent read. `PluginProcessor::prepareToPlay` and its 25 Hz timer (`kWavetablePollHz`) both call `WavetableStore::setActive` and are serialised by `PluginProcessor::wavetableLock_` (a `juce::CriticalSection`), never taken on the audio thread. The pointer itself reaches the audio thread through a single-slot atomic mailbox, `SynthEngine::setPendingWavetable`, drained once per block inside `SynthEngine::process`: handing it to the voices directly from the message thread would race with the audio thread reading the oscillator's plain (non-atomic) fields.

## Presets

`Source/parameters/presets.json` is the single source of truth for the twelve factory presets. `scripts/gen-params.mjs` reads it, together with `parameters.json`, and emits four generated files: `Source/parameters/ParameterTable.h`, `WebUI/src/synth/params.generated.ts`, `Source/parameters/PresetTable.h`, `WebUI/src/synth/presets.generated.ts`.

`StateChannel::applyPreset` (`Source/bridge/StateChannel.cpp`) applies a preset on the message thread: for every spec in `params::kTable` it looks up a value in the preset's value list and falls back to the parameter's spec default (`spec.def`) when the preset doesn't mention it — this is what stops a preset from inheriting fragments of whatever sound was active before it was picked. Each parameter is set through `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture`, so host automation and undo see the change like any other parameter edit.

The same default-fallback rule is implemented twice — once here, once in `WebUI/src/juce/fake-backend.ts`'s `loadPreset` — because the web UI needs it without a JUCE host attached. Only the TypeScript side has a test (`WebUI/src/juce/fake-backend.test.ts`): `XerumTests` does not compile `Source/bridge/*`, so the two implementations can drift without either test suite noticing.

## Roadmap

1. **Scaffolding** (this phase) — build, MIDI path, WebView shell
2. **WavetableStore** — load / swap tables safely (done)
3. **WavetableOscillator** — interpolate + advance phase (done)
4. **WebRelay** — APVTS ↔ React (done)
5. **ModulationMatrix** — LFO / env / vel / mod wheel → seven targets, control rate (done; macros and a per-sample path are not part of it)
6. **Warp / unison**
7. **FX rack**
