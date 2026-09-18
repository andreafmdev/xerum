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
- Three materials via `variant` (`deep` default, `soft`, `glow`) — `synth.css` overrides the `@xerum/ui` hardware tokens on the chassis. In the browser: `?variant=glow&tab=lfo`.
- Every control is an `@xerum/ui` primitive (Knob with modulation rings and drop target, Segmented, Stepper, Meter, Tabs `bar`, Toggle, Panel, Button). Displays specific to the window (filter response, envelope, LFO scope, wavetable stack + spectrum) are app-level canvases/SVGs.
- **Known divergence:** `WaveDisplay` (`WebUI/src/synth/ui/WaveDisplay.tsx`) draws a procedural curve from `WebUI/src/synth/curves.ts`, not the real `.xwt` table data — deliberate, not an oversight.
- **Known gap:** the twelve factory presets (`Source/parameters/presets.json`) are structurally complete but their values have not been tuned by ear, and the preset browser (`PresetOverlay`) has not been clicked through end to end in the Standalone.

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

**Headroom.** `Source/engine/SynthVoice.cpp` applies a fixed per-voice gain, `kVoiceHeadroomGain = 0.1f` (-20 dB), to every sample before panning — not a `1 / numVoices` divisor, which would pump the overall level up and down every time a note starts or ends. Measured at the *engine* output (`SynthEngine::process`, master gain at unity): a single note at `level = 1.0` sits at -19.04 dBFS; a worst-case 16-voice chord (no phase cancellation) reaches -4.19 dBFS. `PluginProcessor::processBlock` used to multiply that by `volume × 6 dB` of fixed headroom left over from before this per-voice gain existed — double-counting it, so the same 16-voice chord reached -0.12 dBFS (0.986 linear) at default volume, 1.5% short of clipping in the host. That fixed boost is gone: `volume` is now applied as the plain linear APVTS value, no hidden multiplier. Measured at the *plugin* output (engine + default volume, 0.8 linear, no boost): a single note at `level = 1.0` sits at -25.97 dBFS (0.0503 linear); the same 16-voice chord reaches -11.24 dBFS (0.274 linear) — comfortably short of clipping, with room to spare for unison and FX still to come. It's one constant, picked for headroom safety now, meant to be revisited once the rest of the engine (unison, FX) is in and the gain-staging picture is complete.

**Envelope timing.** `dsp::ADSREnvelope` (`Source/dsp/ADSREnvelope.cpp`) derives each stage's per-sample coefficient from an exact time constant, not a rounded approximation: attack reaches 0.99 of the peak in the nominal `att` seconds, decay covers 99% of its distance in `dec`, release falls under -80 dBFS — and the voice is freed — in `rel`. The constants are `ln(100)` for attack and decay and `ln(10000)` for release. Decay's 1% tolerance is checked against `decayDistance_`, the distance from peak to the sustain target *at the start of the decay stage*, not against the peak itself: an earlier version compared against the peak, and with sustain near 1.0 the peak-relative tolerance was wider than the actual (tiny) distance left to travel, so the stage's exit condition was already satisfied on the first sample and the decay curve never ran.

## Wavetables

Six tables ship under `Resources/wavetables/*.xwt` — `basic`, `saws`, `grit`, `vocal`, `bells`, `pwm`, in the order of the `wtIndex` choice options in `Source/parameters/parameters.json` — sourced from **Adventure Kid Waveforms (AKWF)**, CC0-1.0, provenance tracked in `Resources/wavetables/CREDITS.md`. Regenerate them with:

    node scripts/fetch-wavetables.mjs

This re-downloads the AKWF families from GitHub, resamples each cycle to 64 frames × 2048 samples, and overwrites the `.xwt` files and `CREDITS.md`. It runs rarely — the `.xwt` files are committed, and this only refreshes them from source.

**Format** (`dsp::parseXwt`, `Source/dsp/WavetableBlob.h/.cpp`): a 12-byte header — magic `"XWT1"`, little-endian `uint32` frame count, little-endian `uint32` frame size (must be a power of two) — followed by `frames × frameSize` `float32` samples, one frame after another. `parseXwt` rejects a wrong magic, a non-power-of-two frame size, a truncated buffer, sizes above its sanity caps (256 frames, 4096 samples/frame), or a misaligned pointer.

**Mipmaps** (`dsp::MipTable` / `buildMipTable`, `Source/dsp/MipTable.h/.cpp`, `Source/dsp/WavetableStore.cpp`): each frame gets `MipTable::kMaxLevel + 1` (7) band-limited copies built with an FFT — level *k* keeps `frameSize >> k` samples, half the harmonics of level *k − 1*, so the oscillator can pick the shortest level whose harmonics stay under Nyquist for the note being played. `buildMipTable` returns `nullptr` when a frame is shorter than 2^`kMaxLevel` (64) samples rather than construct an FFT with a negative order — a null table means silence, not a crash. The shipped tables are 2048 samples/frame, so this path never triggers on real material.

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
