# Striscia bassa: tastiera web, wheel e pitch bend

Data: 2026-09-20
Stato: approvato in brainstorming, pronto per il piano di implementazione

## Problema

La striscia bassa del plugin è oggi una `juce::MidiKeyboardComponent` riskinnata
(`Source/ui/XerumKeyboard.{h,cpp}`) montata **sotto** la WebView, fuori dallo chassis. Ha tre
conseguenze:

- la sua pelle vive in C++ e va tenuta a mano allineata ai token di `theme.css`: due sorgenti di
  verità per gli stessi colori, e `design-sync` non la vede;
- non offre nessun controllo di esecuzione — niente ottava, niente velocity, niente wheel;
- costringe l'editor a una geometria a due pezzi (`webHeightForWidth` + altezza tastiera) e la
  WebUI a squadrare gli angoli bassi dello chassis perché la striscia ci si attacchi
  (`XerumKeyboard::setBottomCornerRadius`, `SynthWindow.test.tsx:24`).

## Obiettivo

Sostituire la striscia nativa con una striscia **dentro la WebUI**: tasti ridisegnati più una
barra di esecuzione con ottava, velocity, pitch/mod wheel e stato MIDI. La WebView diventa
l'intero editor.

## Non obiettivi

Questo documento copre solo il sottoprogetto **A**. Restano fuori, ognuno con spec propria:

- **B — device I/O standalone**: enumerazione e scelta delle periferiche MIDI in ingresso e
  dell'uscita audio, hot-plug, persistenza. A predispone solo il posto dove quell'informazione si
  mostrerà.
- **C — MIDI learn CC → parametro**: mappe, persistenza nello stato, UI di assegnazione. Vale
  anche in plugin, non solo standalone. Non tocca A.

