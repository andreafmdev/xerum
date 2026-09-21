# The JUCE ↔ React bridge

How the plugin's C++ half and its React half talk to each other, and how the WebView that hosts
the UI is set up, served and torn down.

This is the companion to `docs/architecture.md`, which describes the audio side. Here the subject
is the seam: what crosses it, in which direction, on which thread, and what happens when one side
is older than the other. `docs/build.md` covers how to build and run the two halves.

---

## 1. The shape of the thing

Xerum has no native widgets. The entire editor is one `juce::WebBrowserComponent` filling the
plugin window, and everything a user sees — panels, knobs, keyboard, meters — is a React tree
rendered inside it. The C++ side owns the audio, the parameters and the saved state; the web side
owns the pixels and the interaction.

```
┌──────────────────────────── plugin process ─────────────────────────────┐
│                                                                          │
│   audio thread                    message thread                         │
│   ───────────                     ──────────────                         │
│   processBlock()                  XerumAudioProcessorEditor              │
│        │                                   │                             │
│        │  atomics                          │ owns                        │
│        ▼                                   ▼                             │
│   engine::MeterFrame ──────────►  bridge::MeterChannel (30 Hz Timer)     │
│                                            │                             │
│   juce::AudioProcessorValueTreeState       │                             │
│    ├── parameters ────────────►  bridge::WebRelays                       │
│    └── ValueTree (mods, arp) ─►  bridge::StateChannel                    │
│                                   bridge::MidiChannel                    │
│                                   bridge::MidiDeviceChannel  ┐           │
│                                   bridge::AudioSettingsChannel├ standalone│
│                                   bridge::WindowChannel      ┘  only     │
│                                            │                             │
│                                            ▼                             │
│                              juce::WebBrowserComponent                   │
│                                 (WKWebView / WebView2)                   │
│  ┌─────────────────────────────────┼──────────────────────────────────┐  │
│  │                        window.__JUCE__                             │  │
│  │                                 │                                  │  │
│  │   WebUI/src/juce/juce-backend.ts  ──implements──►  Backend         │  │
│  │                                 │                                  │  │
│  │   WebUI/src/juce/hooks.ts  ◄────┘   React hooks                    │  │
│  │                                 │                                  │  │
│  │   WebUI/src/synth/ui/*      React tree, @xerum/ui components       │  │
│  └────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────┘
```

**Everything in `Source/bridge/` runs on the message thread and nothing else.** The audio thread
never touches the WebView, never allocates for it and never blocks on it. The only data the audio
thread publishes is a handful of atomics in `engine::MeterFrame`, which the message thread polls.
That single rule is what keeps a JavaScript engine — garbage-collected, unbounded in latency —
out of the real-time path.

---

## 2. The four transports

JUCE 9's WebView integration gives four distinct mechanisms. They are not interchangeable, and
picking the wrong one is the most common way to make this bridge misbehave.

| Transport | Direction | Shape | Used for |
|---|---|---|---|
| **Relay** | both ways | typed, per-parameter, host-aware | every plugin parameter |
| **Native function** | web → C++, with a reply | `Promise`-returning call | commands and queries |
| **Event** | C++ → web, no reply | fire-and-forget broadcast | meters, state changes, device changes |
| **Resource provider** | C++ → web | HTTP-like fetch | serving the page itself in Release |

All four are configured on a single `juce::WebBrowserComponent::Options` object, built by
`makeWebOptions()` in `Source/plugin/PluginEditor.cpp`, before the WebView exists.

### 2.1 Why the construction order is not negotiable

```
1.  construct the channels          relays_, stateChannel_, midiChannel_, …
2.  channel.applyTo(options)        registers native functions + initialisation data
3.  construct the WebBrowserComponent with those options
4.  relays_.attach(apvts)           binds relays to parameters
5.  channel.setWebView(&webView_)   gives channels a way to emit events
```

Steps 2 and 3 cannot be swapped: `Options` is consumed by the `WebBrowserComponent` constructor,
so a native function registered afterwards is registered on nothing. Step 4 cannot happen before
step 3 either — JUCE's own contract, restated in the comment on `WebRelays`.

