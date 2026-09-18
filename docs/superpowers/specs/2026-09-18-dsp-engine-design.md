# Spec — Motore DSP: wavetable, envelope, filtro, preset

Data: 2026-09-18
Stato: design approvato a sezioni in chat, in attesa di review del documento
Fase roadmap: 2 e 3 (`docs/architecture.md`)

## 1. Scopo

Il plugin oggi non emette suono. UI, bridge, 48 parametri APVTS, tastiera e allocazione voci funzionano; il DSP è fatto di stub:

- `dsp::WavetableOscillator::getSample()` → `return 0.0f`
- `dsp::ADSREnvelope::getNextSample()` → gate secco 0/1, nessuna rampa
- `dsp::StateVariableFilter::processSample()` → bypass
- `util::kEnableTestTone = false`, quindi neanche il seno di prova

Questo lavoro rende il sintetizzatore udibile: oscillatore wavetable con onde reali, inviluppo, filtro, e i parametri della catena minima collegati davvero. In coda, i preset di fabbrica.

**Perimetro deciso in chat**: onde scaricate da AKWF (CC0) e convertite offline, mipmap band-limited costruita all'avvio, catena minima (osc + ADSR + filtro + master), preset di fabbrica compilati nel binario.

**Fuori scope, esplicito**: unison/detune/warp, LFO, mod matrix, effetti (chorus, riverbero), arpeggiatore, `glide`, `voiceMode`, `envCurve`, preset utente su disco, import di `.wav` dell'utente, e l'allineamento del display d'onda della UI alla tavola reale (vedi §9).

## 2. Vincoli di fondo

Valgono le regole già scritte in `Source/util/RealtimeHelpers.h` e `docs/architecture.md`, qui ribadite perché questo è il primo lavoro che le mette davvero alla prova:

- in `processBlock` e in tutto ciò che chiama: nessuna allocazione, nessun lock, nessun I/O, nessun logging;
- niente `libm` per campione. `std::pow`, `std::exp`, `std::tan`, `std::tanh`, `std::log` si calcolano a note-on o a cambio parametro, mai nel loop dei sample;
- i dati condivisi fra thread (tavole) passano per un puntatore atomico;
- ogni parametro si legge **una volta per blocco**, non per campione.

Chi scrive il codice verifica anche che `processBlock` non allochi: in Debug, `juce::ScopedNoDenormals` c'è già; si aggiunge un controllo manuale con un contatore di allocazioni globale durante i test, oppure un'ispezione del codice in review. Non introduciamo dipendenze per questo.

## 3. Vista d'insieme

```
scripts/fetch-wavetables.mjs        (offline, una tantum)
        │  scarica AKWF, ricampiona 600 → 2048, seleziona 64 frame
        ▼
Resources/wavetables/*.xwt          (committati, 512 KB l'uno)
        │  juce_add_binary_data → WavetableAssets
        ▼
dsp::WavetableStore                 (message thread: parse + mipmap, pubblica un puntatore atomico)
        │
        ▼  const MipTable*
dsp::WavetableOscillator  →  dsp::StateVariableFilter  →  dsp::ADSREnvelope  →  pan
        └────────────── engine::SynthVoice ──────────────┘
                             │  16 voci
                             ▼
                    engine::VoiceManager → engine::SynthEngine → volume → buffer
```

## 4. Le onde: da AKWF a `.xwt`

### 4.1 Fonte

Repository `KristofferKarlAxelEkstrand/AKWF-FREE`, licenza **CC0-1.0** (pubblico dominio, nessun obbligo di attribuzione). File a `AKWF/<famiglia>/<nome>.wav`: 600 sample, 16 bit, mono, 44.1 kHz, 1344 byte l'uno. Download da `https://raw.githubusercontent.com/KristofferKarlAxelEkstrand/AKWF-FREE/main/…`.

Anche se CC0 non lo richiede, lo script scrive `Resources/wavetables/CREDITS.md` con fonte, autore, licenza e data di prelievo: costa zero e mette in chiaro la provenienza se un domani il plugin viene distribuito.

### 4.2 Mappatura tavola → famiglia

Le sei voci di `wtIndex` sono già in `Source/parameters/parameters.json` e non cambiano:

| `wtIndex` | etichetta UI | famiglia AKWF | onde disponibili |
|---|---|---|---|
| `basic` | Basic Shapes | `AKWF_bw_perfectwaves` | 4 |
| `saws` | Analog Saws | `AKWF_bw_saw` | 50 |
| `grit` | Digital Grit | `AKWF_bitreduced` | 68 |
| `vocal` | Vocal Formant | `AKWF_hvoice` | 104 |
| `bells` | Glass Bells | `AKWF_fmsynth` | 122 |
| `pwm` | PWM Sweep | `AKWF_bw_squ` | 100 |

Ogni tavola ha **esattamente 64 frame**, perché il display della UI scrive `Frame NN/64` (`WaveDisplay.tsx`) e `wtpos` è mappato `linear 1..64` in `parameters.json`.

- famiglia con ≥ 64 onde: si prendono 64 indici a passo costante;
- famiglia con < 64 onde (solo `basic`, che ne ha 4): le onde sono **ancore** e i frame intermedi si ottengono per interpolazione lineare fra ancore adiacenti, così "Basic Shapes" diventa un morph continuo sin → tri → saw → squ.

### 4.3 Ricampionamento 600 → 2048

Un single-cycle è periodico: il ricampionamento corretto è nel dominio della frequenza. Lo script calcola la DFT dei 600 sample, copia i bin nello spettro di 2048 (zero-padding in alto) e antitrasforma. È interpolazione **esatta** per un segnale periodico band-limited, non un'approssimazione, e non introduce l'aliasing che darebbe un'interpolazione lineare.

600 non è potenza di due, quindi lo script usa una DFT diretta O(N²): 600×300 moltiplicazioni per onda, 64 frame × 6 tavole ≈ 7×10⁷ operazioni in tutto. In Node sono secondi, e lo script gira una volta sola. Nessuna dipendenza esterna (niente `npm install`): solo `node:fs` e `fetch`.

Dopo il ricampionamento, per ogni frame: rimozione della componente continua (media a zero) e normalizzazione del picco a 1.0. Senza, le tavole hanno volumi diversi e il knob `level` non significa niente.

### 4.4 Formato `.xwt`

Little-endian, nessun padding:

| offset | tipo | contenuto |
|---|---|---|
| 0 | char[4] | `XWT1` |
| 4 | uint32 | `frames` (64) |
| 8 | uint32 | `frameSize` (2048) |
| 12 | float32[frames × frameSize] | i campioni, frame dopo frame |

512 KB a tavola, **3,1 MB in tutto**, committati in `Resources/wavetables/`. Il formato è deliberatamente stupido: nessuna compressione, nessun campo opzionale, così il parser C++ è una decina di righe e ogni errore è un controllo di lunghezza.

`CMakeLists.txt` aggiunge un secondo `juce_add_binary_data(WavetableAssets …)`, **sempre linkato** (non dietro `XERUM_EMBED_WEBUI`, che riguarda solo il bundle web in Release).

## 5. `dsp::WavetableStore`

Sostituisce il placeholder vuoto di oggi. Vive sul message thread, pubblica al thread audio.

```cpp
namespace dsp
{
/** Una tavola pronta per l'audio: 64 frame, ognuno con la sua piramide mipmap. */
struct MipTable
{
    static constexpr int kFrames = 64;
    static constexpr int kMaxLevel = 6;          // livello 0 = 2048 sample, livello 6 = 32

    /** Puntatore ai campioni del frame f al livello l, e quanti sono. */
    const float* samples (int frame, int level) const noexcept;
    int size (int level) const noexcept;         // 2048 >> level
};

class WavetableStore
{
public:
    /** Costruisce la tavola se non c'è già. Alloca: solo message thread / prepareToPlay. */
    void build (int wavetableIndex);

    /** Thread audio: puntatore alla tavola pronta, o nullptr se non lo è ancora. */
    const MipTable* active() const noexcept;

    /** Message thread: cambia quale tavola è attiva (la costruisce se serve). */
    void setActive (int wavetableIndex);
};
}
```

