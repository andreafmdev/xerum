# Aggiornamento a JUCE 9 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Portare `external/JUCE` da `8.0.6` a `9.0.2` e sostituire la copia vendorizzata del pacchetto di interop WebView col pacchetto ufficiale, preso direttamente dal submodule.

**Architecture:** Due fasi separate perché falliscono per ragioni diverse e la causa deve restare attribuibile. La prima muove solo il pin e non tocca la WebUI: se qualcosa si rompe è C++ o CMake. La seconda scambia il pacchetto e non tocca il C++: se qualcosa si rompe è l'interop.

**Tech Stack:** JUCE 9.0.2 (C++17, CMake) come submodule git; WebUI in TypeScript con Vite, pnpm 11, Vitest; `@juce-framework/webview` 1.0.0 consumato via `link:` dal submodule.

**Spec:** `docs/superpowers/specs/2026-09-20-juce-9-upgrade-design.md`

## Global Constraints

- Il tag `9.0.2` è **già presente localmente** nel submodule: nessun `git fetch` necessario. Verificabile con `git -C external/JUCE rev-parse --verify 9.0.2`.
- C++17. `CMAKE_CXX_STANDARD` resta `17`: non si tocca.
- **Regola di arresto:** se la compilazione rivela che JUCE 9 pretende C++20, ci si ferma, non si converte niente, e si riporta il fatto al controller. È un cambio di standard del linguaggio su 37 file, non un aggiornamento di dipendenza, ed è fuori dallo scope approvato.
- Aggiungere o togliere un file sotto `Source/` richiede `cmake --preset macos-debug`. Cambiare il pin del submodule lo richiede comunque.
- Test C++: `cmake --build --preset macos-debug --target XerumTests && build/macos-debug/XerumTests_artefacts/Debug/XerumTests` → deve stampare `ALL TESTS PASSED`.
- Gate web: `cd WebUI && pnpm test`, `pnpm ui:test`, `pnpm typecheck`, `pnpm build`, `pnpm --filter @xerum/ui check:colors`.
- pnpm 11. Mai npm, mai yarn.
- Commenti in italiano, come il resto del codice. Messaggi di commit in inglese.
- MAI eseguire comandi `terraform` o `aws`. Regola assoluta di questo repository.

---

## Struttura dei file

**Modificati**
- `external/JUCE` — puntatore del submodule, da `8.0.6` a `9.0.2`
- `WebUI/package.json` — la dipendenza `link:` al pacchetto di interop
- `WebUI/src/juce/juce-backend.ts` — l'unico file che tocca il vendor: import ai righi 9 e 75, annotazioni di tipo ai righi 24, 33, 47, 57, commenti ai righi 1, 74, 80
- `docs/build.md` — il tag pinnato (riga 14) e il nuovo requisito di `pnpm install`

**Cancellati**
- `WebUI/src/juce/vendor/juce-frontend/index.js`
- `WebUI/src/juce/vendor/juce-frontend/index.d.ts`
- `WebUI/src/juce/vendor/juce-frontend/check_native_interop.js`
- `WebUI/src/juce/vendor/juce-frontend/README.md`

**Non toccati, e va detto perché**
- `Source/**` — nessuna delle tre modifiche rompenti che riguardano questo progetto richiede una riga di C++ (vedi la spec). Se la compilazione dice il contrario, è una scoperta da riportare, non da correggere in silenzio.
- `WebUI/src/juce/juce-backend.test.ts` — installa il proprio stub su `window.__JUCE__` e non importa mai il pacchetto per nome. È la rete di sicurezza dello scambio, non il suo bersaglio.
- `WebUI/vite.config.ts` — si tocca **solo se** il gate lo impone (vedi Task 2, passo 7).

---

## Task 1: Il pin

Questa task non tocca un solo file della WebUI. Il vendor resta al suo posto e continua a funzionare: è un file autonomo dentro questo repository, quindi lo spostamento del pacchetto *dentro* JUCE non lo riguarda.

**Files:**
- Modify: `external/JUCE` (puntatore del submodule)
- Modify: `docs/build.md:14`

**Interfaces:**
- Produces: un albero che compila e passa i test con JUCE 9.0.2, su cui la Task 2 costruisce.

- [ ] **Step 1: Registra la linea di base, prima di muovere qualsiasi cosa**

