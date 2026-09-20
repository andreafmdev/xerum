# Import delle wavetable dai preset Serum, e onda vera a schermo

Data: 2026-09-20. Stato: approvato in chat, in attesa del piano.

## Obiettivo

Portare in Xerum le wavetable contenute nel pack *Retro Synthwave Pack 2* (171 file `.fxp` di
Xfer Serum) e, insieme, far disegnare a `WaveDisplay` la **tavola davvero selezionata** invece
della forma sintetica di oggi.

Due lavori distinti che conviene fare insieme: senza il secondo, le tavole nuove sarebbero
indistinguibili a schermo da quelle vecchie.

## Non obiettivi

- **Tradurre i parametri dei preset Serum.** Lo stato Serum è uno struct da 33 872 byte con
  2 oscillatori wavetable, sub, noise, 3 inviluppi, 4 LFO, matrice di modulazione ampia e 10
  slot FX. Xerum ha 1 oscillatore, 1 filtro, 2 inviluppi, 1 LFO, 8 target di modulazione e 3
  FX. Una mappatura sarebbe lossy per costruzione, e non è questo il lavoro.
- **Recuperare le tavole di fabbrica di Serum.** ~140 preset su 171 non portano dati: citano
  per nome 84 tavole Xfer (`/Analog/PWM C64.wav`, `/Analog/Basic Shapes.wav`, …) che nel pack
  non ci sono. Fuori portata, e non nostre.
- **Caricamento di tavole utente a runtime.** Niente I/O su file nel plugin: le tavole
  importate si compilano nel binario come le sei attuali. Un import da interfaccia è un'altra
  feature, con altri problemi (stato, percorsi, tavola mancante alla riapertura).
- **Warp vero nel disegno.** L'oscillator sync resta approssimato in JS come oggi.

## Cosa contiene davvero il pack (misurato, non supposto)

Ogni `.fxp` è un FPCh VST2 (`CcnK` / `FPCh`, uid `XfsX`). Il chunk contiene **uno o due stream
zlib concatenati**:

- stream 0: sempre 33 872 byte, lo stato Serum — scartato;
- stream 1 (quando presente): **float32 raw**, `len / 4 / 2048` frame da 2048 campioni.

Tutti e 171 i file hanno uno stream 1, ma in 141 casi è **vuoto** (0 byte). I 30 restanti
contengono **10 tavole distinte** per sha1; tre sono di 1, 2 e 4 frame — onde singole, non
tavole di morph — e vengono scartate dalla soglia.

Restano **7 tavole** da importare:

| sha1 | frame | preset di origine | slug `.xwt` | etichetta `wtIndex` |
|---|---|---|---|---|
| `56780dc5` | 256 | `BS-RacingDestructionKit5` (+12) | `retro-racing` | Retro Racing |
| `40bbc013` | 256 | `FX-Spindizzy` (+9) | `retro-spindizzy` | Retro Spindizzy |
| `d15054da` | 256 | `FX-GGSisters4` (+1) | `retro-ggsisters` | Retro GG Sisters |
| `8490d094` | 256 | `LD-Commando18` (+1) | `retro-commando` | Retro Commando |
| `b5167ba8` | 278 | `PD-Uridium1` | `retro-uridium-pad` | Retro Uridium Pad |
| `fa38ca0c` | 22 | `FX-Leaderboard0` (+5) | `retro-leaderboard` | Retro Leaderboard |
| `34b742bb` | 22 | `KY-Uridium2` | `retro-uridium` | Retro Uridium |

Tutte hanno già picco 1.000. Costo: 7 × 512 KB ≈ **3,5 MB** di binario. `wtIndex` passa da
6 a **13 opzioni**.

## Licenza — da decidere prima di rilasciare

Le tavole vengono da un pack commerciale di terzi (autore `zak235`, `audio235@gmail.com`).
Compilarle nella build personale va bene; **non sono ridistribuibili in un Xerum pubblicato**
senza permesso scritto dell'autore. `Resources/wavetables/CREDITS.md` registra la provenienza
tavola per tavola, ma la decisione di shipping resta aperta e non è presa qui. Se il permesso
non arriva, le sette tavole vanno rimosse prima della release: è un `git rm` più una riga in
`parameters.json`, non un rollback di architettura.

## 1. `scripts/import-serum-wavetables.mjs`

Sorella di `fetch-wavetables.mjs`. Uso:

```
node scripts/import-serum-wavetables.mjs <cartella-fxp> [--out Resources/wavetables] [--min-frames 8]
```

Passi:

