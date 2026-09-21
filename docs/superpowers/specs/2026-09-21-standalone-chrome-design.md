# Standalone senza chrome: finestra nativa e impostazioni dentro la UI

Data: 2026-09-21. Stato: approvato in chat, in attesa del piano.

## Obiettivo

Togliere allo Standalone la barra del titolo e il bottone **Options** che JUCE gli mette
addosso, e portare quello che Options faceva — scelta della scheda audio, sample rate, buffer
size, ingressi MIDI — dentro la WebUI, dietro l'ingranaggio che l'header ha già.

Il risultato da raggiungere è quello dei plugin commerciali: una finestra che sembra fatta
apposta, non un editor incastrato dentro una cornice di sistema.

## Non obiettivi

- **Windows.** La finestra custom è codice macOS. Un giorno andrà riscritta per Windows (bordi,
  snap, massimizza e barra di sistema si comportano diversamente); quel giorno non è questo.
- **L'ingresso audio.** `PluginProcessor.cpp:16` dichiara solo `withOutput ("Output", stereo)`:
  nessun campione entra nel processore. Esporre la scelta del device d'ingresso significherebbe
  mostrare un controllo che non può fare nulla. Aggiungere il bus è un'altra feature — tocca
  `processBlock`, il layout dei bus e la validazione AU.
- **Riscrivere `MidiDeviceChannel`.** Funziona e resta com'è: cambia solo dove i suoi controlli
  sono disegnati.
- **Compilare `Source/bridge/*` in `XerumTests`.** Resta fuori come oggi (vedi `docs/build.md`);
  questa feature non ribalta quella scelta, si limita a rendere testabile il pezzo che conta.

## Cosa c'è oggi (letto, non supposto)

- Il binario Standalone usa `juce::StandaloneFilterApp`, generata da JUCE
  (`juce_audio_plugin_client_Standalone.cpp:58`). Il repo non ha nessuna classe app propria.
- `StandaloneFilterWindow` è una `DocumentWindow` con barra del titolo e `optionsButton`
  (`juce_StandaloneFilterWindow.h:653-666`); Options apre un `AudioDeviceSelectorComponent`.
- `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP` spegne quella app e chiede un
  `juce_CreateApplication()` nostro (`juce_audio_plugin_client_Standalone.cpp:222`).
- `MidiDeviceChannel` (`Source/bridge/`) porta già gli ingressi MIDI nella UI e trova l'holder
  con `StandalonePluginHolder::getInstance()` (`MidiDeviceChannel.cpp:30`). In AU/VST3 risponde
  `host: true` con la lista vuota: è lo stampo da seguire.
- L'ingranaggio in `Header.tsx:54` è un `<Button>` **senza `onClick`**: un posto pronto e vuoto.
- Il selettore MIDI sta in `PerformanceBar.tsx:80`, non nella BottomStrip.
- `StandalonePluginHolder::saveAudioDeviceState()` (`juce_StandaloneFilterWindow.h:319`) scrive
  `audioSetup` nel `PropertiesFile`; il costruttore rilegge con `reloadAudioDeviceState`.
- `DocumentWindow::setTitleBarHeight` non ha vincoli sul valore (`juce_DocumentWindow.cpp:111`).
- `withNativeFunction` è il canale JS → C++ già usato da `MidiChannel`.

## L'approccio, e i due scartati

Il nodo è uno solo: **la `WKWebView` copre la finestra e si prende i mouse event**. Il
trascinamento non può arrivare a JUCE per la via normale, e `-webkit-app-region: drag` è roba di
Electron che in `WKWebView` non esiste.

**Scelto — finestra nativa con barra trasparente e contenuto a tutta altezza.**
`titlebarAppearsTransparent`, `titleVisibility = NSWindowTitleHidden`,
`NSWindowStyleMaskFullSizeContentView`. La finestra resta nativa, quindi resize, full screen,
Mission Control, snap e semaforo continuano a funzionare **senza scrivere una riga**. Il
contenuto si estende sotto la barra, che diventa invisibile.

**Scartato — `DocumentWindow` con `setTitleBarHeight(0)`.** Meno codice, ma su macOS impone di
spegnere la native title bar, e con lei se ne vanno semaforo, full screen e Mission Control. Il
trascinamento andrebbe comunque rifatto.

**Scartato — finestra borderless** (`addToDesktop` senza `windowHasTitleBar`). Controllo totale
e tutto da riscrivere: drag, resize, minimize, snap. Sembra il più pulito, costa di più e rende
meno.

Il prezzo dell'approccio scelto è dichiarato: `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP` sostituisce
codice che JUCE mantiene con codice che manteniamo noi. Se `StandalonePluginHolder` cambia in un
aggiornamento di JUCE, la finestra custom se ne accorge quando smette di compilare — o quando
smette di comportarsi bene. Vale la pena solo perché il resto resta in mano a macOS.

