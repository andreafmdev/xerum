# Standalone senza chrome — piano di implementazione

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Togliere allo Standalone la barra del titolo e il bottone Options di JUCE, e portare scheda audio, sample rate, buffer e ingressi MIDI dentro la WebUI, dietro l'ingranaggio dell'header.

**Architecture:** La `NSWindow` resta nativa — con la native title bar **accesa** — e viene resa invisibile con `titlebarAppearsTransparent`, `titleVisibility = NSWindowTitleHidden` e `NSWindowStyleMaskFullSizeContentView`: resize, full screen, Mission Control, snap e semaforo continuano a funzionare senza codice nostro. L'app Standalone diventa nostra (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`) ma riusa `juce::StandalonePluginHolder` invariato. Le impostazioni audio arrivano alla UI con un `AudioSettingsChannel` costruito sullo stampo esatto di `MidiDeviceChannel`.

**Tech Stack:** JUCE 9.0.2 (`juce_audio_devices`, `juce_audio_utils`, `juce_gui_extra`), C++23, Objective-C++ (primo file `.mm` del progetto), React 19 + TypeScript + Vitest, CMake + generatore Xcode.

**Spec:** `docs/superpowers/specs/2026-09-21-standalone-chrome-design.md`

## Global Constraints

- **Solo macOS.** Windows è fuori scope; il codice della finestra sta in file `.mm` compilati solo su Apple.
- **Non si tocca `MidiDeviceChannel`** (`Source/bridge/MidiDeviceChannel.{h,cpp}`): cambia solo dove i suoi controlli sono disegnati.
- **Niente bus di ingresso audio.** `PluginProcessor.cpp:16` dichiara solo `withOutput ("Output", stereo)`; il pannello non espone device di input.
- **`Source/bridge/*` resta fuori da `XerumTests`.** Solo la funzione pura del payload entra nella suite, come già fa `Source/bridge/MidiDevices.h` con `Tests/MidiDevicesTests.cpp`.
- **In AU/VST3 il canale risponde `standalone: false` con liste vuote**, come `MidiDeviceChannel` risponde `host: true`.
- Ogni commit che tocca `Source/dsp`, `Source/engine` o `Source/parameters` richiede `XerumTests` verde; questo piano non li tocca, ma la suite va comunque eseguita prima di ogni commit C++.
- Comandi di verifica: `cmake --build --preset macos-debug --target XerumTests && ctest --preset macos-debug` per il C++; `cd WebUI && pnpm test` per la UI.

---

### Task 1: (annullato — vedi ruling)

Era uno spike per decidere se `performWindowDragWithEvent:` regga con `[NSApp currentEvent]`
dietro la consegna asincrona di `WKWebView`. Verificarlo richiede di trascinare una finestra
con il mouse e guardarla: non è automatizzabile.

**Al suo posto il Task 7 implementa entrambe le strade con un ripiego automatico a runtime**:
`beginNativeWindowDrag` restituisce `false` quando l'evento corrente non è più il `mousedown`,
e la pagina passa allora ai delta di `mousemove`. Nessun task resta in attesa di una prova
manuale, e il codice regge anche se il comportamento di `WKWebView` cambia con una versione
di macOS.

Non c'è niente da implementare in questo task: passare al Task 2.

---

### Task 2: `AudioSettings.h` — il payload, puro e sotto test

**Files:**
- Create: `Source/bridge/AudioSettings.h`
- Create: `Tests/AudioSettingsTests.cpp`
- Modify: `CMakeLists.txt:157` (elenco sorgenti di `XerumTests`)

**Interfaces:**
- Consumes: niente.
- Produces: `bridge::AudioOutputDevice { juce::String id, name; }`; `bridge::AudioSettings { bool standalone; std::vector<AudioOutputDevice> outputs; juce::String currentOutput; std::vector<double> sampleRates; double currentSampleRate; std::vector<int> bufferSizes; int currentBufferSize; }`; `juce::var bridge::audioSettingsToVar (const AudioSettings&)`. Il Task 3 chiama `audioSettingsToVar`; il Task 4 consuma il JSON che produce.

- [ ] **Step 1: Scrivere il test che fallisce**

Creare `Tests/AudioSettingsTests.cpp`:

```cpp
#include "bridge/AudioSettings.h"

#include <juce_core/juce_core.h>

/** Il contratto JSON del pannello impostazioni (WebUI/src/juce/backend.ts, AudioSettings). */
struct AudioSettingsTests final : public juce::UnitTest
{
    AudioSettingsTests() : juce::UnitTest ("bridge: audio settings payload", "bridge") {}

    void runTest() override
    {
        beginTest ("nel plugin: standalone falso e nessuna lista");
        {
            const auto v = bridge::audioSettingsToVar ({});
            expect (! (bool) v["standalone"]);
            expectEquals (v["outputs"].getArray()->size(), 0);
            expectEquals (v["sampleRates"].getArray()->size(), 0);
            expectEquals (v["bufferSizes"].getArray()->size(), 0);
        }

        beginTest ("nello Standalone: device, liste e valori correnti");
        {
            bridge::AudioSettings s;
            s.standalone = true;
            s.outputs = { { "AppleHDA", "MacBook Pro Speakers" }, { "Scarlett", "Focusrite 2i2" } };
            s.currentOutput = "Scarlett";
            s.sampleRates = { 44100.0, 48000.0 };
            s.currentSampleRate = 48000.0;
            s.bufferSizes = { 64, 128, 256 };
            s.currentBufferSize = 128;

            const auto v = bridge::audioSettingsToVar (s);
            expect ((bool) v["standalone"]);
            expectEquals (v["outputs"].getArray()->size(), 2);
            expectEquals (v["outputs"][1]["name"].toString(), juce::String ("Focusrite 2i2"));
            expectEquals (v["currentOutput"].toString(), juce::String ("Scarlett"));
            expectEquals ((double) v["currentSampleRate"], 48000.0);
            expectEquals ((int) v["currentBufferSize"], 128);
        }

        beginTest ("la latenza viene calcolata dal buffer e dal sample rate");
        {
            bridge::AudioSettings s;
            s.standalone = true;
            s.currentSampleRate = 48000.0;
            s.currentBufferSize = 240;
            // 240 / 48000 = 5 ms esatti
            expectWithinAbsoluteError ((double) bridge::audioSettingsToVar (s)["latencyMs"], 5.0, 1.0e-9);
        }

        beginTest ("sample rate a zero: latenza zero, non infinito");
        {
            // Succede davvero fra la chiusura di un device e l'apertura del successivo.
            bridge::AudioSettings s;
            s.standalone = true;
            s.currentSampleRate = 0.0;
            s.currentBufferSize = 128;
            expectEquals ((double) bridge::audioSettingsToVar (s)["latencyMs"], 0.0);
        }

        beginTest ("il valore corrente si riporta anche se non e' fra i disponibili");
        {
            // Device staccato a caldo: l'AudioDeviceManager tiene il vecchio valore mentre la
            // lista dei disponibili e' gia' quella nuova. La funzione riporta, non corregge.
            bridge::AudioSettings s;
            s.standalone = true;
            s.sampleRates = { 44100.0 };
            s.currentSampleRate = 96000.0;
            expectEquals ((double) bridge::audioSettingsToVar (s)["currentSampleRate"], 96000.0);
        }
    }
};

static AudioSettingsTests audioSettingsTests;
```