This is why every channel is declared **before** `webView_` in `PluginEditor.h`, with one
deliberate exception:

```cpp
juce::WebBrowserComponent webView_;

/** Timer a 30 Hz che manda i picchi dei meter alla WebView: tiene un riferimento a
    webView_, quindi va dichiarato dopo di lei per essere distrutto prima. */
bridge::MeterChannel meters_;
```

`MeterChannel` holds a reference to the WebView and fires on a timer, so it must die first.
Members are destroyed in reverse declaration order, hence it is declared last. The other channels
are declared first (so their `applyTo` can run before the WebView is built) and therefore die
*after* it — which is why the destructor explicitly unhooks them:

```cpp
XerumAudioProcessorEditor::~XerumAudioProcessorEditor()
{
    stateChannel_.setWebView (nullptr);
    midiDevices_.setWebView (nullptr);
    audioSettings_.setWebView (nullptr);
    window_.setWebView (nullptr);
}
```

Forget one of those lines and a channel can emit into a destroyed WebView during teardown.

---

## 3. Parameters: the relay path

### 3.1 One source of truth, two generated files

Parameter identity is not written twice. `Source/parameters/parameters.json` is the source;
`node scripts/gen-params.mjs` (or `cd WebUI && pnpm gen:params`) emits both sides:

```
Source/parameters/parameters.json
                │
                ├──► Source/parameters/ParameterTable.h      (params::kTable)
                └──► WebUI/src/synth/params.generated.ts     (PARAM_SPECS, ParamId)

Source/parameters/presets.json (+ presets.pack.json)
                ├──► Source/parameters/PresetTable.h
                └──► WebUI/src/synth/presets.generated.ts
```

Both generated files carry a `GENERATED — non modificare a mano` banner. Editing them by hand is
how the two halves drift apart.

`WebRelays` walks `params::kTable` and creates one relay per entry, keyed by the parameter id:

```cpp
for (const auto& s : params::kTable)
    switch (s.kind)
    {
        case params::Kind::Bool:   toggles_.push_back ({ s.id, std::make_unique<juce::WebToggleButtonRelay> (s.id) }); break;
        case params::Kind::Choice: combos_ .push_back ({ s.id, std::make_unique<juce::WebComboBoxRelay>     (s.id) }); break;
        case params::Kind::Float:
        case params::Kind::Int:    sliders_.push_back ({ s.id, std::make_unique<juce::WebSliderRelay>       (s.id) }); break;
    }
```

The relay name **is** the APVTS parameter id. `attach()` looks each one up and asserts that every
relay found its parameter — if that assertion fires, the generated table and the APVTS layout have
diverged.

### 3.2 What the web side receives

JUCE ships the relay inventory in `window.__JUCE__.initialisationData` as three arrays:
`__juce__sliders`, `__juce__toggles`, `__juce__comboBoxes`. `createJuceBackend()` uses them to
decide which handle to build, and — crucially — what to do when an id is **missing**:

```ts
if (spec.kind === "bool"   && init.__juce__toggles.includes(id))    h = new ToggleHandle(juce.getToggleState(id));
else if (spec.kind === "choice" && init.__juce__comboBoxes.includes(id)) h = new ComboHandle(juce.getComboBoxState(id), id);
else if ((spec.kind === "float" || spec.kind === "int") && init.__juce__sliders.includes(id)) h = new SliderHandle(juce.getSliderState(id));
else h = new OrphanHandle(defaultNormalised(spec), id);
```

`OrphanHandle` is the version-skew valve. A WebUI newer than the binary — a dev server pointed at
an old plugin, a hot-reloaded UI after a `parameters.json` change without a rebuild — renders the
new control as visible but inert, logs one warning, and keeps going. Without it the whole editor
would throw on the first unknown id and show a blank window.

### 3.3 Normalisation: the UI speaks 0..1 only

`ParamHandle` exposes a single numeric domain: normalised 0..1. Bools are 0/1, ints are
`(n - min) / (max - min)`, choices are `index / (n - 1)`. The conversions live in
`WebUI/src/synth/mapping.ts` (`fromInt`, `toInt`, `fromIndex`, `toIndex`) and the display strings
in `formatValue`.