Serve a rendere attribuibile ogni fallimento successivo. Se uno di questi è già rosso **prima** dell'aggiornamento, fermati e riportalo: staresti per attribuire a JUCE 9 una rottura che c'era già.

```bash
cmake --build --preset macos-debug 2>&1 | tail -5
build/macos-debug/XerumTests_artefacts/Debug/XerumTests 2>&1 | tail -1
cd WebUI && pnpm test 2>&1 | grep -E "Test Files|Tests " && pnpm typecheck && cd ..
```

Atteso: `BUILD SUCCEEDED`, `ALL TESTS PASSED`, i test web verdi, typecheck pulito. Annota i numeri nel report.

- [ ] **Step 2: Sposta il pin**

```bash
git -C external/JUCE rev-parse --verify 9.0.2
git -C external/JUCE checkout 9.0.2
git -C external/JUCE describe --tags
```

Atteso: l'ultima riga stampa `9.0.2`.

- [ ] **Step 3: Riconfigura**

```bash
cmake --preset macos-debug 2>&1 | tail -20
```

Atteso: configurazione senza errori. **Questo è il primo punto di scoperta**: se l'API CMake di JUCE è cambiata, si manifesta qui. Riporta qualsiasi warning nuovo.

- [ ] **Step 4: Costruisci ogni target — è qui che si scopre il lavoro vero**

```bash
cmake --build --preset macos-debug 2>&1 | tail -40
```

Atteso, nel caso previsto: `BUILD SUCCEEDED`.

Se invece fallisce, **non correggere nulla prima di aver classificato l'errore**:

- **Errore che nomina C++20, `std=c++20`, o una feature di C++20** → applica la regola di arresto: fermati, non convertire niente, riporta l'errore testuale al controller.
- **Deprecazione rimossa o firma cambiata in un punto localizzato** → è il caso previsto dalla spec. Correggilo, documenta nel report quale simbolo è cambiato e come, e prosegui.
- **Qualsiasi altra cosa** → riporta prima di agire.

Riporta anche i warning nuovi: il progetto ne ha 4 preesistenti in `EngineTests.cpp` (switch non esaustivo su `params::ParamSlot`, due lambda con cattura di `this` inutilizzata) e uno nel bundle vendor del web. Quelli non sono tuoi. Tutti gli altri sì.

- [ ] **Step 5: Esegui i test C++**

```bash
build/macos-debug/XerumTests_artefacts/Debug/XerumTests 2>&1 | tail -1
```

Atteso: `ALL TESTS PASSED`.

Questa suite non compila `Source/bridge/*` e non linka la WebView, quindi non dice niente sull'interop — ma con 1179 suite, fra cui le non-regressioni bit-exact di glide e arpeggiatore, dice che il motore suona ancora come prima sotto una major nuova. Se qui qualcosa cambia, è un cambio di comportamento in `juce::dsp` o `juce_audio_basics` ed è la scoperta più importante che questa task possa fare: riportala per esteso.

- [ ] **Step 6: Verifica che il web sia rimasto indifferente**

```bash
cd WebUI && pnpm test 2>&1 | grep -E "Test Files|Tests " && pnpm typecheck && cd ..
```

Atteso: identico allo Step 1. Questa task non ha toccato la WebUI; se qualcosa qui cambia, qualcosa non torna.

- [ ] **Step 7: Aggiorna il tag nella documentazione**

In `docs/build.md`, riga 14, sostituisci:

```
JUCE is pinned under `external/JUCE` (tag `8.0.6`).
```

con:

```
JUCE is pinned under `external/JUCE` (tag `9.0.2`).
```

- [ ] **Step 8: Commit**

```bash
git add external/JUCE docs/build.md
git commit -m "Move the JUCE submodule to 9.0.2"
```

---

## Task 2: Lo scambio del pacchetto di interop

**Files:**
- Modify: `WebUI/package.json` (blocco `dependencies`)
- Modify: `WebUI/src/juce/juce-backend.ts`
- Modify: `docs/build.md` (sezione "Web UI")
- Delete: `WebUI/src/juce/vendor/juce-frontend/` (quattro file)

**Interfaces:**
- Consumes: il submodule a `9.0.2` (Task 1), che contiene il pacchetto in `modules/juce_gui_extra/native/typescript/webview-interop`.
- Produces: `juce-backend.ts` che importa `@juce-framework/webview`, con i tipi `SliderState`, `ToggleState`, `ComboBoxState`, `ListenerList` derivati localmente.

