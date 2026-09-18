# Architecture — SerumStyleSynth

## Principle

**JUCE lives at the boundary.** The real-time synth core (`engine/`, `dsp/`) stays free of UI and host glue so wavetable / mod-matrix work can grow without rewriting the plugin adapter.

## Layers

| Layer | Path | Responsibility |
|-------|------|----------------|
| Plugin adapter | `Source/plugin/` | `AudioProcessor`, APVTS, WebView editor |
| Parameters | `Source/parameters/` | Single source of parameter IDs + layout |
| Engine | `Source/engine/` | MIDI dispatch, voice pool, block render |
| DSP | `Source/dsp/` | Wavetable oscillator + mipmaps, TPT filter, ADSR envelope, `WavetableStore` |
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
- **Scaling uses `zoom`, not `transform: scale()`.** `transform` rasterises the subtree at its layout size (900×600) and stretches the bitmap, so above 1× — which is exactly what the JUCE host does, up to `kMaxScale = 1.5` in `PluginEditor.cpp` — text, 1 px borders and shadows all come out soft. `zoom` redoes the *layout*, so everything is repainted at the final resolution. The trade-off is that the chassis now occupies real layout space, which is fine: `.sx-root` centres it with flex and the fit still measures the container. The one surface `zoom` cannot fix by itself is the `<canvas>` in `WaveDisplay`: its backing store is multiplied by the effective scale by hand (`getBoundingClientRect().width / clientWidth`), and it redraws on `resize` because none of its data dependencies change when only the window size does.
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
- The engine is audible: a wavetable oscillator with band-limited mipmaps (`dsp::WavetableOscillator` / `dsp::MipTable`) feeds an exponential ADSR (`dsp::ADSREnvelope`) into a TPT state-variable filter (`dsp::StateVariableFilter`, 1 or 2 stages). 22 parameters are wired end to end: `oscOn`, `wtIndex`, `wtpos`, `oct`, `semi`, `fine`, `level`, `filtOn`, `ftype`, `slope`, `cutoff`, `res`, `drive`, `keytrk`, `att`, `dec`, `sus`, `rel`, `envVel`, `volume`, `pan`, `bypass`
- Still inert (accepted by the APVTS, no effect on sound yet): `envCurve`, `glide`, `voiceMode`, `unison`, `detune`, `warp`, every `l*` (LFO), `fx1On`/`ch*` (chorus), `fx2On`/`rv*` (reverb), every `arp*`

## Parameter mapping

`Source/parameters/ParamCollect.h` holds `collectEngineParams`, a header-only template that turns raw 0..1 parameter values into an `engine::EngineParams`. It takes an accessor keyed by `params::ParamSlot` (an enum, not a parameter id string) instead of reading the APVTS directly, for two reasons: the conversion arithmetic can be exercised in `XerumTests` with a fake accessor, no `juce_audio_processors` link needed; and at runtime `PluginProcessor` resolves one array of raw parameter pointers once in its constructor (`paramSlots_`, indexed by `ParamSlot`), so the per-block cost is a single array read per parameter — no name lookup, no string comparison.

This split exists because of a bug: an early version cast a denormalised `oct`/`semi` value with `(int)` instead of `juce::roundToInt`, truncating toward zero and landing on the wrong note for several settings (`denormalise()` can return a hair under the true integer, e.g. `7.999998`). The regression test written against that bug reimplemented the `Map::Linear` formula by hand, because at the time the real conversion lived inside `PluginProcessor.cpp`, which pulls in `juce_audio_processors` — a library `XerumTests` doesn't link — so the test verified its own copy of the arithmetic, not the code that ships. `ParamCollect.h` was carved out precisely so a test could call the real `collectEngineParams` (`Tests/EngineTests.cpp`, `ParamCollectTests`): if the truncating cast came back, that test would catch it; the earlier, hand-rolled one would not. Don't collapse this back into `PluginProcessor` for the sake of one fewer header.

## Voice engine

**Headroom.** `Source/engine/SynthVoice.cpp` applies a fixed per-voice gain, `kVoiceHeadroomGain = 0.4f` (-8 dB), to every sample before panning — not a `1 / numVoices` divisor, which would pump the overall level up and down every time a note starts or ends. Measured at the *plugin* output (engine + default `volume` of 0.8 linear, filter at Butterworth Q): a single note at `level = 1.0` sits at -13.8 dBFS, a four-note chord at -6.0 dBFS. Both numbers come from `Tests/EngineTests.cpp`, "gain staging: una nota e un accordo normale stanno sotto il soft clipper", which also pins them below the output soft clipper's 0.8 threshold and above an audibility floor.

This constant used to be `0.1f` (-20 dB), chosen when two other stages could each add uncontrolled gain: the filter multiplied its resonance peak stage by stage (see below), and `saturate()` had a small-signal slope of 1.5 so it added +3.5 dB even at drive zero. With those two fixed, -20 dB left a single note at -26 dBFS — an unusably quiet instrument. Anything that still exceeds full scale is caught by the output soft clipper rather than by keeping every voice far from it.

