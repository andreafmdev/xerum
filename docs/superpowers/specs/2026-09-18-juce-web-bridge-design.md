# Spec — Bridge JUCE ↔ Web UI (WebRelay)

Data: 2026-09-18
Stato: design approvato a sezioni in chat, in attesa di review del documento
Fase roadmap: 4 (`docs/architecture.md`)

## 1. Scopo

Collegare la finestra React (`WebUI/src/synth/`) al plugin JUCE in modo che:

1. ogni controllo della UI muova un parametro APVTS reale (automazione, save/recall, undo host);
2. lo stato non parametrico della UI (mod matrix, step dell'arpeggiatore) venga salvato col progetto del DAW;
3. la UI riceva dati dal motore audio (meter, livello LFO, step arp) senza mai toccare il thread audio;
4. il plugin Release funzioni senza dev server, con il bundle web incorporato nel binario.

Perimetro deciso in chat: **tutti i ~45 parametri** della UI entrano subito nell'APVTS (il DSP consuma quelli che sa usare oggi, gli altri sono cablati ma inerti); trasporto **ibrido** (relay nativi JUCE per i parametri, protocollo custom per stato e meter); feedback meter e stato persistente inclusi; embed Release incluso.

Fuori scope (esplicito): preset browser su file, pulsanti Undo/Redo dell'header, DSP che consuma i nuovi parametri, `WavetableStore`. I knob muovono l'APVTS; il suono resta silenzio finché il DSP non li legge.

## 2. Vincolo di fondo: il thread audio non conosce il bridge

Latenza audio = buffer dell'host + latenza interna del DSP. La UI non sta in quel percorso. Il bridge vive interamente sul message thread; il thread audio tocca solo `std::atomic`:

- parametri: `apvts.getRawParameterValue(id)->load()` per blocco (già così oggi);
- meter: `store(std::memory_order_relaxed)` di pochi float per blocco.

Vietato nel processor: riferimenti a `WebBrowserComponent`, relay, `ValueTree::Listener`, `String`, allocazioni, lock. Verifica in code review (grep) e misura CPU di `processBlock` con editor aperto e chiuso: deve coincidere.

La latenza di *controllo* (knob → suono) è ~1 frame di rendering (1–16 ms), in-process, senza socket: stesso ordine di un `juce::Slider`. Automazione host e MIDI (inclusa la tastiera nativa sotto la WebView) non passano dalla UI web.

## 3. Vista d'insieme

```
                 message thread (UI)                          audio thread
 ┌──────────────────────────────────────────────┐   ┌───────────────────────────┐
 │ WebView (React)                              │   │ processBlock              │
 │   useParam(id) ──┐                           │   │   legge atomics APVTS     │
 │   useBridgeState │        ┌────────────────┐ │   │   scrive MeterFrame       │
 │   useMeters      │  JS↔C++│ Source/bridge  │ │   │   (atomics, no lock)      │
 └──────────────────┼────────┤  · WebRelays   │ │   └───────────┬───────────────┘
                    │        │  · StateChannel│ │               │ atomics
                    │        │  · MeterChannel│◄┼───Timer 30Hz──┘
                    │        │  · WebAssets   │ │
                    │        └───────┬────────┘ │
                    │                │ APVTS (parametri + ValueTree MODS/ARP)
                    │                ▼
                    │        Host: automazione, save/recall, undo
```

Tre canali, tre pattern:

| Canale | Meccanismo | Direzione | Pattern |
|---|---|---|---|
| Parametri | `WebSliderRelay` / `WebToggleButtonRelay` / `WebComboBoxRelay` + `Web*ParameterAttachment` | bidirezionale | valore normalizzato + gesture |
| Stato | native functions `getState`/`setMods`/`setArpSteps` + evento `stateChanged` | bidirezionale | request/response + notifica |
| Meter | atomics nel processor → `juce::Timer` 30 Hz → evento `meters` | audio → UI | stream periodico |

## 4. Contratto dei parametri: una sola fonte di verità

### 4.1 Il problema

45 parametri × (id, tipo, range, default, unità, label) in due linguaggi divergono se scritti due volte.

### 4.2 `Source/parameters/parameters.json`

Unico file sorgente. Uno script Node (`scripts/gen-params.mjs`, Node è già richiesto per la WebUI) genera due file **committati**:

- `Source/parameters/ParameterTable.h` — array `constexpr` di `ParamSpec`;
- `WebUI/src/synth/params.generated.ts` — tipo `ParamId`, `PARAM_SPECS`, `PARAM_DEFAULTS`.

La build C++ non dipende da Node: legge un header committato. Un test Vitest rigenera in memoria e confronta con il file su disco: se il JSON cambia senza rigenerare, il test fallisce.

### 4.3 Schema di una voce

```json
{
  "id": "cutoff",
  "name": "Cutoff",
  "group": "filter",
  "kind": "float",
  "map": { "type": "log", "min": 20, "max": 20000 },
  "default": 0.62,
  "unit": "Hz",
  "decimals": 0
}
```

| Campo | Valori | Note |
|---|---|---|
| `id` | snake/camel case stabile | ID APVTS e ID relay, mai rinominato dopo il primo release (compatibilità preset) |
| `kind` | `float` \| `int` \| `bool` \| `choice` | decide parametro APVTS e tipo di relay |
| `map.type` | `linear` \| `log` \| `db` \| `ms-squared` | formula di denormalizzazione, identica in C++ e TS |
| `map.min/max` | numeri | estremi in unità reali |
| `default` | float: 0..1 normalizzato; int: valore; choice: indice; bool: true/false | |
| `unit` | stringa | suffisso label |
| `decimals` | intero | cifre nella label |
| `labelKind` | opzionale: `pan` \| `signed-cents` \| `note-division` \| … | etichette non derivabili da `map` (es. "C / 50 L") |
| `options` | solo `choice`: `[{ "value": "LP", "label": "LP" }]` | ordine = indice APVTS |

Formule (`v` normalizzato):

- `linear`: `min + v·(max−min)`
- `log`: `min·(max/min)^v` (es. 20·1000^v → 20 Hz … 20 kHz)
- `db`: `v ≤ 0 → −∞`, altrimenti `20·log10(v) + offset` (`offset` in `map`, default 0)
- `ms-squared`: `min + v²·(max−min)` (tempi di inviluppo e glide)

Lato C++ i parametri `float` conservano nell'APVTS il valore **normalizzato 0..1** (`NormalisableRange<float>{0,1}` lineare): la libreria JS di JUCE calcola `getNormalisedValue()` solo da `start/end/skew` del relay, quindi una range con `convertFrom0To1` custom non sarebbe riprodotta lato web. Le unità reali compaiono nel testo per l'host (`withStringFromValueFunction` → `params::formatValue`) e nel DSP, che denormalizza con le stesse formule. `int` usa `AudioParameterInt` (il relay espone `start/end/interval=1`), `choice` `AudioParameterChoice`, `bool` `AudioParameterBool`. Lato TS `denormalise(spec, v)` e `formatValue(spec, v)` producono le label. Test TS con valori noti per ogni tipo (20 Hz, 632 Hz, 20 kHz; −6.0 dB; 1 ms, 8.00 s…).

### 4.4 Migrazione dal codice attuale

- `params.ts` (`DEFAULTS`, `LABELS`, tipi) → sostituito dal generato. Le stringhe "Sezione · Parametro" della mod matrix derivano da `group` + `name`.
- `format.ts` → resta solo per le `labelKind` speciali; il resto passa da `formatValue(spec)`.
- Parametri discreti oggi in `SynthParams` (`unison` 0..3, `oct` −3..3, `semi` −12..12, `slope` 12/24, `wtIndex`) diventano `int` o `choice`.
- I due parametri esistenti (`master_gain` in dB, `osc1_level`) vengono assorbiti: `volume` (`db`, offset +6) e `level` (`db`). Il processor legge i nuovi ID.
- `bypass` (oggi stato locale dell'header) diventa un parametro `bool`: l'host può automatizzarlo e il DSP lo leggerà quando esisterà.

## 5. Lato C++: `Source/bridge/`

Quattro unità, una responsabilità ciascuna. Vivono nell'**editor** (tranne `MeterFrame`), quindi esistono solo mentre la finestra è aperta.

### 5.1 `WebRelays`

```cpp
class WebRelays
{
public:
    explicit WebRelays (const ParameterTable&);          // un relay per voce, tipo da kind
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options) const; // withOptionsFrom(...) per ogni relay
    void attach (juce::AudioProcessorValueTreeState&, juce::UndoManager* = nullptr);      // crea gli attachment
private:
    std::vector<std::unique_ptr<juce::WebSliderRelay>>        sliders;   // float, int
    std::vector<std::unique_ptr<juce::WebToggleButtonRelay>>  toggles;   // bool
    std::vector<std::unique_ptr<juce::WebComboBoxRelay>>      combos;    // choice
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> ...; // + toggle/combo
};
```

Ordine imposto da JUCE: **relay → Options → WebView → attachment**. `applyTo` prima di costruire la WebView, `attach` subito dopo. Il nome del relay è l'`id` del parametro. Header JUCE: `juce_gui_extra/misc/juce_WebControlRelays.h`, `juce_audio_processors/utilities/juce_ParameterAttachments.h`.

### 5.2 `StateChannel`

Stato non parametrico nello `ValueTree` dell'APVTS (`apvts.state`), così viaggia gratis in `getStateInformation`/`setStateInformation`:

```
PARAMS
 ├─ PARAM id=… value=…        (gestiti dall'APVTS)
 ├─ MODS  version=1
 │   └─ MOD src="lfo" target="cutoff" depth=0.25
 └─ ARP   steps="0.8,0,0.6,…"
```

Native functions (registrate con `Options::withNativeFunction`):

| Nome | Argomenti | Ritorna |
|---|---|---|
| `getState` | — | `{ version: 1, mods: [{src,target,depth}], arpSteps: number[16] }` |
| `setMods` | `mods` (array intero) | ok |
| `setArpSteps` | `steps` (array intero) | ok |

Scrittura sempre dell'array intero: piccolo, idempotente, niente diff. Le scritture passano dall'`UndoManager` dell'APVTS se presente.

`StateChannel` è `ValueTree::Listener` su `MODS`/`ARP`: quando il tree cambia per cause esterne (recall, undo, altro editor) emette `stateChanged` con lo stesso JSON di `getState`. Ogni scrittura porta un `origin` (stringa casuale della sessione UI) rimbalzato nell'evento: la UI ignora gli echi delle proprie scritture.

Il processor non legge `MODS` in questa fase; la fase ModulationMatrix lo farà in `setStateInformation` con handoff lock-free.

### 5.3 `MeterChannel`

Parte audio: `Source/engine/MeterFrame.h` (solo atomics, nessuna dipendenza da JUCE gui), posseduto dal processor:

```cpp
struct MeterFrame
{
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, lfo { 0 };
    std::atomic<int>   arpStep { 0 };
};
```

`processBlock`: calcola il picco del buffer (già sul buffer che ha in mano, costo trascurabile) e fa `store(relaxed)`. Con inPeak = picco pre-master, outPeak = post. `lfo` e `arpStep` restano 0 finché non esistono nel DSP (fase 5+): la UI li mostra a zero, non finti.

Parte UI, `Source/bridge/MeterChannel`: `juce::Timer` a 30 Hz che legge, azzera i picchi (`exchange(0)`) e chiama `webView.emitEventIfBrowserIsVisible ("meters", { in, out, lfo, arpStep })`. Editor chiuso → timer distrutto → costo zero. Il decadimento visivo è lato UI.

### 5.4 `WebAssets`

Resource provider per Release: `juce_add_binary_data (WebUIAssets SOURCES <WebUI/dist/**>)`. Il provider mappa l'URL richiesto sul nome della risorsa (Vite emette `assets/index-<hash>.js`; il nome binario è derivato dal path), con mime per estensione e fallback su `index.html` per `/`.

CMake:

- opzione `XERUM_EMBED_WEBUI` (default ON in Release, OFF in Debug);
- se ON e `WebUI/dist/index.html` manca → `message(FATAL_ERROR)` con il comando da lanciare;
- target custom `webui` che esegue `pnpm --dir WebUI build` se `pnpm` è nel PATH (comodo, non obbligatorio).

Debug continua a caricare Vite (`XERUM_WEBUI_URL` per la porta).

### 5.5 `PluginEditor` e `PluginProcessor`

Editor = composizione: costruisce `WebRelays`, monta le `Options` (native integration, relay, native functions di `StateChannel`, provider), costruisce la WebView, chiama `attach`, avvia `MeterChannel`. Nessuna logica propria.

Processor: acquisisce `MeterFrame` e un accessor `getMeters()`; legge i parametri dalla tabella (loop) invece dei due puntatori scritti a mano; `ParameterLayout` costruisce il layout in loop dalla `ParameterTable`.

## 6. Lato web: `WebUI/src/juce/`

### 6.1 `Backend`: l'unico punto che conosce `window.__JUCE__`

```ts
export interface ParamHandle {
  get(): number;                       // normalizzato 0..1
  set(v: number): void;
  begin(): void;                       // gesture
  end(): void;
  subscribe(cb: () => void): () => void;
}

export interface Backend {
  param(id: ParamId): ParamHandle;
  call<T>(fn: string, ...args: unknown[]): Promise<T>;   // getState, setMods, setArpSteps
  on(event: string, cb: (payload: unknown) => void): () => void;  // stateChanged, meters
}
```

Due implementazioni:

- **`JuceBackend`** — wrappa `juce-framework-frontend` (copia di `external/JUCE/modules/juce_gui_extra/native/javascript/`, versionata con JUCE): `getSliderState(id)` (`setNormalisedValue`, `getNormalisedValue`, `sliderDragStarted/Ended`, `valueChangedEvent.addListener`), `getToggleState` (`getValue/setValue`), `getComboBoxState` (`getChoiceIndex/setChoiceIndex`), `getNativeFunction(name)`, `window.__JUCE__.backend.addEventListener`. Il `ParamHandle` normalizza le tre famiglie (toggle → 0/1, choice → indice/(n−1)) così la UI vede sempre 0..1.
- **`FakeBackend`** — in memoria: mappa id → valore, `getState` da un oggetto locale, eventi emessi a mano. Opzione `demo: true` accende un clock finto (LFO, meter, arp) per browser e Storybook; nei test è spento. Sostituisce il vecchio `animate`.

Selezione in `main.tsx`: `window.__JUCE__?.backend ? new JuceBackend() : new FakeBackend({ demo: true })`, iniettata con `<BridgeProvider backend>` (React context).

### 6.2 Hook

- `useParam(id)` → `{ value, set, begin, end, spec, label }`. `useSyncExternalStore` sul `ParamHandle`: re-render del solo controllo. Durante una gesture (tra `begin` e `end`) gli aggiornamenti in arrivo dal backend sullo stesso id sono ignorati (evita jitter da eco dell'host).
- `useBridgeState()` → `{ mods, arpSteps, setMods, setArpSteps }`: `getState` al mount, `stateChanged` in ascolto, filtro per `origin`.
- `useMeters()` → ultimo frame `{ in, out, lfo, arpStep }`, con decadimento (peak hold) calcolato nell'hook.

### 6.3 Impatto sul codice esistente

- `useSynth` perde `p/set/mods/setMods` locali: diventa composizione di `useBridgeState` + stato puramente UI (tab, preset corrente, overlay, bypass locale finché non è un parametro).
- `ParamKnob` usa `useParam(id)`; `useDragValue` espone già `dragging` → `begin/end` sul fronte del cambio.
- `Segmented`/`Stepper`/`Toggle` nei pannelli: valore da `useParam`, conversione indice↔normalizzato dentro il handle.
- `SynthWindow` perde `animate` e lo stato locale `bypass` (ora `useParam("bypass")`); `sources` (livelli sorgente per i knob modulati) viene da `useMeters` (LFO reale quando esisterà, zero ora) e dalle costanti `vel`/`mw`.
- `WaveDisplay`, curve, `PresetOverlay`: invariati.

## 7. Flussi

**Knob → host.** pointerdown → `begin()` → `sliderDragStarted` → `beginChangeGesture` (l'host apre il blocco undo/automazione) → drag → `set(v)` → `setNormalisedValue` → attachment → `setValueNotifyingHost` → pointerup → `end()`. Il DSP vede il valore al blocco successivo. La UI aggiorna il knob localmente subito, senza attendere l'eco.

**Host → UI.** Automazione/recall → APVTS → attachment → relay `valueChanged` → handle → `useSyncExternalStore` → re-render del knob. Ignorato durante una gesture locale sullo stesso id.

**Stato.** Mount → `getState()` → store. Drop chip / slider depth / step arp → `setMods`/`setArpSteps` (array intero, con `origin`). Recall/undo → `stateChanged` → store se `origin` diverso.

**Meter.** Ogni blocco: `store` dei picchi. Timer 30 Hz: `exchange(0)`, un evento `meters`. UI: peak hold e decadimento nell'hook; il componente `Meter` riceve un livello.

## 8. Errori e casi limite

| Caso | Comportamento |
|---|---|
| `window.__JUCE__` assente (browser, test) | `FakeBackend`, un `console.info`, mai crash |
| `getState` malformato o `version` sconosciuta | stato default + `console.warn`; il tree non viene sovrascritto finché la UI non scrive |
| `id` presente nel generato ma non nell'APVTS (plugin vecchio, UI nuova) | handle orfano: valore default, `set` no-op, `console.warn` una volta |
| Editor chiuso / finestra nascosta | timer distrutto con l'editor; `emitEventIfBrowserIsVisible` copre la finestra nascosta |
| Release senza `WebUI/dist` | errore CMake a configure con il comando da lanciare |
| Eco della propria scrittura di stato | filtrato da `origin` |

## 9. Test e verifica

**TS unit** (Vitest, app shell):
- `denormalise`/`formatValue` per ogni `map.type` con valori noti; `labelKind` speciali;
- freshness dei file generati rispetto a `parameters.json`;
- `FakeBackend`: set/get/subscribe/gesture; conversioni toggle/choice ↔ 0..1;
- store stato: `origin`, `version`, payload malformato.

**TS integrazione**: `SynthWindow` dentro `BridgeProvider(FakeBackend)`: drag knob → il backend riceve `begin/set/end` nell'ordine giusto; `stateChanged` finto → UI aggiornata; `meters` finto → `Meter` si muove. Sostituiscono gli smoke test attuali basati su stato locale.

**C++**: build AU/VST3/Standalone in Debug e Release. Checklist manuale in `docs/build.md`: automazione di `cutoff` dal DAW muove il knob; il knob scrive automazione; save/reload del progetto ripristina mod matrix e arp; Release mostra la UI senza Vite; nessun `lock`, `new`, `String`, riferimento al bridge nel processor (grep).

**Performance**: CPU di `processBlock` con editor aperto e chiuso uguale (misura nel DAW).

## 10. File toccati (mappa)

```
Source/parameters/parameters.json          nuovo — fonte di verità
Source/parameters/ParameterTable.h         generato, committato
Source/parameters/ParameterLayout.cpp      loop sulla tabella
Source/parameters/ParameterIDs.h           rimosso (gli ID stanno nella tabella)
Source/bridge/WebRelays.{h,cpp}            nuovo
Source/bridge/StateChannel.{h,cpp}         nuovo
Source/engine/MeterFrame.h                 nuovo (atomics scritti dal thread audio)
Source/bridge/MeterChannel.{h,cpp}         nuovo (timer 30 Hz → evento meters)
Source/bridge/WebAssets.{h,cpp}            nuovo
Source/plugin/PluginEditor.{h,cpp}         composizione
Source/plugin/PluginProcessor.{h,cpp}      MeterFrame, lettura parametri in loop
CMakeLists.txt                             XERUM_EMBED_WEBUI, juce_add_binary_data, target webui
scripts/gen-params.mjs                     nuovo
WebUI/src/synth/params.generated.ts        generato, committato
WebUI/src/synth/params.ts, format.ts       ridotti (labelKind) / rimossi
WebUI/src/juce/{backend,juce-backend,fake-backend,provider,hooks}.ts(x)   nuovi
WebUI/src/juce/juce-framework-frontend/    copia dal submodule JUCE
WebUI/src/synth/useSynth.ts, ui/*.tsx      adattati agli hook
docs/architecture.md, docs/build.md        aggiornati
```