- [ ] **Step 1: Cancella il vendor e guarda il typecheck fallire**

Questo è il rosso del ciclo: prova che il codice dipendeva davvero dal vendor e che lo scambio non è cosmetico.

```bash
git rm -r WebUI/src/juce/vendor/juce-frontend
cd WebUI && pnpm typecheck; cd ..
```

Atteso: FALLISCE con due errori in `src/juce/juce-backend.ts`, ai righi 9 e 75, che non trovano il modulo `./vendor/juce-frontend/index`.

- [ ] **Step 2: Aggiungi la dipendenza**

In `WebUI/package.json`, nel blocco `dependencies`, in ordine alfabetico **prima** di `@xerum/ui`:

```json
    "@juce-framework/webview": "link:../external/JUCE/modules/juce_gui_extra/native/typescript/webview-interop",
```

Il percorso è relativo a `WebUI/` ed è verificato. Poi:

```bash
cd WebUI && pnpm install 2>&1 | tail -10; cd ..
```

Atteso: installazione pulita, e `WebUI/node_modules/@juce-framework/webview` è un symlink verso il submodule. Verificalo:

```bash
ls -l WebUI/node_modules/@juce-framework/webview
ls WebUI/node_modules/@juce-framework/webview/dist/
```

Atteso: il primo mostra una freccia verso `external/JUCE/...`; il secondo elenca `index.js` e `index.d.ts`.

- [ ] **Step 3: Sostituisci l'import dei tipi**

In `WebUI/src/juce/juce-backend.ts`, sostituisci la riga 9:

```ts
import type * as Juce from "./vendor/juce-frontend/index";
```

con:

```ts
import type { getSliderState, getToggleState, getComboBoxState } from "@juce-framework/webview";

/**
 * Il pacchetto dichiara SliderState, ToggleState, ComboBoxState e ListenerList come classi
 * ma **non le esporta**: esporta solo le funzioni che le restituiscono. I tipi si derivano
 * quindi da quelle, invece di riscriverli a mano come faceva il nostro vecchio index.d.ts —
 * cosi' restano legati al pacchetto e cambiano insieme a lui.
 */
type SliderState = ReturnType<typeof getSliderState>;
type ToggleState = ReturnType<typeof getToggleState>;
type ComboBoxState = ReturnType<typeof getComboBoxState>;
type ListenerList = SliderState["valueChangedEvent"];
```

`import type` non emette codice a runtime, quindi convive con l'import dinamico dello Step 5 senza caricare il modulo prima del tempo.

- [ ] **Step 4: Aggiorna le quattro annotazioni**

Sempre in `juce-backend.ts`, togli il prefisso `Juce.` dai quattro siti — righi 24, 33, 47 e 57 nel file originale:

| Prima | Dopo |
|---|---|
| `(list: Juce.ListenerList, subs: Subs)` | `(list: ListenerList, subs: Subs)` |
| `constructor(private st: Juce.SliderState)` | `constructor(private st: SliderState)` |
| `constructor(private st: Juce.ToggleState)` | `constructor(private st: ToggleState)` |
| `constructor(private st: Juce.ComboBoxState, private id: ParamId)` | `constructor(private st: ComboBoxState, private id: ParamId)` |

Dopo questo passo la stringa `Juce.` non deve più comparire nel file. Verifica: `grep -c "Juce\." WebUI/src/juce/juce-backend.ts` deve stampare `0`.

- [ ] **Step 5: Sposta l'import dinamico**

Sostituisci le righe 74-75:

```ts
  // Import dinamico: il modulo vendor legge window.__JUCE__ al caricamento.
  const juce = await import("./vendor/juce-frontend/index.js");
```

con:

```ts
  // Import dinamico, e resta tale: il modulo legge window.__JUCE__ al caricamento.
  const juce = await import("@juce-framework/webview");
```

- [ ] **Step 6: Correggi i due commenti rimasti falsi**

Riga 1, `JUCE 8` non è più vero:

```ts
// Implementazione di Backend sopra i relay JUCE 9: ogni parametro del plugin ha
```

Riga 80 circa, non esiste più un "bundle vendor":

```ts
  // removeEventListener del bundle upstream è un no-op: registrando un
```

- [ ] **Step 7: Esegui tutti i gate web**