**Filter resonance** (`dsp::StateVariableFilter`). Two deliberate choices keep resonance from becoming a gain stage. First, in the 24 dB configuration the resonance sits on the *last* stage only and the first stays Butterworth: applying it to both made the peak grow as Q², over +40 dB at Q 12. Second, the filter input is attenuated by `sqrt(Qbutter / Q)`, so the peak grows as `sqrt(Q · Qbutter)` (about +9 dB at the top of the range) and the passband loses level as resonance rises — the way an analogue resonant filter steals bass. `res` maps to Q exponentially, 0.707 to 12 (`ParamCollect.h`), not linearly to 20: with the old linear map, `res` at its 30% default already meant Q 6.5.

**Output soft clipper** (`SynthEngine::process`). After the master gain, both channels pass through a soft clipper that is bit-transparent below 0.8 and asymptotes to 1.0 above it. It exists because the per-voice gain is a constant: a dense chord, or a high master volume, can still exceed full scale, and without it the host would receive hard-truncated samples. Its derivative is 1 at the threshold, so there is no corner where it engages, and it never fires during ordinary playing (see the gain-staging test above). `Tests/EngineTests.cpp`, "il soft clipper d'uscita non tocca il segnale sotto soglia", pins the transparency: doubling the master gain must double the peak exactly.

**Envelope timing.** `dsp::ADSREnvelope` (`Source/dsp/ADSREnvelope.cpp`) derives each stage's per-sample coefficient from an exact time constant, not a rounded approximation: attack reaches 0.99 of the peak in the nominal `att` seconds, decay covers 99% of its distance in `dec`, release falls under -80 dBFS — and the voice is freed — in `rel`. The constants are `ln(100)` for attack and decay and `ln(10000)` for release. Decay's 1% tolerance is checked against `decayDistance_`, the distance from peak to the sustain target *at the start of the decay stage*, not against the peak itself: an earlier version compared against the peak, and with sustain near 1.0 the peak-relative tolerance was wider than the actual (tiny) distance left to travel, so the stage's exit condition was already satisfied on the first sample and the decay curve never ran.

## Wavetables

Six tables ship under `Resources/wavetables/*.xwt` — `basic`, `saws`, `grit`, `vocal`, `bells`, `pwm`, in the order of the `wtIndex` choice options in `Source/parameters/parameters.json` — sourced from **Adventure Kid Waveforms (AKWF)**, CC0-1.0, provenance tracked in `Resources/wavetables/CREDITS.md`. Regenerate them with:

    node scripts/fetch-wavetables.mjs

This re-downloads the AKWF families from GitHub, resamples each cycle to 64 frames × 2048 samples, and overwrites the `.xwt` files and `CREDITS.md`. It runs rarely — the `.xwt` files are committed, and this only refreshes them from source.

**Format** (`dsp::parseXwt`, `Source/dsp/WavetableBlob.h/.cpp`): a 12-byte header — magic `"XWT1"`, little-endian `uint32` frame count, little-endian `uint32` frame size (must be a power of two) — followed by `frames × frameSize` `float32` samples, one frame after another. `parseXwt` rejects a wrong magic, a non-power-of-two frame size, a truncated buffer, sizes above its sanity caps (256 frames, 4096 samples/frame), or a misaligned pointer.

**Mipmaps** (`dsp::MipTable` / `buildMipTable`, `Source/dsp/MipTable.h/.cpp`, `Source/dsp/WavetableStore.cpp`): each frame gets `MipTable::kMaxLevel + 1` (7) band-limited copies built with an FFT. Level *k* keeps `(frameSize >> k) / 2` harmonics, half those of level *k − 1*, so the oscillator can pick the highest-bandwidth level whose harmonics stay under Nyquist for the note being played.

**Every level stays `frameSize` samples long.** The band limiting lives in the spectrum, not in the buffer length. An earlier version also decimated the length (`frameSize >> k` samples), which meant a note around A4 played a 64-sample table and anything higher a 32-sample one: at that length the oscillator's linear interpolation between adjacent samples adds far more distortion than the band limiting removes, and it was the single largest contributor to the instrument sounding bad. The cost is memory — `(kMaxLevel + 1) × frameSize` per frame instead of ≈ `2 × frameSize`, about 3.7 MB for a 64 × 2048 table — and only one table is live at a time. `buildMipTable` returns `nullptr` when a frame is shorter than 2^(`kMaxLevel` + 1) (128) samples, because the top level would keep zero harmonics; the shipped tables are 2048 samples/frame, so this path never triggers on real material.

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
5. **ModulationMatrix** — LFO / env / macro → targets
6. **Warp / unison**
7. **FX rack**