1. per ogni `.fxp`: verifica `CcnK` a 0, `FPCh` a 8, uid `XfsX` a 16; legge `chunkLen` a 56;
2. divide il chunk negli stream zlib concatenati (`inflate` ripetuto finché i due byte
   successivi sono `78 01`), tiene gli stream dall'indice 1 in poi;
3. scarta gli stream vuoti e quelli la cui lunghezza non è multiplo di `2048 * 4`;
4. deduplica per sha1; scarta sotto `--min-frames` (default 8);
5. **riduce a 64 frame** interpolando linearmente lungo l'asse dei frame: il frame `i` di
   uscita si campiona a `src = i * (N - 1) / 63`, mescolando i due frame sorgente adiacenti
   campione per campione. Non decimazione: a 256 → 64 la decimazione butterebbe tre quarti
   del morph;
6. **normalizzazione globale**, un solo fattore per tutta la tavola, la stessa di
   `realign-wavetables.mjs`;
7. scrive `<slug>.xwt`: header `"XWT1"` + `frames` u32 LE + `frameSize` u32 LE + i
   `64 * 2048` float32;
8. stampa per ogni tavola frame sorgente, picco e RMS prima e dopo, e aggiorna `CREDITS.md`.

**Niente riallineamento di fase, niente equalizzazione dell'RMS per frame.** Quei due passaggi
esistono in `realign-wavetables.mjs` perché le AKWF sono onde indipendenti messe in fila: fra
un frame e il successivo la fase salta, e il crossfade cancella a pettine. Una tavola Serum è
già coerente lungo l'asse del morph — riallinearla la ruoterebbe senza motivo e romperebbe il
movimento che l'autore ha scritto. Le metriche stampate al punto 8 servono a verificarlo.

Le funzioni riusabili (interpolazione fra frame, normalizzazione, scrittura `.xwt`) stanno in
`scripts/wavetable-dsp.mjs`, dove sono già le altre, coperte da `wavetable-dsp.test.mjs`.

## 2. Registrazione delle tavole

Nessun codice C++ nuovo. Per ogni tavola:

- un'opzione in `Source/parameters/parameters.json`, in coda a `wtIndex.options`
  (`{ "value": "retro-racing", "label": "Retro Racing" }`);
- una voce in `kWavetableFiles` (`Source/dsp/WavetableStore.h`), **nello stesso ordine**.

`file(GLOB ... CONFIGURE_DEPENDS)` in `CMakeLists.txt` raccoglie i `.xwt` da solo: basta
riconfigurare. Lo `static_assert` in `Source/engine/ParamCollect.h` confronta già le due liste
nome per nome a tempo di compilazione, quindi un disallineamento non compila.

## 3. Preset per nome, non per numero

Un choice si normalizza `x = indice / (nOpzioni - 1)`. Portando `wtIndex` da 6 a 13 opzioni,
ogni valore già scritto punta a un'altra tavola: in `presets.json` `"wtIndex": 1.0` oggi è
`pwm`, dopo l'aggiunta sarebbe `retro-uridium`. Lo `static_assert` non se ne accorge: confronta
nomi di file, non valori di preset.

La correzione è togliere di mezzo il numero. In `Source/parameters/presets.json` i valori dei
parametri **choice** diventano stringhe:

```json
{ "name": "Sub Pulse", "cat": "Bass", "values": {
  "wtIndex": "saws", "ftype": "LP", "slope": "24", "wtpos": 0.1, ... } }
```

I float restano normalizzati 0..1 come oggi: la convenzione non cambia per loro.

`scripts/gen-params.mjs` risolve nome → indice → normalizzato mentre genera `PresetTable.h` e
`presets.generated.ts`, e **fallisce con exit non-zero** se un nome non esiste fra le opzioni
di quel parametro, o se un choice porta ancora un numero. Riguarda `wtIndex`, `ftype` e
`slope`. Il commento `_note` in testa a `presets.json` va riscritto: la formula dei choice non
serve più a chi edita il file.

Nessun impatto su progetti DAW salvati: Xerum è pre-v1 e non è mai stato rilasciato.

## 4. `scripts/build-wavetable-previews.mjs` e l'onda vera

Script separato, non parte dell'importatore: legge **tutti** i `.xwt` in `Resources/wavetables`
— le sei AKWF comprese — e scrive `WebUI/src/synth/wavetables.generated.ts`.

