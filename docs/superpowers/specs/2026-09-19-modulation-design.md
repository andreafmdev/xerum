# Modulazione: cablare il mod matrix nel motore audio

Data: 2026-09-19
Stato: proposta, in attesa di approvazione

## Perché

Il synth oggi ha un solo inviluppo, sull'ampiezza, e un cutoff che resta fermo per tutta la
durata della nota. Una nota suona identica dal primo all'ultimo campione, a meno che l'utente
non muova un knob a mano. È la ragione strutturale per cui lo strumento non suona come un
synth: nessuna quantità di ritaratura del guadagno o di pulizia delle wavetable la risolve.

Manca la modulazione. E la modulazione, in questo progetto, **è già stata progettata e
costruita ovunque tranne che nel motore audio**.

## Il contratto che esiste già

Non va inventato niente: va implementato il lato DSP di un contratto completo e versionato.

| Pezzo | Dove | Cosa fa già |
|---|---|---|
| Stato persistito | `Source/parameters/StateTree.h` | Nodo `MODS` con figli `MOD { src, target, depth }`, `kVersion = 1`, `ensureChildren`, `setMods`, `toVar` |
| Bridge host↔UI | `Source/bridge/StateChannel.{h,cpp}` | Native function `getState()` e `setMods(json, origin)`, evento `stateChanged` |
| Tipi del bridge | `WebUI/src/juce/backend.ts` | `ModSource = "lfo" \| "env" \| "vel" \| "mw"`, `ModAssignment = { src, target: ParamId, depth: number }` |
| Matematica della UI | `WebUI/src/synth/mod.ts` | `lfoShape()` con 5 forme, `liveValue()`, `modsFor()` |
| Interfaccia utente | `WebUI/src/synth/ui/ModChip.tsx`, `Tabs.tsx` | Drag di LFO/ENV/VEL/MW su qualunque knob, ModTab con depth bipolare e rimozione |
| Telemetria | `Source/engine/MeterFrame.h:12` | `std::atomic<float> lfo { 0.0f }; // -1..1, zero finché l'LFO non esiste nel DSP` |

Quel commento in `MeterFrame.h` dice esplicitamente che questo lavoro era previsto e rimandato.

### La formula che il DSP deve riprodurre

`WebUI/src/synth/mod.ts`:

```ts
export function liveValue(value: number, mods: ModAssignment[], sources: SourceLevels): number {
  return clamp01(mods.reduce((acc, m) => acc + m.depth * sources[m.src], value));
}
```

Tre proprietà da rispettare alla lettera, altrimenti l'anello del knob nella UI e il suono
raccontano due storie diverse:

1. La somma avviene sul valore **normalizzato 0..1**, non sul valore reale. La
   denormalizzazione viene dopo.
2. Il risultato è **clampato a 0..1** prima della denormalizzazione.
3. I livelli delle sorgenti sono: `lfo` bipolare **−1..1**, `env`/`vel`/`mw` unipolari **0..1**
   (commento di `SourceLevels` in `mod.ts`).

## Scelte di progetto

Decise con l'utente prima della stesura:

- **Si implementa il matrix esistente**, non parametri a destinazione fissa. Nessun parametro
  nuovo in `parameters.json`, nessuna UI nuova, nessuna modifica al formato di stato.
- **`env` è l'inviluppo d'ampiezza già esistente**, riusato come sorgente. Niente secondo ADSR.
  Conseguenza accettata: il filtro segue la forma dell'ampiezza, quindi un pluck di filtro sotto
  una coda lunga non è esprimibile. Se servirà, sarà un lavoro successivo che aggiunge una
  sorgente `env2` al `ModSource`, cioè un'estensione del contratto, non una sua riscrittura.
- L'**inviluppo di filtro** è quindi l'assegnazione `env → cutoff` con depth, non un gruppo di
  parametri nuovo.

## Architettura

### 1. Il tipo condiviso: `Source/engine/ModMatrix.h` (nuovo)

```cpp
namespace engine
{
enum class ModSource { lfo, env, vel, mw, count };

struct ModRoute
{
    ModSource src;
    params::ParamSlot target;   // già risolto: mai una stringa sul thread audio
    float depth;                // -1..1
};

struct ModSnapshot
{
    static constexpr int kMaxRoutes = 32;
    ModRoute routes[kMaxRoutes] {};
    int count { 0 };
};
}
```

