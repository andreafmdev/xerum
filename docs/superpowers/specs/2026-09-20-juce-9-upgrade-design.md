# Aggiornamento a JUCE 9

Data: 2026-09-20
Stato: approvato in brainstorming, pronto per il piano di implementazione

## Problema

Il submodule `external/JUCE` è fermo al tag `8.0.6`. L'ultima versione disponibile è
`9.0.2`, una major. Restare indietro di una major su un framework che fornisce l'audio, la
GUI, i formati plugin e il sistema di build significa accumulare un debito che cresce da
solo: ogni mese che passa allunga il salto e rende più difficile distinguere una rottura
causata dall'aggiornamento da una causata dal proprio codice.

Insieme all'aggiornamento c'è un debito più piccolo ma più insidioso da chiudere. Il
pacchetto JavaScript che fa parlare la WebUI col plugin — le funzioni `getSliderState`,
`getToggleState`, `getComboBoxState`, `getNativeFunction` — è oggi **una copia a mano**
dentro `WebUI/src/juce/vendor/juce-frontend/`: un `index.js` da 17 KB preso da JUCE, un
`index.d.ts` scritto da noi perché l'originale non aveva tipi, e un
`check_native_interop.js` che non importa nessuno.

Quella copia può divergere dal binario con cui parla. Se qualcuno aggiornasse il submodule
senza ricopiare il vendor, la UI parlerebbe un protocollo diverso da quello del plugin, e
nessun test se ne accorgerebbe: il difetto comparirebbe come un malfunzionamento dentro un
DAW, lontano dalla sua causa.

## Obiettivo

Portare il submodule a `9.0.2` e sostituire la copia vendorizzata con il pacchetto
ufficiale, preso direttamente dal submodule, in modo che interop e binario siano per
costruzione lo stesso commit.

## Non obiettivi

- **Non si aggiunge CI.** Il progetto non ha `.github/workflows` e la sua assenza è un
  problema reale, ma è un progetto a sé: infilarlo qui renderebbe impossibile dire se un
  fallimento viene dall'aggiornamento o dall'infrastruttura nuova.
- **Non si passa a C++20** né si tocca nessuna altra impostazione di linguaggio, salvo che
  JUCE 9 lo imponga — e in quel caso vale la regola di arresto qui sotto.
- **Non si esegue il debito di prove manuali** accumulato dal branch precedente. Resta
  dell'utente, per sua decisione esplicita.

## Cosa si è accertato prima di decidere

Non sono supposizioni: vengono dalla lettura di `BREAKING_CHANGES.md` nel tag `9.0.2` e dal
codice di questo repository.

**Fra 8.0.6 e 9.0.2 ci sono 35 modifiche rompenti dichiarate. Tre toccano questo progetto,
e due si annullano da sole.**

| Modifica | Impatto qui |
|---|---|
| `AudioProcessor::createEditor()` è diventato privato; i chiamanti usano `createEditorAndMakeActive()` | **Nessuno.** In questo progetto `createEditor()` è solo un `override` (`PluginProcessor.h:34`, `PluginProcessor.cpp:339`) e nessuno lo chiama direttamente. |
| `libxi-dev` obbligatorio per compilare su Linux | **Nessuno.** Progetto macOS, nessuna CI Linux. |
| Il pacchetto di interop della WebView si è spostato da `modules/juce_gui_extra/native/javascript` a `modules/juce_gui_extra/native/typescript/webview-interop`, riscritto in TypeScript e pubblicato come `@juce-framework/webview` | **È il lavoro di questo branch.** |

**Nessuna riga impone C++20.** Il progetto resta a `CMAKE_CXX_STANDARD 17`. L'assenza di una
riga non è però una prova: vedi la regola di arresto.

**Il submodule non ha patch locali.** `git -C external/JUCE status` è pulito. La ricerca di
settembre (`memory/xerum-webview-risks`) segnalava che il workaround comunitario per il
furto di focus della tastiera è «una patch a JUCE da riapplicare a ogni update»: non è mai
stata applicata, quindi non c'è niente da riportare.