Per ogni tavola: **9 frame × 256 punti**, presi a passo costante lungo i 64 frame e i 2048
campioni, come array di numeri. Nove perché è già la costante `FRAMES` di `WaveDisplay`.
Ordine identico a `wtIndex.options`, con lo slug accanto a ogni voce perché un test possa
confrontare le due liste. Peso: ~18 KB per tavola, ~250 KB per tredici.

Girando sugli stessi `.xwt` che finiscono nel binario, la preview non può divergere dal suono.

In `WaveDisplay.tsx`, `sampleWave(pos, t, warp)` viene sostituita da una lettura della tavola:
i due frame di preview adiacenti a `wtpos`, mescolati per la frazione, campionati in fase `t`
con interpolazione lineare fra i 256 punti. `wtIndex` diventa una dipendenza vera del disegno,
non più solo l'etichetta accanto allo schermo. Il `warp` continua a moltiplicare la fase come
adesso.

`sampleWave` esce da `curves.ts` insieme al suo test: nessun altro la usa.

`PresetOverlay` eredita la miniatura giusta — `presetWave()` già restituisce `wtpos` e `warp`,
gli si aggiunge `wtIndex` letto dal preset (con il default di spec quando il preset non lo
tocca, come fa già per gli altri due).

## Errori

L'importatore fallisce presto e forte, senza scrivere file a metà: magic o uid sbagliati,
chunk non inflatabile, lunghezza non multipla di `2048 * 4`, frame sotto soglia, cartella di
uscita inesistente → messaggio sullo stderr ed exit non-zero.

`gen-params.mjs` fallisce allo stesso modo su un nome di choice sconosciuto.

A runtime non cambia niente: le tavole o sono nel binario e valide, o la build non parte —
`CMakeLists.txt` ha già il `FATAL_ERROR` sulla cartella vuota, e `parseXwt` più lo
`static_assert` coprono il resto.

## Test

- **Node, importatore**: un `.fxp` fixture ridotto (uno stream di 8 frame sintetici) →
  conteggio frame, sha1 e RMS attesi in uscita; un fxp con uid non `XfsX`, uno con chunk
  troncato e uno con stream sotto soglia → errore atteso, nessun file scritto.
- **Node, `wavetable-dsp.mjs`**: la riduzione a 64 frame di una rampa lineare lungo l'asse
  frame resta una rampa lineare (nessuna perdita agli estremi: frame 0 e 63 coincidono con i
  sorgenti 0 e N-1).
- **Node, previews**: `wavetables.generated.ts` ha una voce per ogni `.xwt` presente, nello
  stesso ordine di `wtIndex.options`, con gli stessi slug.
- **C++**: `parseXwt` più `buildMipTable` non-null su **ogni** file di `Resources/wavetables`,
  iterando `kWavetableFiles` invece delle sei tavole elencate a mano di oggi.
- **C++**: ogni preset di `kPresetTable` risolve i propri choice a un indice dentro il numero
  di opzioni di quel parametro.
- **WebUI**: `WaveDisplay` disegna punti diversi per `wtIndex` diversi a parità di `wtpos` e
  `warp` — con la forma sintetica di oggi questo test passerebbe identico per tutte e sei le
  tavole, ed è esattamente il buco che chiude.

## File toccati

| file | cosa |
|---|---|
| `scripts/import-serum-wavetables.mjs` | nuovo |
| `scripts/build-wavetable-previews.mjs` | nuovo |
| `scripts/wavetable-dsp.mjs` + `.test.mjs` | riduzione frame, riuso della normalizzazione |
| `Resources/wavetables/*.xwt` | 7 file nuovi |
| `Resources/wavetables/CREDITS.md` | provenienza e nota di licenza |
| `Source/parameters/parameters.json` | 7 opzioni in `wtIndex` |
| `Source/dsp/WavetableStore.h` | 7 voci in `kWavetableFiles` |
| `Source/parameters/presets.json` | choice per nome, `_note` riscritta |
| `scripts/gen-params.mjs` | risoluzione nome → indice, errore sui nomi ignoti |
| `Source/parameters/PresetValue.h` / `PresetTable.h` | se il tipo generato cambia forma |
| `WebUI/src/synth/wavetables.generated.ts` | nuovo, generato |
| `WebUI/src/synth/curves.ts` + `curves.test.ts` | via `sampleWave` |
| `WebUI/src/synth/ui/WaveDisplay.tsx` | lettura della tavola vera |
| `WebUI/src/synth/presets.ts` | `presetWave` restituisce anche `wtIndex` |
| `Tests/` | i due test C++ sopra |
| `docs/architecture.md` | sezione wavetable: da dove vengono e come si aggiungono |