This is *not* the same convention the audio side uses internally. `getRawParameterValue` returns
**natural** units for `Kind::Int` and `Kind::Choice` — see the long warning in
`docs/architecture.md` → *Parameter mapping*, which documents a bug that shipped for months. The
bridge is the boundary where the two conventions meet; keep the UI on normalised values and do the
conversion once, here.

### 3.4 A knob drag, end to end

```
pointerdown on the cap
   │
   ├─► ParamKnob → useFloatParam(id).begin()
   │       └─► SliderHandle.begin()
   │             dragging = true
   │             local    = current value            ← freeze a local truth
   │             st.sliderDragStarted()  ──────────► WebSliderParameterAttachment
   │                                                    └─► parameter->beginChangeGesture()
   │                                                          └─► host starts an automation write
   │
   ├─► pointermove … set(v)
   │       └─► SliderHandle.set(v)
   │             local = v
   │             st.setNormalisedValue(v) ─────────► APVTS parameter
   │             this.notify()                       ← local subscribers, immediately
   │
   └─► pointerup → end()
           └─► dragging = false
               st.sliderDragEnded() ───────────────► parameter->endChangeGesture()
```

Two details in there are load-bearing.

**`notify()` on every `set`.** JUCE's relay `set*` methods do not fire the relay's own
`valueChangedEvent` — only changes coming *from* C++ do. Without the explicit `notify()` the knob
would only redraw when the C++ echo came back, one message-loop turn late, and dragging would feel
like dragging through syrup.

**`dragging` / `local`.** While a gesture is open, `get()` returns the locally held value and
ignores echoes. The host can be writing automation for the same parameter at the same time; without
this the knob would jump between the user's finger and the host's value at 30 Hz.

The mirror direction — host automation, a preset load, an undo — arrives as the relay's
`valueChangedEvent`, fans out to the handle's subscriber set, and `useSyncExternalStore` re-renders
exactly the components that read that parameter.

### 3.5 Subscription plumbing

`useHandle` memoises the store object per handle:

```ts
const store = useMemo(() => ({ subscribe: (cb) => h.subscribe(cb), get: () => h.get() }), [h]);
const value = useSyncExternalStore(store.subscribe, store.get);
```

Without the `useMemo`, `subscribe` and `get` would be fresh closures on every render and
`useSyncExternalStore` would unsubscribe and resubscribe on every commit.

---

## 4. Non-parametric state: the ValueTree path

Mod matrix assignments and the 16 arpeggiator steps are not parameters — they are a variable-length
structure, and the host has no business automating them. They live in the APVTS `ValueTree` under
`MODS` and `ARP` (`Source/state/StateTree.h`) and cross the bridge through `StateChannel`.

```
   web                                        C++
   ───                                        ───
   backend.getState()        ──native fn──►   state::toVar(apvts.state, {})
                             ◄──reply─────    { version, mods[], arpSteps[] }

   backend.setMods(m, origin)──native fn──►   state::setMods(...)  → ValueTree mutation
                                                    │
                                              ValueTree::Listener
                                                    │
                                              triggerAsyncUpdate()   ← coalescing
                                                    │
                                              handleAsyncUpdate()
                                                    │
   onStateChanged(cb)        ◄────event────   emitEventIfBrowserIsVisible("stateChanged", …)
```

### 4.1 Coalescing

`setMods` is implemented as *remove all children, then append N* — that is `2N` listener callbacks
for one logical edit. Emitting synchronously from each would mean `2N` serialisations and `2N`
`evaluateJavascript` round trips while the user drags a depth slider. `AsyncUpdater` collapses them
into one event per message-loop turn:

```cpp
void StateChannel::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) { if (isOurs (t)) triggerAsyncUpdate(); }
```

### 4.2 Echo suppression, and why `origin` exists

Every `useBridgeState` instance mints a random `origin` string at mount. Writes carry it; the event
that comes back carries it too; the UI ignores its own echo:

```ts
const off = backend.onStateChanged((s) => {
  if (s?.origin !== origin.current) apply(s);
});
```

Without this, a local optimistic update would be immediately overwritten by the round trip — at
best a flicker, at worst a fight between the dragged slider and the returning value.