## 1. L'app e la finestra

`CMakeLists.txt` definisce `JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1` sul target Standalone.

`Source/standalone/XerumStandaloneApp.cpp` fornisce `juce_CreateApplication()`. L'app **riusa
`juce::StandalonePluginHolder` invariato**: è lui che tiene l'`AudioDeviceManager`, apre il MIDI,
salva le impostazioni e ricarica lo stato all'avvio. Cambia solo la finestra che ospita
l'editor: `XerumStandaloneWindow` copia di `StandaloneFilterWindow` le due parti utili —
salvataggio di posizione/dimensione, chiusura che spegne l'app — e lascia fuori barra del titolo
e bottone Options.

Resta una `DocumentWindow` **con la native title bar accesa** (`setUsingNativeTitleBar (true)`,
che è il default): è quella barra nativa a portare il semaforo, il full screen e lo snap. Non
viene tolta, viene resa invisibile dai tre attributi `NSWindow` qui sotto. Spegnerla
significherebbe ricadere nell'approccio scartato.

`Source/standalone/XerumWindowMac.mm` è il **primo file Objective-C++ del progetto** (oggi
`Source/` è tutto `.cpp`/`.h`) e va aggiunto alle sorgenti del target Standalone in CMake.
Espone due funzioni sole, chiamate dopo `addToDesktop`:

- `makeWindowChromeless (void* nsWindowHandle)` — prende l'`NSWindow` da `getWindowHandle()` e le
  applica i tre attributi sopra;
- `beginNativeWindowDrag (void* nsWindowHandle)` — vedi sotto.

## 2. Il trascinamento

Al `mousedown` sulla fascia di trascinamento la pagina chiama la native function
`beginWindowDrag`; il C++ prende `[NSApp currentEvent]` e lo passa a
`performWindowDragWithEvent:`. Da lì il trascinamento lo gestisce macOS: fluido, e **nessun
`mousemove` attraversa il bridge**.

**Questo è il rischio numero uno dell'intero lavoro.** `WKWebView` consegna i messaggi alla JS
bridge in modo **asincrono**: quando la native function gira, `[NSApp currentEvent]` potrebbe non
essere più quel `mousedown`. Va verificato per primo, con uno spike da buttare (vedi *Ordine di
lavoro*). Se cade, il ripiego è far mandare alla pagina i delta di `mousemove` e muovere la
finestra con `setBounds`: funziona di sicuro, è meno fluido, e cambia questa sezione.

Serve anche `toggleWindowZoom`, chiamata sul `dblclick`: su macOS il doppio clic sulla barra
ingrandisce la finestra, e chi usa un Mac lo dà per scontato.

## 3. `AudioSettingsChannel`

`Source/bridge/AudioSettingsChannel.{h,cpp}`, sullo stampo di `MidiDeviceChannel`: stesso
`applyTo (Options)` prima della WebView, stesso `setWebView`, stesso `ChangeListener`
sull'`AudioDeviceManager`.

**Lettura** — `getAudioSettings()` restituisce una fotografia sola:

    { standalone: bool,
      outputs: [{ id, name }], currentOutput,
      sampleRates: [...], currentSampleRate,
      bufferSizes: [...], currentBufferSize,
      latencyMs }

Le liste vengono da `AudioIODeviceType::getDeviceNames(false)` e da
`AudioIODevice::getAvailableSampleRates()` / `getAvailableBufferSizes()`: **dipendono dal device
aperto**, quindi cambiando scheda cambiano anche loro. La UI non può tenersele in cache.

**Scrittura** — `setAudioOutput(id)`, `setSampleRate(hz)`, `setBufferSize(n)`. Tutte e tre
leggono l'`AudioDeviceSetup` corrente, cambiano un campo e chiamano
`setAudioDeviceSetup (setup, true)`. Quel metodo **ritorna una stringa d'errore** (vuota se è
andata): va girata alla UI nel completion della native function. Un device che non si apre —
sample rate non supportato, scheda staccata — deve dirlo, non restare in silenzio con la vecchia
impostazione ancora attiva.

**Evento** — `"audioSettingsChanged"`, stesso payload, dal `changeListenerCallback`. Serve anche
quando non sei tu a cambiare: staccando l'interfaccia, macOS ricade sul default e il pannello
deve aggiornarsi da solo.

**Persistenza**: il canale chiama `StandalonePluginHolder::saveAudioDeviceState()` dopo ogni
cambio riuscito, così la scelta sopravvive a una chiusura brutta.

**In AU/VST3** risponde `standalone: false` con le liste vuote, come `MidiDeviceChannel` risponde
`host: true`.

## 4. La UI

**`SettingsOverlay.tsx`**, stesso pattern di `PresetOverlay`: overlay sopra lo chassis, aperto da
uno stato nuovo in `useSynth` (`settings`, accanto a `browse`) e montato in `SynthWindow.tsx:207`.
L'ingranaggio di `Header.tsx:54` diventa il suo interruttore.