- [ ] **Step 2: Aggiungere il test al target**

In `CMakeLists.txt`, dopo la riga `Tests/MidiDevicesTests.cpp` (riga 157):

```cmake
        Tests/AudioSettingsTests.cpp
```

- [ ] **Step 3: Verificare che fallisca**

Run: `cmake --build --preset macos-debug --target XerumTests`
Expected: FAIL in compilazione — `'bridge/AudioSettings.h' file not found`.

- [ ] **Step 4: Scrivere l'header**

Creare `Source/bridge/AudioSettings.h`:

```cpp
#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace bridge
{
/** Un device di uscita, come lo vede la UI. Su CoreAudio `id` e `name` coincidono: JUCE
    identifica i device per nome (AudioIODeviceType::getDeviceNames), e un id separato non
    esiste. I due campi restano distinti perche' il contratto JSON non debba cambiare se un
    giorno si aggiunge un backend che gli id ce li ha. */
struct AudioOutputDevice
{
    juce::String id;
    juce::String name;
};

/** Lo stato del device audio come lo mostra il pannello. Tutto cio' che serve alla UI, niente
    di piu': le liste dipendono dal device aperto e vanno rilette a ogni cambio. */
struct AudioSettings
{
    bool standalone { false };
    std::vector<AudioOutputDevice> outputs;
    juce::String currentOutput;
    std::vector<double> sampleRates;
    double currentSampleRate { 0.0 };
    std::vector<int> bufferSizes;
    int currentBufferSize { 0 };
};

/**
 * Il payload di getAudioSettings() e dell'evento "audioSettingsChanged".
 *
 * `standalone` falso (VST3/AU) significa "li gestisce l'host": le liste sono vuote e la UI
 * mostra una riga di testo, non controlli morti. Funzione libera e header-only perche' sia
 * testabile senza aprire nessun device audio (Tests/AudioSettingsTests.cpp) — stesso motivo,
 * e stessa forma, di bridge/MidiDevices.h.
 *
 * `latencyMs` e' calcolata qui e non passata da fuori: e' l'unica aritmetica del payload, e
 * tenerla dentro la funzione pura e' cio' che la mette sotto test. Con sample rate a zero —
 * che capita davvero fra la chiusura di un device e l'apertura del successivo — vale zero
 * invece di infinito.
 */
inline juce::var audioSettingsToVar (const AudioSettings& s)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("standalone", s.standalone);

    juce::Array<juce::var> outputs;

    for (const auto& d : s.outputs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", d.id);
        o->setProperty ("name", d.name);
        outputs.add (juce::var (o));
    }

    obj->setProperty ("outputs", outputs);
    obj->setProperty ("currentOutput", s.currentOutput);

    juce::Array<juce::var> rates;
    for (const auto r : s.sampleRates) rates.add (r);
    obj->setProperty ("sampleRates", rates);
    obj->setProperty ("currentSampleRate", s.currentSampleRate);

    juce::Array<juce::var> sizes;
    for (const auto b : s.bufferSizes) sizes.add (b);
    obj->setProperty ("bufferSizes", sizes);
    obj->setProperty ("currentBufferSize", s.currentBufferSize);

    obj->setProperty ("latencyMs", s.currentSampleRate > 0.0
                                       ? 1000.0 * (double) s.currentBufferSize / s.currentSampleRate
                                       : 0.0);

    return juce::var (obj);
}
} // namespace bridge
```

- [ ] **Step 5: Verificare che passi**

Run: `cmake --build --preset macos-debug --target XerumTests && ctest --preset macos-debug`
Expected: PASS, e in fondo `ALL TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add Source/bridge/AudioSettings.h Tests/AudioSettingsTests.cpp CMakeLists.txt
git commit -m "Add the audio settings payload, pure and under test"
```

---

### Task 3: `AudioSettingsChannel` — il canale verso la WebView

**Files:**
- Create: `Source/bridge/AudioSettingsChannel.h`, `Source/bridge/AudioSettingsChannel.cpp`
- Modify: `CMakeLists.txt:101` (sorgenti di `Xerum`), `Source/plugin/PluginEditor.h`, `Source/plugin/PluginEditor.cpp`

**Interfaces:**
- Consumes: `bridge::audioSettingsToVar`, `bridge::AudioSettings` (Task 2).
- Produces: native function `getAudioSettings()`, `setAudioOutput(id)`, `setSampleRate(hz)`, `setBufferSize(n)` — le tre setter restituiscono una **stringa**: vuota se è andata, altrimenti il messaggio d'errore. Evento `"audioSettingsChanged"`. Il Task 4 li consuma.

- [ ] **Step 1: Scrivere l'header**

Creare `Source/bridge/AudioSettingsChannel.h`:

```cpp
#pragma once

#include "bridge/AudioSettings.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace bridge
{
/**
 * Il device audio del sistema, per il pannello impostazioni della UI.
 *
 * Native function: getAudioSettings() -> vedi bridge/AudioSettings.h; setAudioOutput(id),
 * setSampleRate(hz), setBufferSize(n), che tornano "" se e' andata e il messaggio d'errore di
 * AudioDeviceManager::setAudioDeviceSetup altrimenti.
 * Evento verso la UI: "audioSettingsChanged", stesso payload di getAudioSettings.
 *
 * Ha senso solo nello Standalone, dove l'audio lo apre l'app (StandalonePluginHolder e il suo
 * AudioDeviceManager, che persiste gia' la scelta). In VST3/AU risponde `standalone: false` con
 * le liste vuote: la UI mostra la scritta, non i controlli. Gemello di MidiDeviceChannel.
 */
class AudioSettingsChannel final : private juce::ChangeListener
{
public:
    explicit AudioSettingsChannel (juce::AudioProcessor& processor);
    ~AudioSettingsChannel() override;

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

    /** La WebView a cui mandare l'evento; nullptr la disattiva. */
    void setWebView (juce::WebBrowserComponent* view) noexcept { webView_ = view; }

private:
    juce::AudioDeviceManager* deviceManager() const noexcept;
    juce::var snapshot() const;
    /** Applica una modifica al setup corrente. Torna "" se e' andata, l'errore altrimenti. */
    juce::String applySetup (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::AudioProcessor& processor_;
    juce::WebBrowserComponent* webView_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsChannel)
};
} // namespace bridge
```

- [ ] **Step 2: Scrivere il .cpp**

Creare `Source/bridge/AudioSettingsChannel.cpp`:

```cpp
#include "bridge/AudioSettingsChannel.h"

#if JucePlugin_Build_Standalone
 // Stesso motivo di MidiDeviceChannel.cpp: `currentInstance` e' inline static nell'header, quindi
 // includerlo nel codice condiviso e' innocuo per gli altri formati.
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace bridge
{
AudioSettingsChannel::AudioSettingsChannel (juce::AudioProcessor& processor)
    : processor_ (processor)
{
    if (auto* dm = deviceManager())
        dm->addChangeListener (this);
}

AudioSettingsChannel::~AudioSettingsChannel()
{
    if (auto* dm = deviceManager())
        dm->removeChangeListener (this);
}

juce::AudioDeviceManager* AudioSettingsChannel::deviceManager() const noexcept
{
   #if JucePlugin_Build_Standalone
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return &holder->deviceManager;
   #endif

    return nullptr;
}

juce::var AudioSettingsChannel::snapshot() const
{
    auto* dm = deviceManager();

    if (dm == nullptr)
        return audioSettingsToVar ({});

    AudioSettings s;
    s.standalone = true;

    if (auto* type = dm->getCurrentDeviceTypeObject())
    {
        type->scanForDevices();

        for (const auto& name : type->getDeviceNames (false))
            s.outputs.push_back ({ name, name });
    }

    const auto setup = dm->getAudioDeviceSetup();
    s.currentOutput = setup.outputDeviceName;

    // Le liste dipendono dal device aperto: senza device restano vuote, e la UI mostra solo il
    // selettore dei device. Non si inventano valori di ripiego.
    if (auto* device = dm->getCurrentAudioDevice())
    {
        for (const auto r : device->getAvailableSampleRates()) s.sampleRates.push_back (r);
        for (const auto b : device->getAvailableBufferSizes()) s.bufferSizes.push_back (b);
        s.currentSampleRate = device->getCurrentSampleRate();
        s.currentBufferSize = device->getCurrentBufferSizeSamples();
    }

    return audioSettingsToVar (s);
}

juce::String AudioSettingsChannel::applySetup (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)> change)
{
    auto* dm = deviceManager();

    if (dm == nullptr)
        return "Le impostazioni audio le gestisce l'host.";

    auto setup = dm->getAudioDeviceSetup();
    change (setup);

    // `true` = trattalo come scelta dell'utente: e' cio' che lo fa finire nelle impostazioni
    // salvate invece di restare una preferenza volatile.
    const auto error = dm->setAudioDeviceSetup (setup, true);

   #if JucePlugin_Build_Standalone
    if (error.isEmpty())
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->saveAudioDeviceState();
   #endif

    return error;
}

void AudioSettingsChannel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (webView_ != nullptr)
        webView_->emitEventIfBrowserIsVisible ("audioSettingsChanged", snapshot());
}

juce::WebBrowserComponent::Options AudioSettingsChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("getAudioSettings",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 done (snapshot());
                             })
        .withNativeFunction ("setAudioOutput",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.isEmpty())
                                     return done (juce::var (juce::String ("Nessun device indicato.")));

                                 const auto name = args[0].toString();
                                 done (juce::var (applySetup ([&name] (auto& setup) { setup.outputDeviceName = name; })));
                             })
        .withNativeFunction ("setSampleRate",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.isEmpty())
                                     return done (juce::var (juce::String ("Nessun sample rate indicato.")));

                                 const auto rate = (double) args[0];
                                 done (juce::var (applySetup ([rate] (auto& setup) { setup.sampleRate = rate; })));
                             })
        .withNativeFunction ("setBufferSize",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (args.isEmpty())
                                     return done (juce::var (juce::String ("Nessun buffer size indicato.")));

                                 const auto size = (int) args[0];
                                 done (juce::var (applySetup ([size] (auto& setup) { setup.bufferSize = size; })));
                             });
}
} // namespace bridge
```

- [ ] **Step 3: Aggiungere il sorgente al target**

In `CMakeLists.txt`, dopo `Source/bridge/MidiDeviceChannel.cpp` (riga ~106):

```cmake
        Source/bridge/AudioSettingsChannel.cpp
```

- [ ] **Step 4: Agganciare il canale all'editor**

In `Source/plugin/PluginEditor.h`, dopo l'include di `MidiDeviceChannel.h`:

```cpp
#include "bridge/AudioSettingsChannel.h"
```

e dopo il membro `midiDevices_`:

```cpp
    /** Il device audio del sistema (solo Standalone): anche lui prima di webView_. */
    bridge::AudioSettingsChannel audioSettings_;
```

In `Source/plugin/PluginEditor.cpp`: aggiungere il parametro a `makeWebOptions`

```cpp
                                                  bridge::MidiDeviceChannel& midiDevices,
                                                  bridge::AudioSettingsChannel& audioSettings)
```

e in fondo al corpo, prima di `return options;`:

```cpp
    // Il device audio: getAudioSettings / setAudioOutput / setSampleRate / setBufferSize
    // (solo Standalone).
    options = audioSettings.applyTo (options);
```

Nella lista di inizializzazione del costruttore, **dopo `midiDevices_ (p),`**:

```cpp
      audioSettings_ (p),
```

(l'ordine conta: i membri si costruiscono nell'ordine di dichiarazione, e `webView_` deve venire dopo tutti i canali), aggiornare la chiamata:

```cpp
      webView_ (makeWebOptions (relays_, stateChannel_, midiChannel_, midiDevices_, audioSettings_)),
```

nel corpo del costruttore dopo `midiDevices_.setWebView (&webView_);`:

```cpp
    audioSettings_.setWebView (&webView_);
```

e nel distruttore dopo `midiDevices_.setWebView (nullptr);`:

```cpp
    audioSettings_.setWebView (nullptr);
```

- [ ] **Step 5: Verificare che compili e che lo Standalone parta**

Run: `cmake --build --preset macos-debug --target Xerum_Standalone`
Expected: `BUILD SUCCEEDED`.

Run: `scripts/dev.sh`, poi nella console della WebView (o da un test manuale) verificare che `getAudioSettings` risponda con `standalone: true` e la lista dei device.

Nota: non c'è test automatico per questo task — `Source/bridge/*` non è compilato in `XerumTests`, e la parte testabile è già coperta dal Task 2.

- [ ] **Step 6: Commit**

```bash
git add Source/bridge/AudioSettingsChannel.h Source/bridge/AudioSettingsChannel.cpp CMakeLists.txt Source/plugin/PluginEditor.h Source/plugin/PluginEditor.cpp
git commit -m "Bring the audio device settings to the WebView"
```

---

### Task 4: il backend TypeScript e l'hook

**Files:**
- Modify: `WebUI/src/juce/backend.ts` (interfaccia + tipi), `WebUI/src/juce/juce-backend.ts:137`, `WebUI/src/juce/fake-backend.ts:78`, `WebUI/src/juce/hooks.ts`
- Test: `WebUI/src/juce/hooks.test.tsx`

**Interfaces:**
- Consumes: le native function del Task 3.
- Produces: `type AudioSettings`, `Backend.audioSettings()`, `Backend.setAudioOutput(id): Promise<string>`, `Backend.setSampleRate(hz): Promise<string>`, `Backend.setBufferSize(n): Promise<string>`, `Backend.onAudioSettingsChanged(cb)`, e l'hook `useAudioSettings()` che restituisce `{ settings, error, setOutput, setSampleRate, setBufferSize }`. Il Task 5 li consuma.

- [ ] **Step 1: Scrivere il test che fallisce**

In `WebUI/src/juce/hooks.test.tsx`, aggiungere in fondo:

```tsx
describe("useAudioSettings", () => {
  it("parte da standalone falso e prende la fotografia dal backend", async () => {
    const backend = new FakeBackend({
      audioSettings: {
        standalone: true,
        outputs: [{ id: "Scarlett", name: "Focusrite 2i2" }],
        currentOutput: "Scarlett",
        sampleRates: [44100, 48000], currentSampleRate: 48000,
        bufferSizes: [64, 128], currentBufferSize: 128,
        latencyMs: 2.6666666666666665,
      },
    });
    const { result } = renderHook(() => useAudioSettings(), { wrapper: wrapperFor(backend) });
    await waitFor(() => expect(result.current.settings.standalone).toBe(true));
    expect(result.current.settings.outputs[0]!.name).toBe("Focusrite 2i2");
  });

  it("tiene l'errore che la setter restituisce invece di ingoiarlo", async () => {
    const backend = new FakeBackend({ audioSettings: { standalone: true, outputs: [], currentOutput: "",
      sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 } });
    backend.failNextAudioChange("Il device non si apre a 96 kHz");
    const { result } = renderHook(() => useAudioSettings(), { wrapper: wrapperFor(backend) });
    await act(async () => { await result.current.setSampleRate(96000); });
    expect(result.current.error).toBe("Il device non si apre a 96 kHz");
  });

  it("un cambio dal sistema aggiorna la fotografia", async () => {
    const backend = new FakeBackend({ audioSettings: { standalone: true, outputs: [], currentOutput: "",
      sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 } });
    const { result } = renderHook(() => useAudioSettings(), { wrapper: wrapperFor(backend) });
    await waitFor(() => expect(result.current.settings.standalone).toBe(true));
    act(() => backend.emitAudioSettingsChanged({ standalone: true, outputs: [], currentOutput: "MacBook",
      sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 }));
    await waitFor(() => expect(result.current.settings.currentOutput).toBe("MacBook"));
  });
});
```

Se `wrapperFor` non esiste già nel file, usare lo stesso helper con cui i test esistenti costruiscono il provider — leggere l'inizio di `hooks.test.tsx` e riusarlo, senza introdurne un secondo.

- [ ] **Step 2: Verificare che fallisca**

Run: `cd WebUI && pnpm vitest run src/juce/hooks.test.tsx`
Expected: FAIL — `useAudioSettings is not exported` / `failNextAudioChange is not a function`.

- [ ] **Step 3: Estendere il contratto in `backend.ts`**

Dopo `export type MidiInputs = ...` (riga 97):

```ts
export type AudioOutputDevice = { id: string; name: string };
/** Lo stato del device audio. `standalone` falso (VST3/AU): lo gestisce l'host, liste vuote. */
export type AudioSettings = {
  standalone: boolean;
  outputs: AudioOutputDevice[];
  currentOutput: string;
  sampleRates: number[];
  currentSampleRate: number;
  bufferSizes: number[];
  currentBufferSize: number;
  latencyMs: number;
};
```

Nell'interfaccia `Backend`, dopo `onMidiInputsChanged`:

```ts
  /** Il device audio del sistema. `standalone` falso (VST3/AU): lo gestisce l'host. */
  audioSettings(): Promise<AudioSettings>;
  /** Le tre setter tornano "" se è andata, altrimenti il messaggio d'errore da mostrare. */
  setAudioOutput(id: string): Promise<string>;
  setSampleRate(hz: number): Promise<string>;
  setBufferSize(samples: number): Promise<string>;
  onAudioSettingsChanged(cb: (s: AudioSettings) => void): () => void;
```

- [ ] **Step 4: Implementare nei due backend**

In `WebUI/src/juce/juce-backend.ts`, accanto a `onMidiInputsChanged` (riga 111):

```ts
  const onAudioSettingsChanged = fanOut<AudioSettings>("audioSettingsChanged");
```

e nell'oggetto restituito, dopo `onMidiInputsChanged,` (riga 139):

```ts
    audioSettings: () => call("getAudioSettings")() as Promise<AudioSettings>,
    setAudioOutput: (id: string) => call("setAudioOutput")(id) as Promise<string>,
    setSampleRate: (hz: number) => call("setSampleRate")(hz) as Promise<string>,
    setBufferSize: (samples: number) => call("setBufferSize")(samples) as Promise<string>,
    onAudioSettingsChanged,
```

Aggiungere `AudioSettings` all'import dei tipi in cima al file.

In `WebUI/src/juce/fake-backend.ts`, nel costruttore (riga 43) aggiungere l'opzione `audioSettings?: AudioSettings` e:

```ts
    if (opts.audioSettings) this.audio = structuredClone(opts.audioSettings);
```

con i membri:

```ts
  /** Nel browser non c'è nessun device: `standalone` falso è anche la verità. */
  private audio: AudioSettings = { standalone: false, outputs: [], currentOutput: "",
    sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0 };
  private audioSubs = new Set<(s: AudioSettings) => void>();
  private nextAudioError = "";
```

e i metodi, accanto a quelli MIDI (riga 80):

```ts
  async audioSettings() { return structuredClone(this.audio); }
  async setAudioOutput(id: string) { return this.applyAudio(() => { this.audio.currentOutput = id; }); }
  async setSampleRate(hz: number) { return this.applyAudio(() => { this.audio.currentSampleRate = hz; }); }
  async setBufferSize(samples: number) { return this.applyAudio(() => { this.audio.currentBufferSize = samples; }); }
  onAudioSettingsChanged(cb: (s: AudioSettings) => void) { this.audioSubs.add(cb); return () => { this.audioSubs.delete(cb); }; }
  /** Per i test: la prossima modifica fallisce con questo messaggio. */
  failNextAudioChange(message: string) { this.nextAudioError = message; }
  /** Per i test: le impostazioni cambiano "dal sistema". */
  emitAudioSettingsChanged(s: AudioSettings) { this.audio = structuredClone(s); for (const cb of this.audioSubs) cb(structuredClone(s)); }

  private applyAudio(change: () => void): string {
    if (this.nextAudioError) { const e = this.nextAudioError; this.nextAudioError = ""; return e; }
    change();
    this.emitAudioSettingsChanged(this.audio);
    return "";
  }
```

- [ ] **Step 5: Scrivere l'hook**

In `WebUI/src/juce/hooks.ts`, dopo `useMidiInputs`:

```ts
/**
 * Le impostazioni del device audio e il modo di cambiarle. `standalone` falso finche' il
 * backend non risponde e nel plugin: il pannello mostra la riga "li gestisce l'host".
 *
 * `error` tiene il messaggio che la setter ha restituito: un device che non si apre — sample
 * rate non supportato, scheda staccata — deve dirlo, non restare in silenzio con la vecchia
 * impostazione ancora attiva. Si azzera al cambio riuscito successivo.
 */
export function useAudioSettings() {
  const backend = useBackend();
  const [settings, setSettings] = useState<AudioSettings>({
    standalone: false, outputs: [], currentOutput: "",
    sampleRates: [], currentSampleRate: 0, bufferSizes: [], currentBufferSize: 0, latencyMs: 0,
  });
  const [error, setError] = useState("");
  useEffect(() => {
    let alive = true;
    backend.audioSettings().then((s) => { if (alive) setSettings(s); },
      (e) => console.warn("[bridge] getAudioSettings fallita", e));
    const off = backend.onAudioSettingsChanged((s) => setSettings(s));
    return () => { alive = false; off(); };
  }, [backend]);

  const run = useCallback(async (p: Promise<string>) => { setError(await p); }, []);
  return {
    settings,
    error,
    setOutput: useCallback((id: string) => run(backend.setAudioOutput(id)), [backend, run]),
    setSampleRate: useCallback((hz: number) => run(backend.setSampleRate(hz)), [backend, run]),
    setBufferSize: useCallback((n: number) => run(backend.setBufferSize(n)), [backend, run]),
  };
}
```

Aggiungere `AudioSettings` all'import dei tipi in cima al file.

- [ ] **Step 6: Verificare che passi**