**Il pacchetto ufficiale esiste e combacia quasi del tutto.** `@juce-framework/webview`
1.0.0, doppia licenza `AGPL-3.0-only OR LicenseRef-JUCE` — le stesse condizioni sotto cui il
progetto già linka JUCE, quindi nessun vincolo nuovo. Ha `dist/` committato dentro il tag,
con `index.js` e `index.d.ts`, quindi non richiede un passo di build.

**L'unico punto di attrito:** il pacchetto esporta `getNativeFunction`, `getSliderState`,
`getToggleState`, `getComboBoxState`, `getBackendResourceAddress` e
`ControlParameterIndexUpdater`. Dichiara `ListenerList`, `SliderState`, `ToggleState` e
`ComboBoxState` come classi **ma non le esporta**. Il nostro `juce-backend.ts` usa tutte e
quattro come annotazioni di tipo (`:24`, `:33`, `:47`, `:57`). Vanno derivate, non riscritte.

## La regola di arresto

Se compilando risulta che JUCE 9 pretende C++20, **il lavoro si ferma e lo scope si
ridiscute**. Non è più un aggiornamento di dipendenza: è un cambio di standard del
linguaggio che attraversa i 37 file che includono JUCE, con le sue implicazioni su
compilatore minimo e comportamento. Portarlo dentro questo branch senza dirlo
significherebbe consegnare un lavoro diverso da quello approvato.

## Progettazione

### Il pin

`external/JUCE` passa da `8.0.6` a `9.0.2`. La sezione C++ del lavoro è, sulla carta, vuota.

"Sulla carta" è la parte che conta: `BREAKING_CHANGES.md` elenca ciò che gli autori hanno
scelto di dichiarare, non le deprecazioni rimosse in silenzio, i cambi di comportamento o le
differenze nell'API CMake. **Il piano deve trattare la prima compilazione come scoperta, non
come conferma.** Si sposta il pin, si riconfigura, si costruisce ogni target, e ciò che esce
dal compilatore definisce il lavoro rimanente.

### Lo scambio del pacchetto

Si cancella `WebUI/src/juce/vendor/juce-frontend/` per intero: `index.js`, `index.d.ts`,
`README.md` e `check_native_interop.js` — quest'ultimo verificato non importato da nessuno
fuori dalla directory stessa.

In `WebUI/package.json`:

```json
"@juce-framework/webview": "link:../external/JUCE/modules/juce_gui_extra/native/typescript/webview-interop"
```

Il percorso è relativo a `WebUI/` ed è stato verificato. `link:` crea un symlink: nessuna
copia, nessun passo di build, e la versione dell'interop è per costruzione quella del
commit di JUCE a cui il submodule è fermo.

In `WebUI/src/juce/juce-backend.ts` cambiano tre cose:

1. L'import dinamico punta al pacchetto invece che al vendor. **Resta dinamico**: la ragione
   scritta nel commento — il modulo legge `window.__JUCE__` al caricamento — vale identica
   per il pacchetto ufficiale.
2. I quattro tipi si derivano da ciò che è esportato, invece di essere riscritti a mano:

   ```ts
   import type { getSliderState, getToggleState, getComboBoxState } from "@juce-framework/webview";

   type SliderState = ReturnType<typeof getSliderState>;
   type ToggleState = ReturnType<typeof getToggleState>;
   type ComboBoxState = ReturnType<typeof getComboBoxState>;
   type ListenerList = SliderState["valueChangedEvent"];
   ```

   Un `import type` non emette codice a runtime, quindi convive con l'import dinamico senza
   caricare il modulo prima del tempo.
3. Le annotazioni `Juce.SliderState` e sorelle diventano i tipi locali derivati.

`window.__JUCE__` **non si tocca**: è dichiarato da noi in `juce-backend.ts:13-20`, non viene
dal pacchetto.