Il `target` è uno `params::ParamSlot`, non un `ParamId` testuale: la risoluzione da stringa a
slot avviene **sul message thread**, quando si costruisce lo snapshot. Sul thread audio non
esiste nessun confronto di stringhe, coerentemente con la scelta già fatta in `ParamCollect.h`.

`ParamSlot` oggi è dichiarato dentro `ParamCollect.h`, insieme al template
`collectEngineParams`. Va spostato in un header proprio (`Source/parameters/ParamSlot.h`), che
`ParamCollect.h` includerà: così `ModMatrix.h` ottiene l'enum senza trascinarsi dietro il
template e le sue dipendenze.

`kMaxRoutes = 32`: la UI non impone un limite, quindi lo impone il motore. Le assegnazioni
oltre la 32ª vengono scartate in costruzione, sul message thread.

### 2. Pubblicazione lock-free verso il thread audio

Stesso schema già collaudato per la wavetable (`SynthEngine::setPendingWavetable`), con una
differenza: le mod cambiano più spesso di una wavetable, quindi un doppio buffer non basta —
il thread audio potrebbe ancora leggere il buffer che il message thread sta riscrivendo.

Anello di 4 snapshot preallocati in `SynthEngine`, mai distrutti, più un
`std::atomic<const ModSnapshot*> activeMods_`. Il message thread scrive nello slot successivo
e pubblica il puntatore con `release`; il thread audio fa una `load(acquire)` all'inizio di
`process()` e usa quel puntatore per tutto il blocco. Con 4 slot il message thread dovrebbe
pubblicare 4 volte dentro un singolo blocco audio per raggiungere il lettore: impossibile nella
pratica, e comunque non produce altro che una modulazione sbagliata per un blocco, mai una
lettura di memoria liberata.

Chi costruisce lo snapshot: `PluginProcessor`, registrandosi come `juce::ValueTree::Listener`
sul nodo `MODS` (lo stesso nodo che `StateChannel` già ascolta per rimbalzare lo stato alla UI).

### 3. Destinazioni ammesse

La UI permette di trascinare una sorgente su **qualunque** knob, ma il motore sa modulare solo
ciò che legge. Prima versione:

| Target | Slot | Note |
|---|---|---|
| `cutoff` | `ParamSlot::cutoff` | Il caso d'uso principale: `env → cutoff` è l'inviluppo di filtro |
| `res` | `ParamSlot::res` | |
| `wtpos` | `ParamSlot::wtpos` | Morph modulato |
| `level` | `ParamSlot::level` | |
| `pan` | `ParamSlot::pan` | |
| `fine` | `ParamSlot::fine` | Vibrato: LFO → fine |
| `drive` | `ParamSlot::drive` | |

Un target fuori da questa lista viene **scartato in costruzione dello snapshot**, sul message
thread: sul thread audio non arriva mai una route che non si sa applicare. La UI continuerà a
mostrare l'assegnazione come attiva; è un difetto noto e accettato per questa fase. Rimediarlo
significa esporre alla UI l'elenco dei target supportati, cioè estendere il payload di
`getState()` — lavoro separato, fuori da questa spec.

### 4. Le quattro sorgenti

| Sorgente | Ambito | Valore | Da dove |
|---|---|---|---|
| `vel` | per voce | 0..1, costante per tutta la nota | `SynthVoice::velocity_`, già presente |
| `env` | per voce | 0..1, il livello istantaneo dell'ADSR d'ampiezza | Serve un accessore nuovo `ADSREnvelope::getLevel()` |
| `lfo` | per voce se `lretrig`, altrimenti globale | −1..1 | Modulo nuovo `dsp::Lfo` |
| `mw` | globale | 0..1, smussato 20 ms | MIDI CC 1, gestito in `SynthEngine::handleMidiEvent` |

`env` e `vel` sono per voce: due note tenute contemporaneamente hanno inviluppi a punti diversi
della loro corsa, quindi la modulazione **deve** vivere dentro `SynthVoice`, non in
`EngineParams`, che è una struct per blocco condivisa da tutte le voci.

### 5. L'LFO: `Source/dsp/Lfo.{h,cpp}` (nuovo)

Le cinque forme vanno portate da `mod.ts` **con le stesse formule**, inclusa la `S&H`, che nella
UI è `Math.sin(Math.floor(ph * 8) * 7.3)` — non un vero sample & hold, ma un pattern
pseudo-casuale deterministico. Riprodurlo identico è ciò che tiene allineato il puntino animato
del tab LFO con quello che si sente.