Run: `cd WebUI && pnpm test`
Expected: tutti verdi, incluse le suite esistenti.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/juce/
git commit -m "Expose the audio settings through the backend contract"
```

---

### Task 5: `SettingsOverlay` — il pannello, e il MIDI che trasloca

**Files:**
- Create: `WebUI/src/synth/ui/SettingsOverlay.tsx`, `WebUI/src/synth/ui/SettingsOverlay.test.tsx`
- Modify: `WebUI/src/synth/useSynth.ts`, `WebUI/src/synth/ui/SynthWindow.tsx:207`, `WebUI/src/synth/ui/Header.tsx:54`, `WebUI/src/synth/ui/PerformanceBar.tsx:70-86`
- Test: `WebUI/src/synth/ui/PerformanceBar.test.tsx` (aggiornare le asserzioni sul selettore che se ne va)

**Interfaces:**
- Consumes: `useAudioSettings` (Task 4), `useMidiInputs` (esistente, invariato).
- Produces: `<SettingsOverlay onClose={() => void} />`; `useSynth` restituisce in più `settings: boolean` e `setSettings: (v: boolean) => void`.

- [ ] **Step 1: Scrivere il test che fallisce**

Creare `WebUI/src/synth/ui/SettingsOverlay.test.tsx`:

```tsx
import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { SettingsOverlay } from "./SettingsOverlay";
import { FakeBackend } from "../../juce/fake-backend";
import { BackendProvider } from "../../juce/provider";

const standalone = {
  standalone: true,
  outputs: [{ id: "Scarlett", name: "Focusrite 2i2" }, { id: "MacBook", name: "MacBook Pro Speakers" }],
  currentOutput: "Scarlett",
  sampleRates: [44100, 48000], currentSampleRate: 48000,
  bufferSizes: [64, 128, 256], currentBufferSize: 128,
  latencyMs: 2.67,
};

const renderWith = (backend: FakeBackend) =>
  render(
    <BackendProvider backend={backend}>
      <SettingsOverlay onClose={() => {}} />
    </BackendProvider>,
  );

describe("SettingsOverlay", () => {
  it("nello Standalone mostra i controlli del device audio", async () => {
    renderWith(new FakeBackend({ audioSettings: standalone }));
    expect(await screen.findByLabelText("Audio output")).toBeInTheDocument();
    expect(screen.getByLabelText("Sample rate")).toBeInTheDocument();
    expect(screen.getByLabelText("Buffer size")).toBeInTheDocument();
  });

  it("nel plugin mostra una riga di testo invece dei controlli", async () => {
    renderWith(new FakeBackend());
    expect(await screen.findByTestId("audio-host")).toBeInTheDocument();
    expect(screen.queryByLabelText("Audio output")).not.toBeInTheDocument();
  });

  it("mostra l'errore che la setter restituisce", async () => {
    const backend = new FakeBackend({ audioSettings: standalone });
    backend.failNextAudioChange("Il device non si apre a 44.1 kHz");
    renderWith(backend);
    await userEvent.selectOptions(await screen.findByLabelText("Sample rate"), "44100");
    expect(await screen.findByRole("alert")).toHaveTextContent("Il device non si apre a 44.1 kHz");
  });

  it("porta dentro anche il selettore MIDI", async () => {
    renderWith(new FakeBackend({ audioSettings: standalone,
      midiInputs: { host: false, devices: [{ id: "k1", name: "Keystation", enabled: true }] } }));
    expect(await screen.findByLabelText("MIDI input")).toBeInTheDocument();
  });
});
```

- [ ] **Step 2: Verificare che fallisca**

Run: `cd WebUI && pnpm vitest run src/synth/ui/SettingsOverlay.test.tsx`
Expected: FAIL — `Failed to resolve import "./SettingsOverlay"`.

- [ ] **Step 3: Scrivere il pannello**

Creare `WebUI/src/synth/ui/SettingsOverlay.tsx`. Leggere prima `PresetOverlay.tsx` e riusarne il guscio (posizionamento, sfondo, bottone di chiusura con `aria-label`, `aria-label` sul contenitore): questo pannello è il suo fratello minore, non un secondo linguaggio visivo.

```tsx
import { Button, Select } from "@xerum/ui";
import { X } from "lucide-react";
import { useAudioSettings, useMidiInputs } from "../../juce/hooks";

/**
 * Il pannello impostazioni: uscita audio, MIDI, e l'errore dell'ultimo cambio.
 *
 * Nel plugin (`standalone` falso) i controlli audio lasciano il posto a una riga di testo: le
 * impostazioni le tiene l'host, e mostrare selettori che non fanno nulla sarebbe peggio che
 * non mostrarli.
 */
export function SettingsOverlay({ onClose }: { onClose: () => void }) {
  const { settings, error, setOutput, setSampleRate, setBufferSize } = useAudioSettings();
  const { inputs, select } = useMidiInputs();

  return (
    <div aria-label="Settings" className="absolute inset-0 z-20 flex flex-col gap-4 rounded-chassis bg-background/95 p-5 backdrop-blur">
      <header className="flex items-center justify-between">
        <h2 className="font-mono text-xs tracking-wider uppercase">Impostazioni</h2>
        <Button variant="secondary" size="icon-xs" aria-label="Close settings" onClick={onClose}><X /></Button>
      </header>

      <section className="flex flex-col gap-2">
        <h3 className="text-2xs tracking-wider text-text-dim uppercase">Uscita audio</h3>
        {settings.standalone ? (
          <>
            <Select label="Audio output" value={settings.currentOutput} onChange={(v) => void setOutput(v)}
              options={settings.outputs.map((d) => ({ value: d.id, label: d.name }))} />
            <Select label="Sample rate" value={String(settings.currentSampleRate)}
              onChange={(v) => void setSampleRate(Number(v))}
              options={settings.sampleRates.map((r) => ({ value: String(r), label: `${r} Hz` }))} />
            <Select label="Buffer size" value={String(settings.currentBufferSize)}
              onChange={(v) => void setBufferSize(Number(v))}
              options={settings.bufferSizes.map((b) => ({ value: String(b), label: `${b} campioni` }))} />
            <p className="text-2xs text-text-dim">Latenza {settings.latencyMs.toFixed(1)} ms</p>
          </>
        ) : (
          <p data-testid="audio-host" className="text-2xs text-text-dim">Le gestisce l'host.</p>
        )}
      </section>

      <section className="flex flex-col gap-2">
        <h3 className="text-2xs tracking-wider text-text-dim uppercase">MIDI</h3>
        {inputs.host ? (
          <p data-testid="midi-host" className="text-2xs text-text-dim">Il MIDI arriva dall'host.</p>
        ) : (
          <Select label="MIDI input" onChange={(v) => void select(v)}
            value={inputs.devices.filter((d) => d.enabled).length === 1
              ? inputs.devices.find((d) => d.enabled)!.id : "all"}
            options={[{ value: "all", label: "Tutti gli ingressi" },
                      ...inputs.devices.map((d) => ({ value: d.id, label: d.name }))]} />
        )}
      </section>

      {error && <p role="alert" className="text-2xs text-destructive">{error}</p>}
    </div>
  );
}
```

Se `Select` in `@xerum/ui` ha una firma diversa da quella usata qui, adeguarsi alla firma vera (leggerla in `PerformanceBar.tsx:80`, che lo usa già) invece di cambiare il componente.

- [ ] **Step 4: Aprirlo dall'ingranaggio**

In `WebUI/src/synth/useSynth.ts`, accanto a `browse`:

```ts
  const [settings, setSettings] = useState(false);
