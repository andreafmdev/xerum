# Confronto con i synth open source — risultati e priorità

Ricerca del 2026-09-19 su Vital, Surge XT, Helm e Odin 2, in quattro aree: voce e modulazione, qualità del segnale, FX e arpeggiatore, plumbing dei parametri e preset.

## Regola di licenza

Xerum non ha ancora scelto una licenza e tiene aperta l'opzione commerciale a sorgente chiuso. Vital, Surge XT, Helm e Odin 2 sono **GPLv3**: da lì si prende *come* affrontano un problema, mai il codice.

Le licenze verificate leggendo i file, non i README né i metadata di GitHub — che oggi si sono rivelati sbagliati o inutili **quattro volte**:

| fonte | licenza reale | note |
|---|---|---|
| `Chowdhury-DSP/chowdsp_utils` | **per modulo** | `chowdsp_waveshapers` (ADAA), `chowdsp_filters`, `chowdsp_dsp_utils`, `chowdsp_reverb` sono **GPLv3**. Solo `math`, `simd`, `core`, `buffers`, `parameters`, `serialization` sono BSD-3 |
| `jatinchowdhury18/ADAA` | BSD-3-Clause | repo separato, auto-contenuto |
| `Signalsmith-Audio/dsp` | MIT | `delay.h` (Lagrange, Kaiser-sinc), `rates.h` (oversampler), `mix.h` (Householder) |
| `electro-smith/DaisySP` | MIT (dal `LICENSE`; GitHub dice `NOASSERTION`) | ha `chorus`, **non ha riverbero** |
| `electro-smith/DaisySP-LGPL` | LGPL-2.1 | `ReverbSc` è qui: link statico in un VST3 chiuso è un problema |
| `FigBug/Gin` | BSD-3 | `gin_platereverb.h` ha una **MIT propria**, © Mike Jarmy |
| `surge-synthesizer/sst-plugininfra` | MIT (dall'header) | `patch_base.h`; ma include un header di `sst-basic-blocks`, che è GPL3 |
| `sst-filters`, `sst-waveshapers`, `sst-effects` | GPL-3.0 | niente da copiare |
| `external/JUCE/examples/.../ArpeggiatorPluginDemo.h` | ISC | pattern adottabile |

## Le priorità, unificate

### Subito — costo basso, nessuna dipendenza

**1. Cambiare la curva di `saturate()`.** `clamp(x, ±1.5)` + cubica ha uno spigolo nella derivata seconda. L'approssimante di Padé di tanh, `x(27+x²)/(27+9x²)`, ha derivata prima e seconda nulle al raccordo. Misurato: **+80 dB** di alias a MIDI 84 con drive 6 dB, +157 dB a MIDI 60, +30 dB a drive 12. Costo CPU: **+0.12 ns/campione**. Attenzione alla misura: vettorizzata le due curve sono indistinguibili (0.61 contro 0.60 ns), ed è il numero che questo documento riportava prima. Ma dentro `render()` il filtro introduce una dipendenza seriale fra campioni consecutivi, quindi il caso reale è quello scalare, dove si vede la differenza: 0.63 contro 0.75 ns. Resta trascurabile — 16 voci × 8 copie di unison fanno 0.7 ms per secondo di audio, lo 0.07% di un core — ma è diversa da zero. `std::tanh`, misurato con la stessa DFT, è **10 dB peggio** del Padé a ogni drive usabile e costa 1.14 ns: il Padé sopra 3 è esattamente piatto, tanh continua a curvare. Surge spedisce la stessa identica formula (`wst_soft`), senza ADAA. *Non è neutro*: satura prima e più dolcemente, `drive` va ritarato e i 12 preset riascoltati. **[CODICE ADOTTABILE — DaisySP, MIT; è anche matematica pubblica]**

**2. Sotto-fette di render a lunghezza massima fissa (32 o 64 campioni).** Oggi le fette sono delimitate solo dagli eventi MIDI, quindi il tasso di modulazione **lo decide l'host**: 375 Hz con buffer da 128, **47 Hz con 1024**. A 47 Hz un LFO a 8 Hz ha meno di sei punti per ciclo. Surge ha `BLOCK_SIZE = 32` (1500 Hz garantiti), Vital `kMaxBufferSize = 128`. Il ciclo esiste già, va solo spezzato ulteriormente. Misurare la CPU prima di scegliere fra 32 e 64. **[TECNICA]**

**3. Generalizzare `ParameterSeamTests` a un ciclo su `kTable`.** Oggi copre 8 parametri scelti a mano su 48. Prerequisito: **generare la mappatura id→slot**, oggi scritta a mano in tre posti (`ParamSlot.h`, `PluginProcessor.cpp`, e il `RealAccessor` del test stesso — cioè il test verifica il proprio cablaggio). **[TECNICA]**

*Correzione, emersa implementandolo.* Una prima versione di questa voce diceva che un ciclo su "`getRawParameterValue` al default vale il default dichiarato" avrebbe preso il bug il primo giorno. **È falso**: per `oct` il grezzo era 0 e il default dichiarato è 0, quindi quel ciclo passa sia col bug sia senza. Guarda il lato APVTS, mentre il difetto stava nella conversione a valle. Nessun ciclo generico può morderlo senza o nominare i campi di `EngineParams` uno per uno, o **far condividere a motore e test la stessa funzione di conversione** — che è la strada presa: una `naturalFromRaw` sola, usata da `collectEngineParams` e confrontata dal test contro i default di `parameters.json` su tutti i valori discreti. I test mirati scritti a mano restano, perché coprono il caso in cui la conversione venga reintrodotta *in linea* aggirando la funzione condivisa.

**4. `static_assert` che tutti i `kModTargets` siano `Kind::Float`.** Oggi `engine/ModMatrix.h` non ha nessuna guardia sul kind: aggiungere `ParamSlot::oct` a quella lista compila e **riproduce il bug delle quattro ottave**, perché `modulated()` clampa a 0..1 assumendo `modBase` normalizzato. Tre righe.

**5. Reset ai default prima di `replaceState`.** `setStateInformation` non lo fa: un parametro assente da uno stato salvato conserva il valore corrente invece di tornare al default. Tutti e tre i synth studiati iterano sullo schema corrente e pescano dal file — il verso dell'iterazione dà gratis "aggiunto → default, rimosso → ignorato, nessun residuo". Dieci righe.

**6. Versione di schema sulla radice dello stato**, distinta dalla versione del plugin. Oggi `kVersion` vive dentro il figlio `MODS`: non c'è un posto da cui una migrazione possa ramificare. Due righe, **ma retroattivamente non si può fare**.

**7. Spostare gli `SmoothedValue` a monte della modulazione.** Oggi smussiamo il valore *già modulato*: lo smoother a 20 ms è un passa-basso a ~8 Hz, quindi attenua di 3 dB una route LFO a 8 Hz e di 8 dB a 20 Hz. **Stiamo filtrando la nostra stessa modulazione.** Vital smussa il valore base e somma dopo (`createBaseModControl`). Cambio di ordine, non di algoritmo. **[TECNICA]**

**8. Secondo inviluppo come sorgente `env2`.** Oggi `env` nel matrix è l'inviluppo d'ampiezza, quindi `env → cutoff` è costretto a seguire la forma dell'ampiezza: il classico filtro che apre e chiude mentre la nota tiene non è esprimibile. Vital ne ha sei, Surge due dedicati. `dsp::ADSREnvelope` è riusabile com'è. **[TECNICA]**

### Poi — costo medio

**9. Integratore non lineare nell'SVF, e ritaratura congiunta del gain staging.** L'attenuazione d'ingresso scambia banda passante contro picco **esattamente 1:1 in dB per qualunque esponente**: è aritmetica, non taratura, e per questo l'idea dell'esponente variabile con Q non funziona. Quello che rompe il cambio è una nonlinearità *dentro l'anello*. Sostituendo `s1 = g·hp + bp` con `s1 = sat(g·hp + bp)`: **zero perdita in banda a ogni Q, 14 dB di picco in meno a Q 24**. Surge fa la stessa cosa in produzione (`SVFLP12Aquad`: `R = max(0.1, 1 − ClipDamp·B²)`). **[TECNICA]**

*Correzione, emersa implementandolo.* Questa voce diceva anche che la mossa "libera il margine speso alzando `kVoiceHeadroomGain` e riapre la soglia di soft clip più bassa". **Misurato: non lo fa.** In materiale polifonico il picco presentato al clipper lo fissa la somma a banda larga delle voci, non un seno fermo sulla risonanza — ed è proprio quella somma che l'attenuazione d'ingresso smorzava fino a 3.8 dB. Togliendo l'attenuazione lo strumento diventa **più forte** ai `res` intermedi (+1.4 dB di picco a metà corsa), e solo a fondo corsa il picco scende, di 0.8 dB. Chi farà la ritaratura del gain staging parta da questi numeri, non dai −14 dB del picco risonante.

*Secondo costo, da mettere in conto.* La saturazione dentro l'anello **genera alias**, e l'argomento "il segnale è già passa-bassato" regge solo sotto i ~2 kHz di cutoff, dove i prodotti ripiegati stanno a −90 dB e sotto. Sopra gli 8 kHz il percorso `hp` porta l'ingresso quasi intatto nel saturatore. Una prima misura con un **seno a fondo scala** piazzato sul picco di risonanza dava −22.5 dB dalla fondamentale: è un artefatto della sonda. Rimisurato con l'**oscillatore vero** (tavola `saws`, `res` a fondo, parametri ai default), il caso peggiore raggiungibile suonando è **−39.9 dB** a MIDI 108 con cutoff 12 kHz (−52 dBFS in assoluto), e nella tessitura normale sta a **−52 dB e sotto**. La ragione è aritmetica: in un dente di sega l'armonica k vale 1/k, quindi quella che eccita la risonanza a 12 kHz è la 23ª a MIDI 72 (−27 dB), mai il fondo scala. Controprova: con il filtro precedente lo stesso banco misura da −96.7 a −151 dB, cioè il pavimento — tutto l'inarmonico viene davvero dall'anello. Accettabile; se un giorno si farà l'oversampling, l'anello è il **secondo** candidato, dopo il waveshaper.

**10. Voice stealing.** Oggi round-robin + `kill()`, che azzera inviluppo, fasi di otto oscillatori e due integratori SVF fra due campioni adiacenti. Surge sceglie la più vecchia **fra quelle in release** e le fa una dissolvenza di ~11 ms (`uber_release`), con il pool sovradimensionato di 3 voci; Gin (BSD) fa lo stesso in tre righe con `setFastKill`. Due mosse: criterio "più silenziosa, poi più vecchia", e non riusare la voce rubata ma darle 5–10 ms di fade mentre la nota nuova prende una voce davvero libera. **[TECNICA + pattern BSD]**

**11. Chorus.** Non è una feature: `fx1On` ha `"default": true` e il tab FX disegna sei knob che non fanno niente. **L'interfaccia mente.** Va fatto per primo fra gli FX perché obbliga a costruire lo stadio FX — dove si inserisce (fra il render e `applyGainRamp`), dry/wet, bypass con crossfade, aggiornamento del test di headroom — ed è meglio sbagliare quelle decisioni sull'effetto semplice. `juce::dsp::DelayLine<float, Lagrange3rd>` + `juce::dsp::DryWetMixer` con regola `sin3dB`: già linkati. **Non usare `Thiran`**, è un allpass con transitorio quando il ritardo cambia — sbagliato per un delay modulato. **[CODICE ADOTTABILE — JUCE]**

**12. Reverb.** `gin_platereverb.h`, **MIT**, plate di Dattorro completo: predelay, quattro diffusori allpass, due tank incrociati con allpass modulati, damping one-pole, sette tap per canale. I nostri tre knob mappano su tre dei suoi sei. Caveat: `setSize()` sposta i tap istantaneamente (zipper se automatizzato) e `setSampleRate()` rialloca (solo in `prepareToPlay`). `juce::dsp::Reverb` è Freeverb letterale — feedback dei comb 0.70–0.98, **nessun predelay, nessuna modulazione**: le due assenze che si sentono. `getTailLengthSeconds()` va corretto **nello stesso commit**, altrimenti si introduce una dichiarazione falsa all'host. **[CODICE ADOTTABILE — MIT]**

**13. Interpolazione Lagrange-3 della wavetable.** Oggi lineare: il pavimento a −72.7 dB è dominato da lei, non dalla piramide. Lagrange-3 legge gli stessi 4 punti di Catmull-Rom (che usa Vital) e dà **4.6 dB in più** nel caso peggiore, 13.7 a 256 armoniche, +34 dB a MIDI 53. Costo: da 2.89% a ~4.0% del budget. Lagrange-5 non serve: scenderebbe sotto il pavimento della piramide. In alternativa a costo CPU zero: frame da 4096 (+12.2 dB, 11.5 MB per tavola). **[CODICE ADOTTABILE — Signalsmith, MIT]**

**14. Glide, mono e legato.** `glide` e `voiceMode` esistono nella UI e non sono cablati: non è una funzione mancante, è una funzione rotta. Tutti interpolano **in numero di nota**, non in Hz (Odin 2 fa il contrario, ed è l'errore da non imitare). `retrigger()` fa già la cosa giusta per il legato. L'opzione *constant rate* (`T' = T·|Δnota|/12`) costa una riga ed è ciò che rende il glide musicale su intervalli ampi. **[TECNICA]**

### Più avanti, o da decidere prima della v1

**15. Registrare i float nell'APVTS in unità naturali** (`NormalisableRange` con lambda). Chiude la **classe** di bug, non l'istanza: `getRawParameterValue` restituirebbe unità naturali per ogni Kind, uniformemente, e "raw" avrebbe un significato solo. L'automazione host è preservata esattamente, la WebUI non cambia di una riga. Il punto delicato è il dominio delle depth di modulazione, che resta genuinamente normalizzato: lì servono due tipi distinti (`Norm01` / `Natural`, ~40 righe) perché è l'unico posto dove le due unità continueranno a convivere. Va fatto **in un commit solo**. 1–2 giorni. **[TECNICA]**

**16. Arpeggiatore.** Riscrivere il `MidiBuffer` in testa a `SynthEngine::process`, come il demo ISC di JUCE ma senza la sua `SortedSet` che alloca sul thread audio. Il nostro ciclo già spezza il render a ogni evento MIDI, quindi la **precisione campione-esatta è gratis**, e `VoiceManager`/`SynthVoice` restano intatti: con `arpOn = false` l'uscita è bit per bit quella di oggi. Da rubare a Odin 2 il **doppio indice** — uno sulla sequenza di note, uno sulla griglia a 16 step, che si avvolgono separatamente: con lunghezze coprime esce un poliritmo. `MeterFrame::arpStep` è già l'indice di griglia. Ultimo dei tre FX perché `arpOn` è `false` di default: nessuno lo sta aspettando.

**17. Da decidere prima della prima release**, perché retroattivamente costano: `version_added` per riga + ordinamento stabile della lista esposta all'host (Vital), così l'indice di un parametro non si sposta quando se ne aggiungono altri; chiave di streaming distinta da `id`; valori dei preset in unità **naturali** invece che 0..1 (con i normalizzati ogni cambio di range è un evento di migrazione silenzioso); se le depth della mod matrix debbano essere parametri automatizzabili (Vital sì, Odin le ha declassate).

## Da non fare

- **Mod matrix audio-rate su `cutoff`, `res`, `wtpos`, `drive`.** Nemmeno Vital lo fa: in `OscillatorModule::init` solo `transpose`, `tune`, `level` e `phase` sono audio-rate; `wave_frame` è a tasso di controllo. FM e RM non passano affatto dal matrix, sono uno stadio dedicato osc→osc con ordinamento topologico — e da noi sarebbero bloccati da un secondo oscillatore che non esiste.
- **ADAA sulle nostre nonlinearità.** Misurato: peggiora fino a 21 dB ai drive usabili. ADAA assume ingresso lineare a tratti fra due campioni; quando la funzione è già morbida a dominare è l'errore di *quel* modello. A drive 0 il nostro `saturate` non alias affatto e ADAA ci mette dentro −56…−80 dB di spazzatura. Surge lo usa solo su rettificatori e wavefolder, cioè dove la derivata salta.
- **Esponente di compensazione della risonanza variabile con Q.** Vedi punto 9: sposta il problema, non lo risolve.
- **`juce::dsp::Reverb`** come riverbero principale, e **DaisySP-LGPL** per il riverbero.
- **Copiare da `chowdsp_waveshapers`/`filters`/`dsp_utils`, `sst-filters`, `sst-waveshapers`**: GPLv3.

## Dove siamo già al livello dei riferimenti, o sopra

Vale quanto il resto, perché evita di spendere tempo dove non serve.

- **Somma della modulazione sul valore normalizzato, denormalizzata con la stessa funzione del percorso non modulato**: è `createBaseModControl` + processore di scala di Vital, e `applyModulationToLocalcopy` di Surge.
- **`modMask_`**: è il `ValueSwitch` di Vital, con meno macchinario.
- **`retrigger()` su nota ribattuta**: è `reclaimVoiceFor` di Surge, stessa motivazione.
- **Unison dentro la voce** invece di N voci del pool (Odin 2): modello Vital, e non consuma polifonia.
- **Livello mipmap frazionario con crossfade**: meglio della selezione secca di Odin 2 **e di Surge**, il cui sorgente ammette un bug noto di fase allo switch.
- **Frame da 2048 campioni**: pari a Vital, 4× Odin 2.
- **Snapshot del matrix pubblicato lock-free** con ring di 4: Vital riconfigura il grafo con un lock dal message thread.
- **`softClip` bit-trasparente sotto soglia**: nessuno dei tre ce l'ha.
- **Generazione del codice dei parametri da una sorgente unica**, con test di freschezza che rigenera e confronta byte per byte: **non ce l'ha nessuno dei tre**. Vital, Surge e Odin scrivono tutto a mano — Odin ripete ogni id in sei file, ~2900 righe di solo cablaggio, e ha una divergenza GUI↔parametro attiva in produzione.
- **Nomi dei test in prosa che sono l'invariante**: è già il nostro stile, ed è quello di Surge.

## Contesto che vale da solo

Il bug delle quattro ottave non è un'anomalia nostra: è la norma in questa classe di progetti. Vital non testa affatto il suo `ValueBridge` e le sue 760 righe di migrazione preset non sono coperte da niente. Il file di test sui parametri di Surge è lungo 147 righe e il round-trip stringa↔valore è dentro un `#if 0`. Surge, con vent'anni di disciplina sulle unità, ha comunque nel layer OSC un `float` chiamato `val01` che per i tipi interi non è 0..1 — il nostro identico scivolone. Odin 2, il più vicino a noi per stack, ha **zero test** e una copia dormiente dello stesso bug in `XYPadComponent`.

La lezione, in una riga: la convenzione di naming non basta mai. O si sposta il confine, o si mette l'unità nel tipo. Meglio entrambe.