```bash
cd WebUI && pnpm typecheck && pnpm test 2>&1 | grep -E "Test Files|Tests " && pnpm ui:test 2>&1 | grep -E "Test Files|Tests " && pnpm build 2>&1 | tail -5 && pnpm --filter @xerum/ui check:colors && cd ..
```

Atteso: typecheck pulito, test allo stesso numero dello Step 1 della Task 1, build e `check:colors` verdi.

**`pnpm typecheck` è il vero test di questa task.** Se il pacchetto ufficiale avesse una forma diversa da quella prevista, i quattro tipi derivati non combacerebbero e fallirebbe qui. Non è igiene: è il controllo di compatibilità dell'API.

**Se `pnpm build` o `pnpm test` falliscono per la risoluzione del modulo symlinkato**, è il caso previsto dalla spec: Vite a volte pretende `server.fs.allow` per una dipendenza collegata fuori dalla radice del progetto, o un giro di `optimizeDeps`. Aggiungi la correzione minima in `WebUI/vite.config.ts` **con un commento che dica perché**, e riportalo: non è un dettaglio di configurazione, è la ragione per cui quella riga esiste. Se invece i gate passano, non toccare `vite.config.ts`.

- [ ] **Step 8: Documenta il costo nuovo**

In `docs/build.md`, nella sezione "Web UI", sotto `Requires pnpm 11 (corepack enable).`, aggiungi:

```
Also requires the JUCE submodule to be initialised: `WebUI` links the WebView interop
package straight out of `external/JUCE`, so `pnpm install` fails without it. The C++ build
already required the submodule; the web build now does too.
```

- [ ] **Step 9: Commit**

La cancellazione del vendor e' gia' in stage dal `git rm` dello Step 1, quindi qui non va
ri-aggiunta. `WebUI/vite.config.ts` compare in questo elenco **solo se** lo Step 7 ti ha
costretto a toccarlo.

```bash
git add WebUI/package.json WebUI/pnpm-lock.yaml WebUI/src/juce/juce-backend.ts docs/build.md
git status --short
git commit -m "Take the WebView interop package from the submodule"
```

`git status --short` prima del commit serve a vedere con i propri occhi che i quattro file
del vendor risultano cancellati (`D`) e che non e' rimasto niente di non voluto.

---

## Self-review

**Copertura della spec**

| Sezione della spec | Task |
|---|---|
| Pin da 8.0.6 a 9.0.2 | 1 |
| La compilazione come scoperta, non conferma | 1, step 3-4 |
| Regola di arresto su C++20 | Global Constraints + 1, step 4 |
| Cancellazione del vendor (quattro file) | 2, step 1 |
| Dipendenza `link:` dal submodule | 2, step 2 |
| Tipi derivati invece che riscritti | 2, step 3-4 |
| Import dinamico che resta dinamico | 2, step 5 |
| `window.__JUCE__` non si tocca | non è un passo: nessuno step lo modifica, ed è dichiarato fra i file non toccati |
| Vite e le dipendenze symlinkate | 2, step 7 |
| `dist/index.js` è codice nuovo, rete = juce-backend.test.ts | 2, step 7 |
| `pnpm install` pretende il submodule | 2, step 8 |
| Aggiornamento del tag in build.md | 1, step 7 |
| Gate: build, XerumTests, typecheck, test, ui:test, build, check:colors | 1 step 4-6, 2 step 7 |
| Rientro (pin indietro + revert) | non è una task: è una procedura d'emergenza, resta nella spec |

**Consumo di `dist` e non di `src`:** la spec lo vieta esplicitamente e ne dà la ragione. Il piano non lo ripete come passo perché nessuno step lo propone; l'istruzione vive nella spec, che l'esecutore legge insieme al piano.

**Nomi verificati end-to-end**

`getSliderState` / `getToggleState` / `getComboBoxState` sono esportati dal pacchetto (verificato in `dist/index.d.ts` del tag `9.0.2`) → i tipi derivati `SliderState` / `ToggleState` / `ComboBoxState` / `ListenerList` sostituiscono uno a uno `Juce.SliderState` / `Juce.ToggleState` / `Juce.ComboBoxState` / `Juce.ListenerList` ai quattro siti d'uso. `getNativeFunction`, usata al rigo 78 tramite `juce.getNativeFunction`, è anch'essa esportata e non cambia forma.
