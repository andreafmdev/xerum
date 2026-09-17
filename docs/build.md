# Build — SerumStyleSynth

## Prerequisites (macOS)

- Xcode (Command Line Tools / full IDE)
- CMake ≥ 3.22 (`brew install cmake`)
- Node.js 18+ (for `WebUI`)
- Git submodule for JUCE:

```bash
git submodule update --init --recursive
```

JUCE is pinned under `external/JUCE` (tag `8.0.6`).

## Configure & build

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug
```

Release:

```bash
cmake --preset macos-release
cmake --build --preset macos-release
```

Artifacts land under `build/macos-debug/` (Xcode layout). With `COPY_PLUGIN_AFTER_BUILD`, AU/VST3 are also copied into the user plugin folders.

Standalone app is useful for MIDI smoke tests without a DAW.

## Web UI (dev)

```bash
cd WebUI
npm install
npm run dev
```

Vite serves `http://localhost:5173`. Debug builds of the editor navigate there automatically. If the server is down, reopen after starting Vite (or use the embedded fallback HTML via the resource provider in Release).

## Smoke checklist

1. Configure CMake without errors
2. Build AU + VST3 + Standalone
3. Open Standalone or load in a DAW as an **instrument**
4. Send MIDI — no crash; silence is expected
5. Automate **Master Gain** from the host
6. With `npm run dev`, open the editor and confirm the React placeholder