Parametri già esistenti in `parameters.json`, tutti da cablare:

| Parametro | Mappa | Comportamento |
|---|---|---|
| `lshape` | choice ×5 | Sine, Tri, Saw, Square, S&H |
| `lrate` | log 0.05..20 Hz | Frequenza in Hz quando `lsync` è falso |
| `lsync` | bool | Vero: la frequenza viene dal tempo dell'host |
| `lphase` | 0..360° | Offset di fase all'avvio |
| `lfade` | 0..4000 ms | Dissolvenza in entrata della profondità, dal note-on |
| `lretrig` | bool | Vero: fase azzerata a ogni nota (LFO per voce). Falso: una fase sola condivisa |

**Sync al tempo.** `lsync` richiede il BPM dell'host, e oggi `AudioPlayHead` non è usato da
nessuna parte nel progetto (verificato con grep su `Source/`). Va aggiunto: `PluginProcessor`
legge `getPlayHead()->getPosition()` una volta per blocco e passa il BPM in `EngineParams`.
Quando l'host non espone un tempo (Standalone, host offline) si ripiega su **120 BPM**, che è
anche il valore che rende il comportamento deterministico nei test.

Le divisioni sono già fissate dalla UI (`Tabs.tsx:82`): `["1/16", "1/8", "1/4", "1/2", "1", "2"]`,
selezionate da `lrate` grezzo con `min(5, floor(v * 6))`. Il DSP deve usare **la stessa
indicizzazione**, altrimenti l'etichetta del knob mente.

### 6. Dove si applica la modulazione

Dentro `SynthVoice::setParams`, una volta per blocco e per voce:

```
per ogni target modulabile:
    normalizzato = clamp01 (base[target] + Σ route.depth × livelloSorgente[route.src])
    reale        = conversione[target] (normalizzato)
    -> alimenta lo SmoothedValue già esistente per quel target
```

**`conversione[target]` non è sempre `params::denormalise`.** `collectEngineParams` applica a
quasi ogni parametro una trasformazione propria dopo la denormalizzazione, e la modulazione deve
passare per la stessa, altrimenti un cutoff modulato e un cutoff mosso a mano non finiscono allo
stesso posto:

| Target | Da normalizzato a reale |
|---|---|
| `cutoff` | `denormalise` (mappa Log) |
| `res` | `kButterworthQ × pow (12 / kButterworthQ, denormalise × 0.01)` — la mappa esponenziale di Q |
| `wtpos` | identità: è già 0..1 sul set di frame, nessuna denormalizzazione |
| `level` | identità: il valore grezzo **è già** il guadagno lineare, malgrado la mappa dichiarata `Db` (vedi il commento in `ParamCollect.h` e `WebUI/src/synth/mapping.ts`) |
| `pan` | `denormalise × 0.02` (da −50..50 a −1..1) |
| `fine` | `denormalise` (cent), poi confluisce in `tuningSemitones_` |
| `drive` | `Decibels::decibelsToGain (denormalise)` |

Queste sette conversioni oggi vivono in linea dentro `collectEngineParams`. Vanno estratte in
funzioni piccole nello stesso header, così che esista **una sola definizione** usata sia dal
percorso non modulato sia da quello modulato. È l'unico refactoring che questa spec chiede su
codice esistente, ed è quello che impedisce alle due strade di divergere.

`EngineParams` deve quindi portare, oltre ai valori già denormalizzati, anche i **valori
normalizzati di base** dei target modulabili — è su quelli che si somma il depth. Si aggiunge un
`std::array<float, kNumModTargets>` (`kNumModTargets = 7`, i target della tabella al punto 3)
più tre campi: il puntatore allo snapshot, il BPM e il livello corrente del mod wheel.

**Frequenza di aggiornamento.** La modulazione si valuta una volta per blocco, coerentemente con
la scelta già fatta per cutoff e Position (`SynthVoice::render` li aggiorna con `skip
(numSamples)` perché costano `tan()` e ricalcolo degli indici di frame). Con blocchi da 128
campioni a 48 kHz il tasso di aggiornamento è 375 Hz: più che sufficiente per un LFO che arriva
a 20 Hz. Gli `SmoothedValue` già presenti coprono i gradini fra un blocco e il successivo. Il
limite va scritto nel commento: questo è un sistema di modulazione **a tasso di controllo**, non
audio-rate; FM e AM non sono esprimibili e non sono un obiettivo.

### 7. Telemetria