C++ holds the last origin in `lastOrigin_` and clears it **after** the emit, not before, so that a
change with no origin at all (host automation, undo, `replaceState`) really does arrive with an
empty string rather than inheriting the previous UI write's identity.

### 4.3 Whole-tree replacement

`setStateInformation` (session load) replaces the root `ValueTree` outright. The listener attached
to the old tree would go deaf. `StateChannel` listens to a `ChangeBroadcaster` for exactly that:

```cpp
void StateChannel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    cancelPendingUpdate();      // a pending emit refers to the OLD tree
    lastOrigin_.clear();
    listenTo (apvts_.state);    // re-attach to the new one
    emitState ({});             // synchronous, origin empty = external
}
```

### 4.4 Two commands that live here: preset load and factory reset

`StateChannel` also owns `loadPreset(index)` and `resetToDefaults()`. Both are commands, not state,
and both write parameters rather than the `ValueTree` — which is why they are worth reading.

```cpp
parameter->beginChangeGesture();
parameter->setValueNotifyingHost (params::presetValue (preset, spec));
parameter->endChangeGesture();
```

The gesture wrapper is not decoration: without it the host neither records the change nor offers an
undo for it. A preset load is, from the DAW's point of view, a large automation write.

`resetToDefaults` carries the sharper lesson. JUCE's own "Reset to default state" in
`juce_StandaloneFilterWindow.h` calls `clearContentComponent()` and `deletePlugin()` — which, from
here, would **destroy the WebView from inside the native function the WebView is currently
executing**. So this implementation does not recreate anything: it writes every parameter back to
its default, resets mods and arp steps through the same `setMods`/`setArpSteps` path the UI uses
(so the normal `stateChanged` event arrives on its own, rather than through a second write path),
and — in Standalone only — removes the saved `filterState`, which is the reason the command exists
at all: without that line a crash before the next clean close would resurrect the patch the user
just discarded.

### 4.5 The bridge is a trust boundary

`parseState` in `hooks.ts` validates every payload before it reaches React state — version, shape
of every mod, exactly 16 numeric steps:

```ts
export function parseState(raw: unknown): BridgeState | null {
  const o = raw as Record<string, unknown> | null;
  if (typeof o !== "object" || o === null) return null;
  if (o.version !== 1) return null;
  if (!Array.isArray(o.mods) || !o.mods.every(isMod)) return null;
  if (!Array.isArray(o.arpSteps) || o.arpSteps.length !== ARP_STEPS || !o.arpSteps.every((n) => typeof n === "number")) return null;
  return { version: 1, mods: o.mods, arpSteps: o.arpSteps };
}
```

An older binary, or a malformed payload, is logged and dropped — it never enters UI state.

---

## 5. Meters: the 30 Hz path

This is the only place where audio-rate data reaches the UI, and it is deliberately the narrowest
channel in the bridge.

```
audio thread                       message thread                        web
────────────                       ──────────────                        ───
processBlock()
  frame.inPeak.store(max)      ┌── MeterChannel::timerCallback (30 Hz)
  frame.lfo.store(v)           │     in   = inPeak.exchange(0)   ← peaks are consumed
  frame.notesLo/Hi.store(mask) │     lfo  = lfo.load()           ← levels are sampled
                               │     n0..n3 = splitNoteMask(lo, hi)
                               └──►  emitEventIfBrowserIsVisible("meters", obj)
                                                                   │
                                                    meterStore(backend).apply(frame)
                                                                   │
                                            useSyncExternalStore(selector) per component
```

Three decisions worth knowing:

**`exchange` vs `load`.** `in`, `out`, `env`, `env2`, `vel` are transient unipolar peaks: between
two timer ticks — 33 ms — one can be born and die, so the audio thread stores a running maximum and
the reader consumes it with `exchange(0)`. `lfo` is bipolar (a maximum would lose the sign, or
never come back down) and `mw` is a position, not a transient: both are sampled with `load()`.

**Four 32-bit words, not two 64-bit ones.** The sounding-note mask is 128 bits. It travels as JSON,
where numbers are doubles, and a `uint64` does not fit exactly in a double's mantissa. `n0..n3`
each carry 32 notes. `isNoteActive` in `backend.ts` handles the sign quirk of JS bitwise operators
on the top bit.