Restano fuori anche le scorciatoie di esecuzione (HOLD dell'arp, mono/legato, glide) nella barra:
valutate ed escluse.

## Decisioni prese

| Questione | Decisione | Alternativa scartata |
|---|---|---|
| Ruolo della striscia | tastiera suonabile + barra di esecuzione sopra | barra con tastiera a scomparsa; solo performance pad |
| Rendering | tutto nella WebUI | tasti nativi + barra web; tutto nativo |
| Spike preliminare | no, si va dritti | spike di misura del round-trip e del focus |
| Pitch wheel | dentro A, con pitch bend vero nel motore | rimandata a dopo; wheel come sorgente generica del matrix |
| Footer attuale | sfoltito e fuso nella barra | fusione integrale; due righe distinte |
| Velocity del click | valore fisso regolabile dalla barra | dall'altezza del click, come oggi |

### Rischi accettati consapevolmente

Dalla ricerca del 18 set 2026 su UI web nei plugin audio
(`docs/research/`, memoria `xerum-webview-risks`):

1. **Suonare da QWERTY** oggi è nativo (`PluginEditor.cpp:172`, `setKeyPressBaseOctave`). In web
   diventa `keydown` JS e funziona solo quando la WebView ha il focus. Il furto di focus alle
   shortcut dell'host è irrisolto in JUCE a set 2026, e FL Studio 25 ha un problema di passaggio
   tasti che colpisce tutti i plugin con webview.
2. **Pointer capture** per il glissando usa lo stesso meccanismo di `useDragValue.ts:78`, che sul
   forum JUCE risulta rotto dentro la web view. Non è un rischio nuovo: se è rotto sono già rotte
   tutte le manopole. Ma i tasti ci si appoggiano, e da qui viene l'obbligo di `allNotesOff()`.
3. **Latenza click → nota**: oggi zero, in web passa per una native function asincrona. Mai
   misurata su WKWebView in questo progetto.

Il verso opposto — nota in arrivo che accende il tasto — non è a rischio: viaggia nel frame
`meters` già esistente.

## Geometria

Lo chassis passa da `900×600` a **`900×708`**: 600 di pannello invariato più 108 di striscia. Il
rapporto 900:708 diventa il vincolo del `ChassisConstrainer`; `kMinScale 0.72` e `kMaxScale 1.5`
restano. A scala 1 la finestra è più alta di 30 px rispetto a oggi (708 contro 678).

La striscia non è due righe su tutta la larghezza: le wheel hanno bisogno di corsa verticale.

```
┌─────────┬────────────────────────────────────────────────┐
│ ╷    ╷  │ OCT −  C3  +  │ VEL 80 │      MIDI IN ●  ▮▮▮▯   │ 28
│ │PB │MW │ ──────────────────────────────────────────────  │
│ ╵    ╵  │ ▌│ ▌│  │ ▌│ ▌│ ▌│  │ ▌│ ▌│  │ ▌│ ▌│ ▌│  │ ▌│  │ 80
└─────────┴────────────────────────────────────────────────┘
   64px                      ~820px  (4 ottave, 28 bianchi, ~29px)
                                       ^ spia note + meter OUT
```

Tasti: 4 ottave visibili, 28 tasti bianchi da ~29 px. `OCT −/+` sposta la nota di partenza di
un'ottava per volta; il readout mostra la nota più bassa visibile. Default C2, come oggi.

## Componenti

### Nuove primitive in `@xerum/ui`

Entrambe entrano in `design-sync` come le altre.

**`Wheel`** — cilindro verticale. Prop `bipolar` (detent al centro) e `springBack` (torna a zero
al rilascio). Due istanze nella striscia: PB bipolare con spring-back, MW unipolare che resta dov'è.
Riusa `useDragValue` con `axis: "y"`, come `Fader`.

**`Keybed`** — puro e controllato, nessuna conoscenza del bridge. Prop: `firstNote`, `octaves`,
`activeNotes`, `velocity`, `onNoteOn(note, velocity)`, `onNoteOff(note)`, `onAllNotesOff()`.

Reso in DOM, non su canvas: 28 tasti bianchi e 20 neri sono pochi, e `<div>` dà hit-testing e
accessibilità gratis. Ma **l'accensione dei tasti non passa da React**: le classi si mutano via
ref, come `WaveDisplay` fa col canvas. Un re-render di 48 nodi trenta volte al secondo dentro una
WebView è spreco di CPU che si paga nel plugin.

### Nuovi componenti lato synth (`WebUI/src/synth/ui/`)

- **`BottomStrip.tsx`** — cabla `Keybed` e le due `Wheel` al bridge, tiene lo shift d'ottava e la
  velocity correnti.
- **`PerformanceBar.tsx`** — `OCT −/+` con readout, `VEL`, stato MIDI, e a destra i meter IN/OUT
  ereditati dal Footer.

### Componenti rimossi

- `Footer.tsx` eliminato. `MetersContext` resta e viene consumato da `PerformanceBar`. `VOICES` e
  `CPU` spariscono: erano numeri finti per ammissione del file stesso. Renderli veri è lavoro a
  parte — `VOICES` costa poco (contatore in `VoiceManager` → campo in `MeterFrame`), `CPU` no.
- `Source/ui/XerumKeyboard.{h,cpp}` cancellati.

### Editor

`PluginEditor.cpp` dimagrisce: spariscono `keyboard_`, `configureKeyboard()`, `kKeyboardHeight`,
`kWhiteKeysVisible`, `kLowestNote`, `kHighestNote`, `kChassisCorner`, `webHeightForWidth()`.
`resized()` diventa `webView_.setBounds(getLocalBounds())`.

`processorRef_.getKeyboardState()` **resta**: è il punto d'ingresso delle note dalla UI, già
mergiato in `processBlock()` (`PluginProcessor.cpp:275`). Cambia solo chi ci scrive.

`gutter=0` e `data-attached` restano: servivano ad attaccare la striscia nativa, ma servono ancora
a togliere il margine e far riempire la WebView.

## Il canale MIDI del bridge

Nuovo `Source/bridge/MidiChannel.{h,cpp}`, stesso stampo di `StateChannel`: `applyTo(Options)`
chiamata prima di costruire la WebView, registrazione in `PluginEditor`.

### Web → nativo

| Native function | Destinazione |
|---|---|
| `noteOn(note, velocity)` | `keyboardState_.noteOn(1, note, velocity)` |
| `noteOff(note)` | `keyboardState_.noteOff(1, note, 0)` |
| `allNotesOff()` | `keyboardState_.allNotesOff(1)` |
| `setWheel(kind, value)` | atomici nel processor, vedi sotto |

Le note riusano la strada esistente: `keyboardState_.processNextMidiBuffer(midi, ...)` fa sì che
una nota della UI entri nel flusso indistinguibile da una dell'host. Nessun codice nuovo a valle.
Il `CriticalSection` che `MidiKeyboardState` prende internamente è già documentato e accettato a
`PluginProcessor.cpp:273`.

`allNotesOff()` non è cosmetico: se la WebView perde il puntatore a metà click — cambio finestra, o
il bug di pointer capture — la nota resta appesa e suona per sempre. La UI lo chiama su
`pointercancel`, su `blur` e allo smontaggio.

### Le wheel non passano da `MidiKeyboardState`

`MidiKeyboardState` trasporta solo note. Le wheel usano due atomici nel processor,
`uiPitchBend {8192}` e `uiModWheel {-1}`; a ogni `processBlock`, se sono cambiati dall'ultimo giro,
il processor **antepone un vero messaggio MIDI** al buffer.

Così l'arp, il motore e la mod matrix non sanno né devono sapere che quella rotella è disegnata:
una wheel della UI è indistinguibile da una hardware, e CC 1 finisce da solo nel ramo che esiste
già a `SynthEngine.cpp:193`, dove `mw` è già sorgente del matrix.

### Nativo → web: il mask delle note attive

Due campi nuovi in `engine::MeterFrame`: `notesLo` e `notesHi`, `std::atomic<uint64_t>`.

Regime **istantaneo** — `load()` senza azzeramento, non `exchange(0)`. È la stessa ragione già
scritta nel commento di `MeterFrame` per `mw`: una nota tenuta è uno stato, non un transitorio, e
azzerarla a ogni lettura la spegnerebbe appena il thread audio smette di girare.

`MeterChannel` li spedisce nel frame `meters` già esistente come **quattro numeri da 32 bit**
(`n0..n3`): un `uint64` non entra esatto nella mantissa di un `double`, quattro da 32 sì. Nessun
timer nuovo, nessun canale nuovo, 33 ms di latenza su un'accensione — invisibili a occhio.

I bit si alzano in `SynthEngine::handleMidiEvent`, che sta **a valle dell'arp**
(`SynthEngine.cpp:596` processa l'arp, `:629` passa il risultato a `handleMidiEvent`). Ad arp
acceso la tastiera mostra quindi il **pattern che suona**, non i tasti tenuti premuti. È la scelta
voluta: si vede davvero cosa esce.

## Pitch bend nel motore

Il punto d'innesto esiste già ed è uno solo: `midiNoteToHz(note, offsetSemitones)`
(`SynthVoice.cpp:65`) prende un offset in semitoni, ed è lì che oct, semi, fine e detune
confluiscono in `tuningSemitones_`. Il bend è un addendo in più in quella somma. Con bend a zero i
bit restano identici (si somma `0.0f`): stesso argomento di non-regressione già scritto nel
commento della funzione per la nota frazionaria del glide.

1. **Parsing** — in `SynthEngine::handleMidiEvent`, ramo `message.isPitchWheel()` accanto a quello
   di CC 1: `pitchBend_ = (getPitchWheelValue() - 8192) / 8192.0f`, campo −1..1 del solo thread
   audio, come `modWheel_`.
2. **Parametro `pbRange`** — in `Source/parameters/parameters.json`, che è la verità unica:
   `kind: "int"`, `group: "osc"`, `slot: true`, `map` lineare `0..24`, default **2**, unit `SEMI`.
   `scripts/gen-params.mjs` rigenera tabella C++ e `params.generated.ts`. Il campo `slot` va a
   `true`, altrimenti `collectEngineParams` non vede il parametro.
3. **Propagazione** — `pitchBend` in `EngineParams` accanto a `modWheel` (`EngineParams.h:169`),
   riempito dove si riempie `modWheel` (`SynthEngine.cpp:584`). La voce somma `bend * pbRange` in
   `tuningSemitones_`.
4. **Arp** — nessuna modifica. `Arpeggiator.cpp:473` già dichiara di non interpretare il pitch bend
   e di lasciarlo passare: il bend piega il pattern intero, che è il comportamento giusto.

**Da verificare implementando, non deciso qui**: il bend è applicato per sotto-fetta di controllo,
come `modWheel`. Se muovendo la rotella si sente lo zipper, serve uno `SmoothedValue` sul bend. Non
è nel design a priori perché smorzerebbe anche i bend rapidi voluti, e `modWheel` senza smoothing
finora non ha dato problemi. Se salta fuori è un fix locale a `SynthVoice`.

## Test

### C++

Nuovo `Tests/PitchBendTests.cpp`:

- bend a 0 bit-identico a HEAD;
- bend +1 con `pbRange 2` → +2 semitoni misurati in frequenza;
- `pbRange 0` rende il bend inerte;
- bend durante un glide non lo interrompe;
- arp acceso: il pattern intero si piega.

In `Tests/ParameterSeamTests.cpp`, **obbligatorio**: `pbRange` attraverso la cucitura
normalizzato ↔ naturale. È un `int` con `map` lineare, cioè esattamente la forma che il 19 set ha
prodotto il bug di trasposizione di 4 ottave su `oct`/`semi`. Stessa forma, stessa trappola.

In `Tests/EngineTests.cpp`, il mask delle note senza WebView: note-on alza il bit, note-off lo
abbassa, `allNotesOff` pulisce, e ad arp acceso i bit seguono il pattern.

`MidiChannel` non prende test unitari, come `StateChannel` e `MeterChannel`: servirebbe una
`WebBrowserComponent` viva.

### Web (vitest)

`Keybed`: click → `onNoteOn` con la velocity della barra; drag da tasto a tasto → note-off della
precedente e note-on della nuova; `pointercancel` → `allNotesOff`; `activeNotes` accende le classi
giuste.

`Wheel`: spring-back al rilascio in modalità PB, tenuta in modalità MW, detent al centro.

`SynthWindow.test.tsx:24` si inverte: oggi asserisce che il fondo dello chassis sia squadrato per
la tastiera nativa, deve asserire che sia arrotondato.

## Ordine di costruzione

Cinque fasi, ognuna verificabile da sola, con la tastiera nativa che continua a funzionare fino
all'ultima.

1. **Pitch bend e `pbRange`** — solo DSP, nessuna UI, test propri. Si prova con un controller vero.
2. **`MidiChannel` e note mask** — bridge in piedi mentre la striscia nativa è ancora al suo posto:
   si verifica dal dev server.
3. **`Wheel` e `Keybed` in `@xerum/ui`** — primitive isolate, storie, design-sync.
4. **`BottomStrip` e `PerformanceBar`**, `Footer` eliminato, chassis a 708.
5. **Rimozione di `XerumKeyboard`** ed editor dimagrito. Il punto di non ritorno, ultimo e da solo.

## Aggancio dei sottoprogetti successivi

Lo stato MIDI in `PerformanceBar` è il posto dove B atterrerà.

In A non esiste ancora nessuna enumerazione di periferiche — è tutta roba di B — quindi quello
spazio mostra solo due cose, entrambe già disponibili: la sorgente (`HOST` nel plugin, `MIDI IN`
nello standalone) e una spia di attività accesa dal mask delle note, che funziona ovunque perché
viene dal motore e non dal device manager. Il nome del device compare quando arriva B.
