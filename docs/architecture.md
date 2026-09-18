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

## Wavetables

Six tables ship under `Resources/wavetables/*.xwt` — `basic`, `saws`, `grit`, `vocal`, `bells`, `pwm`, in the order of the `wtIndex` choice options in `Source/parameters/parameters.json` — sourced from **Adventure Kid Waveforms (AKWF)**, CC0-1.0, provenance tracked in `Resources/wavetables/CREDITS.md`. Regenerate them with:

    node scripts/fetch-wavetables.mjs

This re-downloads the AKWF families from GitHub, resamples each cycle to 64 frames × 2048 samples, and overwrites the `.xwt` files and `CREDITS.md`. It runs rarely — the `.xwt` files are committed, and this only refreshes them from source.

**Format** (`dsp::parseXwt`, `Source/dsp/WavetableBlob.h/.cpp`): a 12-byte header — magic `"XWT1"`, little-endian `uint32` frame count, little-endian `uint32` frame size (must be a power of two) — followed by `frames × frameSize` `float32` samples, one frame after another. `parseXwt` rejects a wrong magic, a non-power-of-two frame size, a truncated buffer, sizes above its sanity caps (256 frames, 4096 samples/frame), or a misaligned pointer.

**Mipmaps** (`dsp::MipTable` / `buildMipTable`, `Source/dsp/MipTable.h/.cpp`, `Source/dsp/WavetableStore.cpp`): each frame gets `MipTable::kMaxLevel + 1` (7) band-limited copies built with an FFT — level *k* keeps `frameSize >> k` samples, half the harmonics of level *k − 1*, so the oscillator can pick the shortest level whose harmonics stay under Nyquist for the note being played. `buildMipTable` returns `nullptr` when a frame is shorter than 2^`kMaxLevel` (64) samples rather than construct an FFT with a negative order — a null table means silence, not a crash. The shipped tables are 2048 samples/frame, so this path never triggers on real material.

**Loading and lifecycle** (`dsp::WavetableStore`): tables are embedded at build time via `juce_add_binary_data(WavetableAssets ... HEADER_NAME WavetableData.h)` — the explicit header name avoids colliding with the `WebUIAssets` target, which already generates its own `BinaryData.h` for the web-bundle embed. `WavetableStore::lookupBlob` copies each embedded blob into an aligned buffer before parsing it, because JUCE's generated resource arrays give no alignment guarantee and `parseXwt` refuses a misaligned pointer. Built `MipTable`s are never freed: the audio thread reads `WavetableStore::active()` (an atomic pointer) at any time, so their memory must outlive every possible concurrent read. `PluginProcessor::prepareToPlay` and its 25 Hz timer (`kWavetablePollHz`) both call `WavetableStore::setActive` and are serialised by `PluginProcessor::wavetableLock_` (a `juce::CriticalSection`), never taken on the audio thread. The pointer itself reaches the audio thread through a single-slot atomic mailbox, `SynthEngine::setPendingWavetable`, drained once per block inside `SynthEngine::process`: handing it to the voices directly from the message thread would race with the audio thread reading the oscillator's plain (non-atomic) fields.

## Roadmap

1. **Scaffolding** (this phase) — build, MIDI path, WebView shell
2. **WavetableStore** — load / swap tables safely (done)
3. **WavetableOscillator** — interpolate + advance phase (done)
4. **WebRelay** — APVTS ↔ React (done)
5. **ModulationMatrix** — LFO / env / macro → targets
6. **Warp / unison**
7. **FX rack**