**Mipmap vera, non nove copie.** Il livello *k* contiene `2048 >> k` campioni, cioè metà armoniche a ogni salto, ottenuto troncando lo spettro e antitrasformando con `juce::dsp::FFT` (ordine 11 in avanti, ordine `11-k` all'indietro). Memoria per tavola: `2048 × (1 + ½ + ¼ + …) ≈ 4096` float per frame, cioè **~1 MB**, contro i 4,5 MB che costerebbero nove livelli a piena risoluzione. Costruzione: ~640 FFT, **decine di ms**.

**Ciclo di vita.** Le tavole costruite non vengono **mai** distrutte finché vive il plugin. È la scelta che rende sicuro il puntatore atomico: il thread audio può stare leggendo `active()` mentre il message thread ne costruisce un'altra, e nessuno gli toglie la memoria da sotto. Costo massimo: 6 MB se l'utente le prova tutte e sei. In cambio non serve nessuno schema di reclamation (hazard pointer, garbage queue, RCU), che è la parte dove questi bug diventano irriproducibili.

**Quando si costruisce.**

- `prepareToPlay`: costruzione **sincrona** della tavola attualmente selezionata. Lì allocare è lecito e la latenza non conta.
- cambio di `wtIndex`: un `juce::AudioProcessorValueTreeState::Listener` sul message thread chiama `setActive`. Finché la nuova tavola non è pronta, `active()` continua a restituire la precedente: l'audio non si interrompe e non c'è silenzio improvviso.
- se `active()` è `nullptr` (nessuna tavola, caso patologico), l'oscillatore restituisce silenzio invece di dereferenziare.

## 6. La voce

### 6.1 `dsp::WavetableOscillator`

- fase in `double`, incremento `f0 / sampleRate`;
- **livello mipmap** scelto quando cambia la frequenza (note-on): il livello più piccolo *k* tale che `(2048 >> k) / 2 ≤ sampleRate / (2 · f0)`, cioè la tabella più corta che contiene solo armoniche sotto Nyquist. Un `while` su interi, niente `log2`;
- **doppia interpolazione**: lineare fra i due campioni adiacenti *dentro* il frame, e lineare fra i due frame adiacenti a `wtpos`. È la seconda che fa il morph — senza, muovere Position dà scatti invece di una trasformazione continua;
- `wtpos` è smussato (§6.4): muoverlo di scatto produce un salto di forma d'onda udibile come click.

### 6.2 `dsp::ADSREnvelope`

Inviluppo esponenziale stile analogico: ogni stadio ha un coefficiente `coeff` e un target, e `level += coeff · (target − level)`. I coefficienti si ricalcolano **solo** quando cambiano `att`/`dec`/`rel` o a note-on — `std::exp` non entra mai nel loop.

- `att`, `dec`, `rel`: mappa `ms-squared` 1…8001 ms da `parameters.json`;
- `sus`: lineare 0…100 %;
- `envVel`: 0…100 %, quanto la velocity scala il picco (`0 %` = inviluppo sempre a piena ampiezza, `100 %` = proporzionale alla velocity);
- la voce si spegne quando il livello di release scende sotto -80 dB, non quando "finisce" il release: altrimenti resta appesa con code inudibili che rubano voci.

`envCurve` resta fuori dalla prima passata (la forma è quella esponenziale fissa).

### 6.3 `dsp::StateVariableFilter`

SVF topology-preserving (Zavalishin), che resta stabile anche quando il cutoff viene modulato velocemente — proprietà che servirà quando arriverà l'LFO.

- `ftype`: LP / HP / BP presi dalle tre uscite dello stesso stato;
- `slope`: `12` = uno stadio, `24` = due stadi in cascata;
- `cutoff`: mappa `log` 20 Hz…20 kHz; il coefficiente `g = tan(π · fc / sampleRate)` si calcola **una volta per blocco**, non per campione, ed è l'unico `tan` del percorso;
- `res`: 0…100 % → fattore di smorzamento; con `keytrk` e risonanza alta il cutoff va limitato a `0.49 · sampleRate` prima di calcolare `g`, altrimenti il filtro esplode;
- `keytrk`: 0…100 %, sposta il cutoff con la nota suonata, calcolato a note-on;
- `drive`: 0…24 dB, saturazione **polinomiale** (`x − x³/3` con clamp, o una rational approximation) prima del filtro. Niente `std::tanh` per campione.

### 6.4 Smoothing

`juce::SmoothedValue<float, ValueSmoothingTypes::Linear>` su `cutoff`, `wtpos`, `level`, `volume`, `pan`; tempo di rampa 20 ms. Senza, ogni movimento di knob produce zipper noise, e con l'automazione dell'host è peggio.

### 6.5 Catena e somma

Per voce: `osc → drive → filtro → ampiezza (env × velocity × level) → pan`. La somma delle voci va nel buffer, poi `volume` come guadagno master (già implementato oggi, con i +6 dB di headroom).

## 7. Parametri

**La mappatura non si riscrive.** `Source/parameters/ParameterMapping.h::denormalise` esiste già e implementa le stesse formule di `WebUI/src/synth/mapping.ts`; `parameters.json` resta l'unica fonte di verità. Il DSP chiama `denormalise(spec, raw)` e non contiene nessun numero di range.

**Come arrivano al motore.** Il processore tiene i `std::atomic<float>*` di APVTS (come fa già per `volume` e `level`) e una volta per blocco riempie una `struct engine::EngineParams` con i valori denormalizzati. L'engine e le voci leggono quella struct. Nessun `load()` atomico per campione, nessuna denormalizzazione per campione.

**Collegati in questa passata (22):**

`oscOn`, `wtIndex`, `wtpos`, `oct`, `semi`, `fine`, `level`, `filtOn`, `ftype`, `slope`, `cutoff`, `res`, `drive`, `keytrk`, `att`, `dec`, `sus`, `rel`, `envVel`, `volume`, `pan`, `bypass`.

L'intonazione della nota è `midiNote + 12 · oct + semi`, più `fine` in centesimi (−100…+100). Il `std::pow` della conversione nota→Hz resta dov'è: a note-on (`SynthVoice::start`), mai per campione.

`oscOn`, `filtOn` e `bypass` sono interruttori, e vanno definiti perché oggi nessuno li legge: `oscOn = false` azzera l'uscita dell'oscillatore (le voci restano allocate, l'inviluppo scorre); `filtOn = false` salta il filtro lasciando passare il segnale; `bypass = true` produce **silenzio** e spegne tutte le voci — è un sintetizzatore, non c'è ingresso da lasciar passare.

**Non collegati (i knob girano, il suono non cambia):** `envCurve`, `glide`, `voiceMode`, `unison`, `detune`, `warp`, tutti gli `l*` (LFO), `fx1On`/`ch*`, `fx2On`/`rv*`, tutti gli `arp*`. Restano inerti come sono oggi.

`util::kEnableTestTone` e il ramo `if constexpr` in `SynthVoice::render` spariscono: erano l'impalcatura della fase 1 e con un oscillatore vero diventano un ramo morto che può solo confondere.

## 8. Preset

`Source/parameters/presets.json`, accanto a `parameters.json`. Uno schema minimo:

```json
{
  "version": 1,
  "presets": [
    { "name": "Sub Pulse", "cat": "Bass", "values": { "wtIndex": 0.0, "wtpos": 0.32, "cutoff": 0.35, "res": 0.2 } }
  ]
}
```

I valori sono **normalizzati 0..1**, come nell'APVTS; i parametri non elencati restano al default della spec.

`scripts/gen-params.mjs` viene esteso per emettere anche `Source/parameters/PresetTable.h` e `WebUI/src/synth/presets.generated.ts`, con la stessa struttura già collaudata per i parametri (lo script scrive oggi due file, ne scriverà quattro). `presets.ts` mantiene `filterPresets` e `step`, ma la lista di nomi hardcodata viene rimpiazzata da quella generata.

**Caricamento in C++.** Nuova funzione sul `StateChannel`: `loadPreset(index)`. Sul message thread scrive tutti i parametri del preset con `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture` (così l'host registra il cambio e l'undo funziona), poi notifica `stateChanged` perché la UI si riallinei. L'alternativa — preset in TypeScript che pilotano i relay uno per uno — costava meno lavoro ma lascia il plugin senza preset quando la UI non è aperta e chiude la porta a esporli come program all'host.

12 preset che coprono le categorie già mostrate dalla UI (Bass, Lead, Pad, Keys, Pluck, FX). **Si scrivono per ultimi**, quando il motore suona: prima è accordare alla cieca.

## 9. Casi limite e divergenze note

| caso | comportamento |
|---|---|
| `.xwt` corrotto o troncato | il parser rifiuta (magic, dimensioni, lunghezza attesa) e la tavola resta assente: silenzio, nessun crash, nessuna lettura fuori limite |
| tavola non ancora costruita | `active()` restituisce la precedente; se non ce n'è nessuna, l'oscillatore emette silenzio |
| nota molto acuta (oltre il livello 6) | si usa il livello 6; con 16 armoniche a quella frequenza l'aliasing è comunque sotto la soglia udibile |
| `sampleRate` alto (96/192 kHz) | la scelta del livello dipende da `sampleRate`, quindi si adatta da sola; `prepareToPlay` ricalcola tutto |
| risonanza massima + cutoff a 20 kHz a 44.1 kHz | cutoff limitato a `0.49 · sampleRate` prima del calcolo di `g` |
| `wtIndex` cambiato durante una nota | la nota in corso passa alla nuova tavola al prossimo blocco (comportamento voluto, come Serum) |
| display d'onda della UI | **divergenza nota**: `WaveDisplay` disegna una curva procedurale (`curves.ts`) e dopo questo lavoro non mostrerà la tavola reale. Non viene toccato qui; si risolve mandando il frame vero sul bridge, in una fase a sé |

## 10. Test e verifica

Oggi in C++ non esiste nessun test: tutti i test del progetto (88 in `WebUI`, 95 in `@xerum/ui`) sono TypeScript. Il DSP è il posto dove i test ripagano di più, quindi il piano aggiunge un target `juce_add_console_app` che gira `juce::UnitTestRunner`.

| cosa | criterio |
|---|---|
| parser `.xwt` | round-trip su un blob sintetico; blob con magic sbagliato, dimensioni assurde o troncato → rifiutato senza crash |
| ricampionatore (test Node, a sé) | seno a 600 sample → 2048: THD sotto soglia; DC nulla; picco normalizzato |
| costruzione mipmap | il livello *k* non contiene energia oltre `(2048>>k)/2` armoniche |
| scelta del livello | saw a La5 renderizzata: nessun bin FFT sopra Nyquist oltre il rumore di fondo |
| ADSR | il livello arriva a 1 entro `att` ms; il release scende sotto -80 dB e la voce si libera; sustain esatto |
| SVF | -3 dB al cutoff per LP 12 dB; stabile a risonanza massima su tutto il range di cutoff; nessun NaN |
| sweep parametri | tutti i parametri agli estremi, 10 s di rendering: nessun NaN, nessun inf, picco sotto 0 dBFS |
| voci | 16 note simultanee + note stealing: nessun click, nessuna voce appesa |

A mano, dopo i test: standalone (`scripts/dev.sh`) e poi la checklist DAW di `docs/build.md` — automazione dall'host, salvataggio e ricarica del progetto, cambio preset.

## 11. File toccati (mappa)

**Nuovi**

- `scripts/fetch-wavetables.mjs` — download, selezione frame, ricampionamento DFT, scrittura `.xwt` + `CREDITS.md`
- `Resources/wavetables/*.xwt` (6) + `CREDITS.md`
- `Source/dsp/MipTable.h` — la tavola pronta per l'audio
- `Source/parameters/presets.json` e `Source/parameters/PresetTable.h` (generato)
- `WebUI/src/synth/presets.generated.ts` (generato)
- target di test C++ + i suoi file

**Modificati**

- `Source/dsp/WavetableStore.h/.cpp` — da placeholder a implementazione
- `Source/dsp/WavetableOscillator.h/.cpp` — interpolazione, livelli, morph
- `Source/dsp/ADSREnvelope.h/.cpp` — inviluppo vero
- `Source/dsp/StateVariableFilter.h/.cpp` — SVF TPT
- `Source/engine/SynthVoice.h/.cpp` — catena, intonazione, fine della strada per `kEnableTestTone`
- `Source/engine/SynthEngine.h/.cpp`, `VoiceManager` — `EngineParams`, distribuzione ai voice
- `Source/plugin/PluginProcessor.h/.cpp` — puntatori atomici, `EngineParams` per blocco, listener su `wtIndex`, `WavetableStore` in `prepareToPlay`
- `Source/bridge/StateChannel.h/.cpp` — `loadPreset`
- `Source/util/RealtimeHelpers.h` — via il test tone
- `scripts/gen-params.mjs` — genera anche i preset
- `WebUI/src/synth/presets.ts` e `ui/PresetOverlay.tsx` — lista generata, chiamata `loadPreset`
- `CMakeLists.txt` — `WavetableAssets`, nuovi `.cpp`, target di test
- `docs/architecture.md`, `docs/build.md` — fasi 2 e 3 fatte, come rigenerare le tavole

## 12. Ordine di lavoro consigliato

1. script + `.xwt` committati (verificabile da solo: i file esistono e il test Node passa)
2. `WavetableStore` + `MipTable` + parser, con i test
3. `WavetableOscillator` — **qui il plugin emette il primo suono**
4. `ADSREnvelope`
5. `StateVariableFilter`
6. `EngineParams` e collegamento dei 22 parametri
7. target di test C++ completo e sweep
8. preset (per ultimi, a motore funzionante)