Tre gruppi: **uscita audio** (device, sample rate, buffer, con la latenza in ms come testo
sotto), **MIDI** (il selettore spostato da `PerformanceBar.tsx:80`, che continua a chiamare le
native function di `MidiDeviceChannel`), e la riga d'errore di `setAudioDeviceSetup`. In AU/VST3
i primi due gruppi diventano una riga di testo.

Spostando il selettore MIDI, **nel plugin la scritta "MIDI HOST" sparisce dalla barra in basso**
e si legge solo nel pannello: una perdita piccola ma reale, presa consapevolmente.

**Lo spazio per il semaforo.** I tre pallini sono disegnati da macOS in coordinate di
**finestra**; l'header vive dentro lo chassis, scalato con `transform: scale()`. I due non si
muovono insieme: a scala 1.4 un padding in px CSS diventa 1.4 volte tanto in px di finestra e il
semaforo finisce in mezzo al logo. Il padding va quindi calcolato come `larghezza / scale`, con
lo stesso `sc` che `WaveDisplay` riceve già come prop, passato anche all'header.

La larghezza da riservare **va misurata sulla finestra vera**, non assunta: il semaforo occupa
all'incirca 70-80 px, ma il numero esatto dipende dalla versione di macOS e va letto dai frame
dei tre `NSButton` (`[window standardWindowButton: NSWindowCloseButton]` e fratelli) invece di
essere scritto a mano nel CSS.

Il padding si applica **solo in Standalone**, che la UI sa dal campo `standalone` di
`getAudioSettings` — nessun canale in più. Logo e bottoni Undo/Redo scorrono a destra. Se
visivamente non regge (l'header è alto 40 px e ha già parecchia roba), l'alternativa è nascondere
il wordmark `XERUM` in Standalone tenendo solo la X: da decidere guardandolo, non ora.

**Il trascinamento nella pagina.** Un handler di `mousedown` sull'`<header>` chiama
`beginWindowDrag` **solo se** `event.target.closest("button, input, [role='slider']")` è nullo:
altrimenti trascini la finestra mentre giri una manopola. Il `dblclick` chiama
`toggleWindowZoom`.

**Il fake backend.** `WebUI/src/juce/fake-backend.ts` deve rispondere a `getAudioSettings` e alle
tre setter, altrimenti la UI nel browser (`pnpm dev`, e i test Vitest che montano `SynthWindow`)
si rompe appena il pannello si apre. Risponde `standalone: false`, che è anche la verità.

## 5. Test e verifica

**La maggior parte di questo lavoro non è copribile da test automatici**: una finestra senza
barra, il semaforo che galleggia, il trascinamento si guardano, non si asseriscono. Quello che
segue separa ciò che i test tengono fermo da ciò che resta a mano.

**Test C++, uno e mirato.** La costruzione del payload va estratta in una funzione **pura** —
prende un `AudioDeviceSetup` e le liste disponibili, restituisce il `juce::var` — testabile senza
aprire nessun device: liste vuote, sample rate fuori dai disponibili, device staccato. È il pezzo
dove un errore sarebbe silenzioso, ed entra in `XerumTests` senza tirarsi dietro tutto
`Source/bridge/*`.

**Test Vitest**, dove sta la logica vera della UI:

1. il pannello mostra i controlli in Standalone e la riga di testo in modalità host;
2. l'errore di `setAudioDeviceSetup` compare invece di essere ingoiato;
3. il padding del semaforo c'è solo in Standalone ed è **diviso per lo scale**;
4. il `mousedown` chiama `beginWindowDrag` sull'header ma **non** se parte da un bottone o da una
   manopola — è il test che vale di più: quell'errore renderebbe la finestra inusabile.

**Verifica manuale**, come voce nuova nella "Bridge checklist" di `docs/build.md`: finestra senza
barra ma semaforo funzionante; trascinamento dall'header e non dalle manopole; doppio clic che
ingrandisce; resize che rispetta ancora il constrainer; cambio di device e di buffer size mentre
suona una nota; interfaccia audio staccata a caldo; riapertura che ritrova device e MIDI scelti.

## Ordine di lavoro

1. **Spike sul trascinamento** — `performWindowDragWithEvent:` con `[NSApp currentEvent]` da una
   native function. Codice da buttare. Decide la sezione 2, e va fatto prima di tutto il resto.
2. App custom e finestra senza chrome (sezione 1), con il semaforo ancora sopra il logo.
3. `AudioSettingsChannel` e la sua funzione pura sotto test (sezione 3).
4. `SettingsOverlay`, spostamento del selettore MIDI, padding del semaforo, drag nella pagina
   (sezione 4).
5. Checklist manuale in `docs/build.md` e passata sulla Standalone Release.