**Peak-hold lives on the web side, and only for the two audio meters.** `meters.ts` applies a
time-based decay so a one-frame peak stays readable:

```ts
const elapsedTicks = at === null ? Infinity : (now - at) / (1000 / 30);
const decay = Math.min(1, Math.pow(0.85, elapsedTicks));
frame = { in: Math.max(finite(m.in), p.in * decay), … };
```

Time-based, not event-based, so a duplicated listener after a remount does not compress twice. The
five modulation sources pass through untouched: they are already peaks, and holding them would make
the ring fall visibly after the envelope it is meant to show.

**One listener for the whole UI.** `meterStore` is a `WeakMap<Backend, MeterStore>`; components
subscribe with a selector, so a component reading `f.lfo` re-renders when the LFO moves, not 30
times a second regardless. The store attaches to `backend.onMeters` with its first subscriber and
detaches with its last.

**Never put data inside `emitEvent` that the UI could have computed itself.** The frame is twelve
numbers: eight scalars and the four words of the note mask. Everything else the UI derives locally.

---

## 6. MIDI played inside the UI

The on-screen keyboard and the wheels are ordinary DOM, but the notes they produce must be
indistinguishable from the host's.

```
Keybed pointerdown ──► backend.noteOn(note, vel) ──native fn──► MidiChannel
                                                                   │
                                                    keyboardState_.noteOn (1, note, vel)
                                                                   │
                                          juce::MidiKeyboardState  │
                                                                   ▼
                                      processBlock(): keyboardState_.processNextMidiBuffer(...)
                                                                   │
                                                          the same MidiBuffer the host fills
```

Wheels take a different road, because pitch bend and mod wheel are continuous and must survive
between blocks:

```
setWheel("pitch", v) ──► processor_.setUiPitchBend(v) ──► atomic ──► real MIDI events in processBlock
```

`allNotesOff()` is not cosmetic. If the WebView loses the pointer mid-click — `pointercancel`, a
blur, an unmount — the note has no `noteOff` coming and will sound forever. The UI calls it on all
three.

---

## 7. Standalone-only channels

Three channels exist only when `JucePlugin_Build_Standalone`. In AU/VST3 the native functions are
still registered, but they answer with empty/neutral values, so the UI needs no branch of its own —
it asks, gets `{ host: true, devices: [] }` or `standalone: false`, and renders accordingly.

| Channel | Native functions | What it wraps |
|---|---|---|
| `MidiDeviceChannel` | `getMidiInputs`, `setMidiInputEnabled` + `midiInputsChanged` event | system MIDI inputs |
| `AudioSettingsChannel` | `getAudioSettings`, `setAudioOutput`, `setSampleRate`, `setBufferSize` + `audioSettingsChanged` event | the audio device |
| `WindowChannel` | `getWindowChrome`, `beginWindowDrag`, `moveWindowBy`, `toggleWindowZoom` | the chromeless native window (macOS) |

The setters in `AudioSettingsChannel` return `""` on success and the error message otherwise —
a string, not a boolean, so the UI can show what actually went wrong.

`beginWindowDrag` deserves a note: it returns `false` when the native drag could not start, which
in WKWebView is routine rather than exceptional. Bridge messages are delivered asynchronously, so
by the time C++ runs, the current event may no longer be the `mousedown` that AppKit needs. The UI
then falls back to tracking `mousemove` itself and calling `moveWindowBy`. That fallback is the
normal path on several macOS versions, not a workaround for a bug.

---

## 8. Serving the page

### 8.1 Two modes

```
Debug                                    Release (XERUM_EMBED_WEBUI=ON)
─────                                    ──────────────────────────────
Vite dev server on :5173                 WebUI/dist → juce_add_binary_data(WebUIAssets)
(XERUM_WEBUI_URL overrides)                        → compiled into the binary
        │                                              │
webView_.goToURL("http://localhost:5173?gutter=0")     │
                                        webView_.goToURL(getResourceProviderRoot() + "?gutter=0")
                                                       │
                                        bridge::webAssets::lookup(url)
```