### Due cose che possono mordere

Entrambe vanno trattate nel piano come scoperta, con una via d'uscita già pensata.

- **Vite e le dipendenze symlinkate.** Un pacchetto collegato fuori dalla radice del progetto
  a volte richiede `server.fs.allow` o un giro di `optimizeDeps`. Se succede, la correzione
  vive in `WebUI/vite.config.ts` e va commentata con la ragione.
- **`dist/index.js` è codice nuovo.** È stato riscritto da TypeScript, non è il file che
  giravamo prima. La rete di sicurezza è `WebUI/src/juce/juce-backend.test.ts`, che esercita
  l'intero percorso del backend contro lo stub — compresi i quattro test di inoltro
  argomenti delle native function MIDI.

### Il costo nuovo

Da qui in poi **`pnpm install` pretende il submodule inizializzato**. La build C++ già lo
pretendeva (`add_subdirectory(external/JUCE)`), la build web no. Va scritto in
`docs/build.md`, dove oggi si legge «JUCE is pinned under `external/JUCE` (tag `8.0.6`)»
(`docs/build.md:14`), riga che va aggiornata insieme.

## Verifica

I cancelli automatici sono `pnpm test`, `pnpm ui:test`, `pnpm typecheck`, `pnpm build`,
`pnpm --filter @xerum/ui check:colors`, la build di ogni target C++ e `XerumTests`. Tre di
loro meritano di essere spiegati, perché provano cose che non si deducono dal nome:

- **La build di tutti i target** (VST3, AU, Standalone, `XerumTests`) è l'unico posto dove la
  superficie C++ di JUCE 9 viene esercitata davvero.
- **`XerumTests`** non compila `Source/bridge/*` e non linka la WebView, quindi non dice
  niente sull'interop — ma con 1179 suite, e le non-regressioni bit-exact di glide e
  arpeggiatore, dice che il motore suona ancora come prima sotto una major nuova.
- **`pnpm typecheck`** è il vero test di compatibilità dello scambio: se il pacchetto
  ufficiale cambiasse forma, i tipi derivati smetterebbero di combaciare e il typecheck
  fallirebbe. Non è igiene, è il guardiano dell'API.

### Il limite noto

**Nessun cancello automatico prova che la UI parli ancora col plugin.** Non esiste una
verifica che apra la WebView: il solo modo di saperlo è aprire il plugin e muovere una
manopola. È il rischio reale di questo branch.

Va letto insieme al debito che lo precede: le dieci prove manuali del branch della striscia
bassa non sono mai state eseguite, e l'utente ha scelto esplicitamente di prenderle in
carico fuori da questo lavoro. Questo aggiornamento quindi **non ha una linea di base
verificata a cui confrontarsi**. Se dopo l'aggiornamento qualcosa nella UI non funziona, non
sarà immediatamente attribuibile a JUCE 9. È una conseguenza accettata, non una svista.

### Rientro

È un pin e una dipendenza. Si riporta il submodule a `8.0.6` e si revoca il commit. Nessuna
migrazione di dati, nessun formato di stato salvato che cambia, nessuna conversione.

## Ordine di costruzione

Due fasi, separate perché falliscono per ragioni diverse e vanno potute attribuire.

1. **Il pin.** Submodule a `9.0.2`, riconfigurazione, build di ogni target, `XerumTests`
   verde. Il vendor resta al suo posto e la WebUI non si tocca: la copia vendorizzata è un
   file autonomo dentro questo repository, quindi lo spostamento del pacchetto dentro JUCE
   non la tocca e continua a funzionare mentre il pin si muove. Se qualcosa si rompe in
   questa fase è C++ o CMake, e nient'altro. Si applica la regola di arresto se compare
   C++20.
2. **Lo scambio del pacchetto.** Vendor cancellato, dipendenza `link:`, tipi derivati, tutti
   i cancelli web. Se qualcosa si rompe qui, è l'interop.

I documenti si aggiornano nella fase che li rende falsi.