```

e aggiungerli al valore restituito: `settings, setSettings`.

In `WebUI/src/synth/ui/Header.tsx`, la prop nuova:

```tsx
type Props = { /* … le esistenti … */ onSettings: () => void };
```

e il bottone (riga 54):

```tsx
      <Button variant="secondary" size="icon-xs" aria-label="Settings" className={iconBtn} onClick={onSettings}><Settings /></Button>
```

In `WebUI/src/synth/ui/SynthWindow.tsx`, passare `onSettings={() => s.setSettings(true)}` all'`<Header>` e montare il pannello accanto a `PresetOverlay` (riga 207):

```tsx
            {s.settings && <SettingsOverlay onClose={() => s.setSettings(false)} />}
```

- [ ] **Step 5: Togliere il selettore MIDI dalla barra**

In `WebUI/src/synth/ui/PerformanceBar.tsx`, il componente `MidiSource` (righe 70-86) si riduce alla sola spia di attività: cancellare `MidiSource` e la sua riga `<MidiSource />`, lasciando `<MidiActivity />`. Cancellare l'import di `Select` e di `useMidiInputs` se non più usati.

Aggiornare `PerformanceBar.test.tsx`: i test che cercano `midi-source` o il `Select` vanno rimossi, **non** adattati a cercare altrove — il componente non ha più quella responsabilità.

- [ ] **Step 6: Verificare che passi tutto**

Run: `cd WebUI && pnpm test && pnpm typecheck`
Expected: tutti verdi.

- [ ] **Step 7: Commit**

```bash
git add WebUI/src/synth/
git commit -m "Put audio and MIDI settings behind the gear button"
```

---

### Task 6: l'app Standalone nostra e la finestra senza chrome

**Files:**
- Create: `Source/standalone/XerumStandaloneApp.cpp`, `Source/standalone/XerumWindowMac.h`, `Source/standalone/XerumWindowMac.mm`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: niente dai task precedenti.
- Produces: `namespace xerum { void makeWindowChromeless (void* nsViewHandle); double trafficLightWidth (void* nsViewHandle); bool beginNativeWindowDrag (void* nsViewHandle); void toggleWindowZoom (void* nsViewHandle); }` — il Task 7 usa le ultime tre.

- [ ] **Step 1: Scrivere l'header del ponte macOS**

Creare `Source/standalone/XerumWindowMac.h`:

```cpp
#pragma once

/**
 * Il poco di macOS che serve alla finestra dello Standalone. L'implementazione sta in
 * XerumWindowMac.mm — primo file Objective-C++ del progetto — e non compila altrove.
 *
 * `nsViewHandle` e' sempre cio' che ComponentPeer::getNativeHandle() restituisce su macOS:
 * un NSView*, da cui si risale alla finestra con [view window].
 */
namespace xerum
{
/** Barra del titolo trasparente e contenuto a tutta altezza: la finestra resta nativa —
    semaforo, full screen, Mission Control e snap continuano a funzionare — ma la barra
    sparisce alla vista e la UI arriva al bordo superiore. */
void makeWindowChromeless (void* nsViewHandle);

/** Quanto spazio occupa il semaforo, in px di finestra, misurato sui bottoni veri e non
    scritto a mano: la larghezza esatta cambia con la versione di macOS. */
double trafficLightWidth (void* nsViewHandle);

/** Avvia il trascinamento nativo a partire dall'evento corrente. Falso se l'evento corrente
    non e' piu' il mousedown: vedi il Task 7 per cosa fare in quel caso. */
bool beginNativeWindowDrag (void* nsViewHandle);

/** Quello che fa il doppio clic sulla barra del titolo. */
void toggleWindowZoom (void* nsViewHandle);
} // namespace xerum
```

- [ ] **Step 2: Scrivere l'implementazione Objective-C++**

Creare `Source/standalone/XerumWindowMac.mm`:

```objc
#include "standalone/XerumWindowMac.h"

#import <Cocoa/Cocoa.h>

namespace
{
NSWindow* windowOf (void* nsViewHandle)
{
    if (nsViewHandle == nullptr)
        return nil;

    return [(__bridge NSView*) nsViewHandle window];
}
} // namespace

namespace xerum
{
void makeWindowChromeless (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);

    if (window == nil)
        return;

    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

double trafficLightWidth (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);

    if (window == nil)
        return 0.0;

    NSButton* zoom = [window standardWindowButton: NSWindowZoomButton];

    if (zoom == nil)
        return 0.0;

    // Il bordo destro del bottone piu' a destra, piu' lo stesso margine che i tre hanno a
    // sinistra: e' lo spazio che la UI non deve occupare.
    NSButton* close = [window standardWindowButton: NSWindowCloseButton];
    const CGFloat leftMargin = close != nil ? NSMinX (close.frame) : 0.0;

    return (double) (NSMaxX (zoom.frame) + leftMargin);
}

bool beginNativeWindowDrag (void* nsViewHandle)
{
    NSWindow* window = windowOf (nsViewHandle);
    NSEvent* event = [NSApp currentEvent];

    if (window == nil || event == nil || event.type != NSEventTypeLeftMouseDown)
        return false;

    [window performWindowDragWithEvent: event];
    return true;
}

void toggleWindowZoom (void* nsViewHandle)
{
    [windowOf (nsViewHandle) zoom: nil];
}
} // namespace xerum
```

- [ ] **Step 3: Scrivere l'app**

Creare `Source/standalone/XerumStandaloneApp.cpp`:

```cpp
/**
 * L'app dello Standalone, al posto di quella che genera JUCE.
 *
 * Perche' esiste: juce::StandaloneFilterWindow e' una DocumentWindow con barra del titolo e
 * bottone Options (juce_StandaloneFilterWindow.h:653-666), e Options e' esattamente cio' che
 * questa feature porta dentro la WebUI. Qui si tiene tutto il resto — StandalonePluginHolder
 * invariato, che e' il pezzo che apre l'audio, apre il MIDI e salva le impostazioni — e si
 * cambia solo la finestra.
 *
 * Il prezzo: questo file sostituisce codice che mantiene JUCE. Aggiornando il submodule, se
 * StandalonePluginHolder cambia se ne accorge il compilatore, non un test.
 */
#include "standalone/XerumWindowMac.h"

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace xerum
{
/** La finestra: una DocumentWindow con la native title bar ACCESA — e' lei a portare semaforo,
    full screen e snap — resa invisibile da makeWindowChromeless. Spegnerla significherebbe
    perderli e doverli riscrivere. */
class StandaloneWindow final : public juce::DocumentWindow
{
public:
    StandaloneWindow (const juce::String& title, juce::PropertySet* settings)
        : DocumentWindow (title, juce::Colour (0xff0e1016), DocumentWindow::allButtons),
          settings_ (settings)
    {
        setUsingNativeTitleBar (true);

        // `false` = NON prendere possesso delle settings: le possiede ApplicationProperties, e
        // lasciargliele cancellare sarebbe una doppia distruzione. Il default del costruttore e'
        // `true` (juce_StandaloneFilterWindow.h:79), quindi va scritto a mano.
        holder_ = std::make_unique<juce::StandalonePluginHolder> (settings, false);

        // createEditorIfNeeded() sta sull'AudioProcessor, non sull'holder: e' da li' che lo
        // prende anche JUCE (juce_StandaloneFilterWindow.h:855).
        auto* editor = holder_->processor->createEditorIfNeeded();
        setContentOwned (editor, true);

        // Il vincolo altezza/larghezza vive sull'editor (ChassisConstrainer in PluginEditor.cpp).
        // JUCE lo traduce per la finestra con un DecoratorConstrainer perche' la sua finestra ha
        // una barra del titolo alta; qui il contenuto riempie la finestra intera — native title
        // bar piu' fullSizeContentView — quindi lo stesso constrainer si applica diretto.
        setResizable (true, false);
        setConstrainer (editor->getConstrainer());

        if (settings_ != nullptr)
            restoreWindowStateFromString (settings_->getValue ("windowState"));
        else
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
        xerum::makeWindowChromeless (getPeer() != nullptr ? getPeer()->getNativeHandle() : nullptr);
    }

    ~StandaloneWindow() override
    {
        if (settings_ != nullptr)
            settings_->setValue ("windowState", getWindowStateAsString());

        clearContentComponent();
        holder_ = nullptr;
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplicationBase::quit();
    }

private:
    juce::PropertySet* settings_;
    std::unique_ptr<juce::StandalonePluginHolder> holder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneWindow)
};

class StandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";

        properties_.setStorageParameters (options);
        window_ = std::make_unique<StandaloneWindow> (JucePlugin_Name, properties_.getUserSettings());
    }

    void shutdown() override
    {
        window_ = nullptr;
        properties_.saveIfNeeded();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties properties_;
    std::unique_ptr<StandaloneWindow> window_;
};
} // namespace xerum

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new xerum::StandaloneApp(); }
```

- [ ] **Step 4: Dirlo a CMake**

In `CMakeLists.txt`, dopo il blocco `target_sources(Xerum PRIVATE ...)`:

```cmake
# L'app dello Standalone e' nostra: JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP spegne la sua e chiede
# un juce_CreateApplication(). I sorgenti vanno sul target di formato, non su Xerum: e' quello a
# compilare il wrapper dello Standalone.
if(TARGET Xerum_Standalone AND APPLE)
    target_sources(Xerum_Standalone
        PRIVATE
            Source/standalone/XerumStandaloneApp.cpp
            Source/standalone/XerumWindowMac.mm)
    target_compile_definitions(Xerum_Standalone PRIVATE JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
    target_link_libraries(Xerum_Standalone PRIVATE "-framework Cocoa")
endif()
```

- [ ] **Step 5: Costruire e guardare**

Run: `cmake --preset macos-debug && cmake --build --preset macos-debug --target Xerum_Standalone`
Expected: `BUILD SUCCEEDED`.

Run: `scripts/dev.sh`

Guardare la finestra e verificare, una per una: **niente barra del titolo visibile**; **il semaforo c'è** e i tre bottoni funzionano; il ridimensionamento dal bordo funziona e rispetta ancora il vincolo altezza/larghezza; il full screen (`⌃⌘F`) funziona. Il trascinamento **non** funziona ancora: è il Task 7.

- [ ] **Step 6: Commit**

```bash
git add Source/standalone/ CMakeLists.txt
git commit -m "Own the standalone app and drop its native chrome"
```

---

### Task 7: trascinamento, doppio clic e lo spazio per il semaforo

**Files:**
- Create: `Source/bridge/WindowChannel.h`, `Source/bridge/WindowChannel.cpp`
- Modify: `CMakeLists.txt`, `Source/plugin/PluginEditor.h`, `Source/plugin/PluginEditor.cpp`, `WebUI/src/juce/backend.ts`, `WebUI/src/juce/juce-backend.ts`, `WebUI/src/juce/fake-backend.ts`, `WebUI/src/synth/ui/Header.tsx`, `WebUI/src/synth/ui/SynthWindow.tsx`
- Test: `WebUI/src/synth/ui/Header.test.tsx`

**Interfaces:**
- Consumes: `xerum::beginNativeWindowDrag`, `xerum::toggleWindowZoom`, `xerum::trafficLightWidth` (Task 6); l'esito del Task 1.
- Produces: `Backend.beginWindowDrag(): Promise<boolean>` (vero = il trascinamento nativo è partito), `Backend.moveWindowBy(dx: number, dy: number): Promise<void>`, `Backend.toggleWindowZoom(): Promise<void>`, `Backend.windowChrome(): Promise<{ trafficLightWidth: number }>`.

**Due strade, scelte a runtime.** `beginWindowDrag` prova il trascinamento nativo e torna `true`
se è partito. Se torna `false` — l'evento corrente non è più il `mousedown`, perché `WKWebView`
consegna i messaggi della bridge in modo asincrono — la pagina passa al ripiego: segue i
`mousemove` e chiama `moveWindowBy(dx, dy)`, che sposta la finestra con
`window.setBounds (window.getBounds().translated (dx, dy))`.

Il ripiego non è codice morto in attesa di un bug: è la strada che si prende su ogni versione di
macOS dove l'evento non arriva in tempo, e non sappiamo quali siano. Va scritto e testato come
l'altra.

- [ ] **Step 1: Scrivere il test che fallisce**

In `WebUI/src/synth/ui/Header.test.tsx` (crearlo se non esiste, sul modello di `PerformanceBar.test.tsx`):

```tsx
it("il mousedown sull'header trascina la finestra", async () => {
  const backend = new FakeBackend();
  renderHeader(backend);
  await userEvent.pointer({ target: screen.getByRole("banner"), keys: "[MouseLeft>]" });
  expect(backend.windowDrags).toBe(1);
});

it("il mousedown su un controllo NON trascina la finestra", async () => {
  // Senza questa esclusione la finestra si sposterebbe mentre giri una manopola: l'errore
  // che renderebbe la UI inusabile.
  const backend = new FakeBackend();
  renderHeader(backend);
  await userEvent.pointer({ target: screen.getByLabelText("Settings"), keys: "[MouseLeft>]" });
  expect(backend.windowDrags).toBe(0);
});

it("lo spazio per il semaforo c'è solo in Standalone ed è diviso per lo scale", () => {
  // Il semaforo lo disegna macOS in coordinate di finestra; l'header vive dentro lo chassis
  // scalato. A scala 1.5 un padding di 78 px CSS ne occuperebbe 117 sulla finestra.
  const { rerender } = renderHeader(new FakeBackend(), { chrome: { trafficLightWidth: 78 }, scale: 1.5 });
  expect(screen.getByRole("banner")).toHaveStyle({ paddingLeft: "52px" });
  rerender(headerWith(new FakeBackend(), { chrome: { trafficLightWidth: 0 }, scale: 1.5 }));
  expect(screen.getByRole("banner")).toHaveStyle({ paddingLeft: "0px" });
});
```

Con, in cima al file:

```tsx
import { describe, expect, it } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { Header } from "./Header";
import { PRESETS } from "../presets";
import { FakeBackend } from "../../juce/fake-backend";
import { BackendProvider } from "../../juce/provider";

type Extra = { chrome?: { trafficLightWidth: number }; scale?: number };

const headerWith = (backend: FakeBackend, extra: Extra = {}) => (
  <BackendProvider backend={backend}>
    <Header
      preset={PRESETS[0]!}
      dirty={false}
      onPrev={() => {}}
      onNext={() => {}}
      onBrowse={() => {}}
      onSettings={() => {}}
      scale={extra.scale ?? 1}
      trafficLightWidth={extra.chrome?.trafficLightWidth ?? 0}
    />
  </BackendProvider>
);

const renderHeader = (backend: FakeBackend, extra: Extra = {}) => render(headerWith(backend, extra));
```

`<Header>` deve quindi rendere un elemento `<header>` (che è già, e che dà il ruolo ARIA `banner` senza attributi in più).

- [ ] **Step 2: Verificare che fallisca**

Run: `cd WebUI && pnpm vitest run src/synth/ui/Header.test.tsx`
Expected: FAIL — `backend.windowDrags is undefined`.

- [ ] **Step 3: Aggiungere le tre native function**

`WindowChannel::applyTo` (nel `.cpp`, includendo `"standalone/XerumWindowMac.h"` **solo su macOS**):

```cpp
        .withNativeFunction ("getWindowChrome",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 auto* obj = new juce::DynamicObject();
                                 obj->setProperty ("trafficLightWidth", nativeViewHandle() != nullptr
                                                                            ? xerum::trafficLightWidth (nativeViewHandle())
                                                                            : 0.0);
                                 done (juce::var (obj));
                             })
        .withNativeFunction ("beginWindowDrag",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 done (juce::var (nativeViewHandle() != nullptr
                                                  && xerum::beginNativeWindowDrag (nativeViewHandle())));
                             })
        .withNativeFunction ("toggleWindowZoom",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 if (nativeViewHandle() != nullptr)
                                     xerum::toggleWindowZoom (nativeViewHandle());

                                 done (juce::var());
                             });