The choice is made by `bridge::webAssets::embedded()`, which is just `#if XERUM_EMBED_WEBUI`. In
Debug the resource provider is registered but never consulted; in Release there is no dev server to
fall back to.

### 8.2 The resource provider

`WebAssets.cpp` resolves a URL to a `Resource`. Two subtleties:

```cpp
auto name = url.upToFirstOccurrenceOf ("?", false, false)   // strip ?v=…, ?import
               .fromLastOccurrenceOf ("/", false, false);   // basename is enough: Vite hashes names
if (name.isEmpty()) name = "index.html";
```

Vite emits content-hashed filenames, so the basename is globally unique and no directory walk is
needed. The query string must be stripped first or `index-abc.js?v=1` matches nothing. MIME types
come from a small extension table; getting one wrong shows up as a blank page with a console error
about a refused module type.

### 8.3 Build-time wiring

`XERUM_EMBED_WEBUI` requires `WebUI/dist/index.html` to exist **at configure time**, because
`juce_add_binary_data` needs the file list then:

```cmake
if(XERUM_EMBED_WEBUI)
    # fails with a pointer to `cd WebUI && pnpm build` if dist is missing
    juce_add_binary_data(WebUIAssets SOURCES ${XERUM_WEBUI_FILES} NAMESPACE WebUIAssets)
    target_link_libraries(Xerum PRIVATE WebUIAssets)
    target_compile_definitions(Xerum PRIVATE XERUM_EMBED_WEBUI=1)
endif()
```

There is also a `webui` target that rebuilds `WebUI/dist` when web sources change, wired as a
dependency of `WebUIAssets`. Adding a *new file* to the bundle still needs a CMake reconfigure —
the glob is evaluated at configure time.

---

## 9. Booting, and running with no host at all

`WebUI/src/main.tsx` picks a backend before React mounts:

```ts
const backend = hasJuce()
  ? await createJuceBackend().catch((e) => { console.error("[bridge] boot fallito", e); return new FakeBackend(); })
  : (console.info("[bridge] nessun host JUCE: FakeBackend demo"), new FakeBackend({ demo: true }));
```

Three cases, one tree:

| Where | `hasJuce()` | Backend |
|---|---|---|
| inside the plugin | true | `JuceBackend` |
| inside the plugin, boot threw | true | `FakeBackend` — inert but visible, never a white window |
| plain browser, Storybook, tests | false | `FakeBackend({ demo: true })` — animated, self-driving |

`hasJuce()` checks `window.__JUCE__?.backend`. The `@juce-framework/webview` package declares
`window.__JUCE__` as always present — true inside the WebView, false everywhere else — so
`juce-backend.ts` reads it as optional, which is the runtime truth.

`FakeBackend` is not a stub. It implements the whole `Backend` interface, holds parameter values,
mirrors the C++ preset rules and the default arp pattern, and generates meter frames. It is what
makes `pnpm dev`, Storybook and the whole app test suite possible without launching a DAW. **Its
duplication of C++ logic is deliberate and must be maintained by hand** — `StateChannel::applyPreset`
and `params::presetValue` carry comments saying so, because there is no way to share that code
across the two languages.

---

## 10. The geometry contract

The window has one free variable: its width. Everything else is derived, on both sides, from the
same three constants.

```
C++  (PluginEditor.cpp)                    web  (SynthWindow.tsx)
kChassisWidth  = 900                       W   = 900
kChassisHeight = 690                       H   = 690
kMaxScale      = 1.5                       MAX_SCALE = 1.5

scaleForWidth(w) = clamp(w/900, 0.72, 1.5)     fitScale(w, h, gutter) =
heightForWidth(w) = ceil(690 * scale)            min((w-gutter)/900, (h-gutter)/690, 1.5)
```

They are **not** the same formula and are not meant to be: the C++ decides how big the *window* may
be (`ChassisConstrainer` re-derives the height from the width on every drag), the web decides how
the chassis fills the WebView it is given. `kMinScale` deliberately has no web twin — below the
minimum the right thing is to keep shrinking, not to overflow.

The editor loads the UI with `?gutter=0`, which tells `SynthWindow` to fill edge to edge with no
margin (the default is 16, for the browser).