`SynthEngine::process` scrive il livello dell'LFO in `MeterFrame::lfo` una volta per blocco:
la fase globale se `lretrig` è falso, altrimenti quella della voce più recente. È il dato che
`LfoDot` in `Tabs.tsx` già consuma.

## File toccati

| File | Modifica |
|---|---|
| `Source/engine/ModMatrix.h` | nuovo |
| `Source/dsp/Lfo.h`, `Source/dsp/Lfo.cpp` | nuovi |
| `Source/dsp/ADSREnvelope.h` | accessore `getLevel()` |
| `Source/engine/EngineParams.h` | valori normalizzati di base, snapshot, BPM, mod wheel |
| `Source/engine/SynthVoice.{h,cpp}` | LFO per voce, calcolo dei livelli sorgente, applicazione |
| `Source/engine/SynthEngine.{h,cpp}` | anello degli snapshot, CC 1, LFO globale, `MeterFrame::lfo` |
| `Source/parameters/ParamCollect.h` | riempie i valori normalizzati di base |
| `Source/plugin/PluginProcessor.{h,cpp}` | listener su `MODS`, costruzione snapshot, `AudioPlayHead` |
| `CMakeLists.txt` | `Source/dsp/Lfo.cpp` nei due target |
| `Tests/` | test nuovi, vedi sotto |
| `docs/architecture.md` | sezione sulla modulazione |

**Nessuna modifica** a `parameters.json`, `presets.json`, `StateTree.h`, `StateChannel.cpp`,
`WebUI/`. È il segno che il contratto era già giusto.

### Punto di attrito con il lavoro in corso

`Source/dsp/ADSREnvelope.h` è in questo momento modificato da un altro agent (fix del bug
`sustain = 0`). L'aggiunta di `getLevel()` va fatta **dopo** che quel lavoro è rientrato, non in
parallelo.

## Piano di test

| # | Cosa verifica | Come |
|---|---|---|
| 1 | Il DSP e la UI calcolano lo stesso valore modulato | Tabella di casi (base, depth, livelli) verificata sia dal test C++ sia da un test TS su `liveValue`, con gli stessi numeri scritti in entrambi |
| 2 | `env → cutoff` è un inviluppo di filtro | Depth 1, attacco lungo: il centroide spettrale della nota deve salire in modo monotono durante l'attacco |
| 3 | Bipolarità dell'LFO | Depth positivo e negativo producono escursioni opposte attorno al valore base |
| 4 | Clamp | Base 0.9 + depth 1 × livello 1 non supera il massimo del parametro |
| 5 | Target non supportato | Una route verso un target fuori lista non arriva nello snapshot e non cambia l'uscita |
| 6 | Oltre `kMaxRoutes` | La 33ª assegnazione viene scartata senza crash e senza allocazione |
| 7 | Forme d'onda | I 5 profili dell'LFO combaciano con `lfoShape` di `mod.ts` su una griglia di fasi |
| 8 | Retrigger | Con `lretrig` vero la fase riparte da `lphase` a ogni note-on; con falso prosegue |
| 9 | Fade | Con `lfade` a 1000 ms la profondità a 500 ms dal note-on è circa metà |
| 10 | Sync | A 120 BPM, divisione `1/4`, l'LFO compie 2 cicli al secondo; senza playhead il fallback resta 120 |
| 11 | Sicurezza real-time | Nessuna allocazione e nessun lock in `process()` con matrix pieno (test già presente nella suite per il resto del motore, da estendere) |
| 12 | Nessuna regressione | Con `MODS` vuoto l'uscita è campione per campione identica a prima |

Il test 12 è il più importante: è la garanzia che questo lavoro aggiunge una capacità senza
cambiare il suono di chi non la usa.

## Fuori portata

Esplicitamente **non** in questa spec, ciascuno un lavoro a sé:

- Arp (`arpOn`, `arpMode`, `arpRate`, `arpGate`, `arpOct`, `arpSwing`) e i 16 step già
  persistiti in `StateTree.h`.
- FX: chorus (`chRate`, `chDepth`, `chMix`) e reverb (`rvSize`, `rvDamp`, `rvMix`).
- Unison e detune, glide, `voiceMode` mono/poly.
- `envCurve`: la forma dell'inviluppo è indipendente dalla modulazione.
- Esporre alla UI quali target il motore sa modulare.
- Una seconda sorgente di inviluppo (`env2`) indipendente dall'ampiezza.