```

con il membro privato:

```cpp
    /** L'NSView della WebView, da cui XerumWindowMac risale alla finestra. Nullo finche' la
        WebView non e' sul desktop, e in tutti i formati che non sono lo Standalone. */
    void* nativeViewHandle() const noexcept;
```

e nel `.cpp`:

```cpp
void* WindowChannel::nativeViewHandle() const noexcept
{
   #if JUCE_MAC
    if (webView_ != nullptr)
        if (auto* peer = webView_->getPeer())
            return peer->getNativeHandle();
   #endif

    return nullptr;
}
```

**Queste tre non vanno in `AudioSettingsChannel`**: sono funzioni di finestra, non di audio, e il nome di un file deve dire cosa c'è dentro. Vanno in `Source/bridge/WindowChannel.{h,cpp}`, nuovo, con lo stesso stampo (`applyTo`, `setWebView`, nessun `ChangeListener` perché non c'è niente da ascoltare). Aggiungere `Source/bridge/WindowChannel.cpp` a `target_sources(Xerum PRIVATE ...)` e il membro `bridge::WindowChannel window_;` a `PluginEditor`, dichiarato **prima** di `webView_` come tutti gli altri canali, con `window_.setWebView (&webView_)` nel costruttore e `nullptr` nel distruttore.

- [ ] **Step 4: Estendere il backend TS**

In `backend.ts` (interfaccia `Backend`):

```ts
  /** Solo Standalone macOS. Da chiamare sul mousedown della fascia di trascinamento. */
  beginWindowDrag(): Promise<void>;
  toggleWindowZoom(): Promise<void>;
  /** Quanto spazio lasciare libero per il semaforo, in px di finestra. 0 fuori dallo Standalone. */
  windowChrome(): Promise<{ trafficLightWidth: number }>;
```

In `juce-backend.ts`:

```ts
    async beginWindowDrag() { await call("beginWindowDrag")(); },
    async toggleWindowZoom() { await call("toggleWindowZoom")(); },
    windowChrome: () => call("getWindowChrome")() as Promise<{ trafficLightWidth: number }>,
```

In `fake-backend.ts`:

```ts
  /** Per i test: quante volte la UI ha chiesto di trascinare la finestra. */
  windowDrags = 0;
  async beginWindowDrag() { this.windowDrags++; }
  async toggleWindowZoom() {}
  async windowChrome() { return { trafficLightWidth: 0 }; }
```

- [ ] **Step 5: Agganciare l'header**

In `Header.tsx`, aggiungere le props `scale: number` e `trafficLightWidth: number`, e sull'`<header>`:

```tsx
    <header
      className="flex h-10 shrink-0 items-center gap-2.5 px-1"
      style={{ paddingLeft: trafficLightWidth > 0 ? `${Math.round(trafficLightWidth / scale)}px` : undefined }}
      onMouseDown={(e) => {
        // Senza questa esclusione la finestra si sposterebbe mentre giri una manopola.
        if ((e.target as HTMLElement).closest("button, input, select, [role='slider']")) return;
        void backend.beginWindowDrag();
      }}
      onDoubleClick={(e) => {
        if ((e.target as HTMLElement).closest("button, input, select, [role='slider']")) return;
        void backend.toggleWindowZoom();
      }}
    >
```

In `SynthWindow.tsx`, leggere `windowChrome()` una volta al mount (`useEffect` + `useState`, con 0 come valore iniziale) e passare `trafficLightWidth` e lo `sc` già calcolato per `WaveDisplay`.

- [ ] **Step 6: Verificare che passi**

Run: `cd WebUI && pnpm test && pnpm typecheck`
Expected: verdi.

Run: `cmake --build --preset macos-debug --target Xerum_Standalone && scripts/dev.sh`

Poi, a mano: trascinare dall'header — la finestra segue; trascinare da una manopola — la manopola gira e la finestra sta ferma; doppio clic sull'header — la finestra si ingrandisce; il semaforo non copre né il logo né i bottoni.

- [ ] **Step 7: Commit**

```bash
git add Source/bridge/ WebUI/src/
git commit -m "Drag, zoom and traffic-light room from the web UI"
```

---

### Task 8: documentazione

**Files:**
- Modify: `docs/architecture.md`, `docs/build.md`

**Interfaces:**
- Consumes: tutto quanto sopra.
- Produces: niente codice.

- [ ] **Step 1: Documentare l'architettura**

In `docs/architecture.md`, sezione nuova `### Lo Standalone e la sua finestra`, che dica: perché l'app è nostra (`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP`) e cosa resta di JUCE (`StandalonePluginHolder` invariato); che la native title bar è **accesa** e resa invisibile, con l'elenco dei tre attributi `NSWindow` e il motivo (semaforo, full screen, snap gratis); che il trascinamento passa dal bridge perché la `WKWebView` si prende i mouse event, e quale delle due strade è stata presa (esito del Task 1); che lo spazio del semaforo si misura sui bottoni veri e si divide per lo scale, con il perché.

Aggiungere anche il costo: aggiornando JUCE, un cambiamento in `StandalonePluginHolder` si manifesta come errore di compilazione, non come test rosso.

- [ ] **Step 2: Estendere la checklist manuale**

In `docs/build.md`, sezione "Bridge checklist", aggiungere le voci:

```
7. Lo Standalone si apre senza barra del titolo, ma il semaforo c'è e i tre bottoni funzionano.
8. L'header trascina la finestra; una manopola no. Doppio clic sull'header: la finestra si ingrandisce.
9. Il ridimensionamento rispetta ancora il vincolo altezza/larghezza del constrainer.
10. Cambio di device e di buffer size mentre una nota suona: nessun crash, nessuna nota appesa.
11. Interfaccia audio staccata a caldo: il pannello si aggiorna da solo.
12. Riaperta l'app, device audio e ingresso MIDI scelti sono quelli di prima.
```

- [ ] **Step 3: Commit**

```bash
git add docs/
git commit -m "Document the chromeless standalone window"
```