`H` exists in three places: `SynthWindow.tsx`, the `.sx-chassis` rule in `synth.css`, and
`kChassisHeight`. Two tests in `WebUI/src/synth/ui/SynthWindow.test.tsx` re-read the C++ and CSS
sources and pin them against the exported `H`, so changing one and not the others fails `pnpm test`
rather than a review.

Scaling itself is `zoom` with a measured fallback to `transform` — see the long comment on
`ScaleMode` in `SynthWindow.tsx`; older WebKit implements `zoom` in a way that divides descendants
instead of multiplying them.

---

## 11. Extending the bridge

### Adding a parameter

1. Add the entry to `Source/parameters/parameters.json`.
2. `cd WebUI && pnpm gen:params` — regenerates `ParameterTable.h` and `params.generated.ts`.
3. Nothing else. `WebRelays` picks it up from `kTable`, the relay name is the id, and
   `PARAM_SPECS` gives the UI its range, mapping and label.
4. Use it: `useFloatParam("newId")` / `useBoolParam` / `useChoiceParam` / `useIntParam`.
5. If it must reach the engine, give it `"slot": true` and add the `ParamSlot` case — that is the
   audio side, see `docs/architecture.md` → *Parameter mapping*.

A rebuild of the C++ is required before the UI stops treating it as an orphan.

### Adding a native function

```cpp
// in SomeChannel::applyTo
return options.withNativeFunction ("doThing",
    [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
    {
        // message thread. Arguments arrive as juce::var; validate them.
        done (juce::var { result });   // always call done(), exactly once
    });
```

```ts
// in juce-backend.ts
doThing: (x: number) => call("doThing")(x) as Promise<Result>,
// and in fake-backend.ts, an implementation that behaves the same
```

Rules: add it to the `Backend` interface so both implementations must provide it; call `done()`
exactly once on every path, including early returns, or the JS promise never settles; never do
blocking or long work inside — you are on the message thread and the UI is waiting.

### Adding an event

```cpp
webView_->emitEventIfBrowserIsVisible ("thingChanged", payload);
```

Use `emitEventIfBrowserIsVisible`, not `emitEvent`: it skips the work when the editor is hidden.
On the web side add a `fanOut<T>("thingChanged")` in `createJuceBackend` — one JUCE listener per
event name, distributed to a local `Set`, so unsubscribing is a set deletion rather than bookkeeping
of JUCE listener handles.

Validate the payload before it reaches React state, the way `parseState` does.

---

## 12. Rules, in one list

1. `Source/bridge/` is message-thread-only. No exceptions.
2. The audio thread publishes atomics; the message thread polls them.
3. Channels are constructed before the WebView, attached after it, and unhooked in the destructor.
4. `MeterChannel` is declared after `webView_` so it dies before it.
5. The UI speaks normalised 0..1 everywhere; conversion happens at the bridge.
6. Every payload crossing into React state is validated first.
7. Local writes carry an `origin`; echoes with that origin are ignored.
8. An unknown parameter id degrades to an inert control, never to an exception.
9. Generated files are generated. Edit `parameters.json`, not `params.generated.ts`.
10. `FakeBackend` mirrors C++ behaviour by hand — when you change one, change the other.

---

## 13. Where the tests are

| Seam | Test |
|---|---|
| `JuceBackend` against a mocked `window.__JUCE__` | `WebUI/src/juce/juce-backend.test.ts` |
| `FakeBackend` behaviour and parity | `WebUI/src/juce/fake-backend.test.ts` |
| Hooks, `parseState`, echo suppression | `WebUI/src/juce/hooks.test.tsx` |
| The whole window on a fake backend | `WebUI/src/synth/ui/SynthWindow.test.tsx` |
| Geometry contract across C++/CSS/TS | same file, the tests that re-read the sources |
| Parameter seam (real APVTS → engine) | `Tests/ParameterSeamTests.cpp` |

**`Source/bridge/*` has no direct C++ test.** `StateChannel::applyPreset` in particular is exercised
only through its TypeScript mirror in `FakeBackend`. That is a known gap, recorded here rather than
in a comment nobody reads.
