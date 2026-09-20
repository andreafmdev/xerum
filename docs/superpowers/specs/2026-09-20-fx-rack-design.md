# FX rack: delay stereo e ordine degli effetti

Data: 2026-09-20. Stato: approvato in chat, in attesa del piano.

## Obiettivo

Lo stadio FX oggi è chorus → riverbero, in serie e in quest'ordine. Diventa un rack a **tre slot fissi** — chorus, delay (nuovo), riverbero — con l'**ordine scelto dall'utente** fra tre permutazioni. Nessun cambio del formato di stato: solo parametri nuovi, che un progetto vecchio carica ai default (delay spento, ordine di oggi), quindi suona identico.

## Non obiettivi

- Slot con scelta dell'effetto (phaser, EQ…): il rack resta a tre effetti noti.
- Tutte e sei le permutazioni: tre bastano a coprire i casi musicali (delay prima o dopo il chorus, riverbero prima o dopo il delay).
- Modulare i parametri del delay dal mod matrix: come chorus e riverbero, restano fuori dai target.

## Parametri (`parameters.json`, gruppo `fx3` "Delay" + `fxOrder` in `master`)

| id | kind | mappa | default | note |
|---|---|---|---|---|
| `fx3On` | bool | — | false | slot |
| `dlTime` | float | log 1..2000 ms, label `time` | 0.78 (≈375 ms) | con `dlSync` il grezzo sceglie una divisione, stessa convenzione di `lrate`/`lsync` |
| `dlSync` | bool | — | false | |
| `dlFeedback` | float | linear 0..90 % | 0.39 (35 %) | tetto 90 %: sopra la coda non finisce più |
| `dlDamp` | float | linear 0..100 % | 0.5 | passa-basso a un polo nel feedback, 20 kHz → 500 Hz log come `rvDamp` |
| `dlMix` | float | linear 0..100 % | 0.25 | regola sin3dB via `DryWetMixer`, come gli altri |
| `dlPingPong` | bool | — | false | il feedback incrocia i canali |
| `fxOrder` | choice | `cdr` "Cho→Dly→Rev", `dcr` "Dly→Cho→Rev", `crd` "Cho→Rev→Dly" | 0 (`cdr`) | slot; `cdr` è l'ordine di oggi |

Tutti con `"slot": true`. `gen-params` produce ParamSlot, tabella e TS come per gli altri; `collectEngineParams` denormalizza nei nuovi campi di `EngineParams` (`delayOn`, `delayTimeSeconds`, `delaySync`, `delayTimeRaw`, `delayFeedback01`, `delayDamp01`, `delayMix01`, `delayPingPong`, `fxOrder`).

Tempo sincronizzato: `dsp::syncedRateHz(raw, bpm)` è già la mappa grezzo → divisione dell'LFO (sei divisioni, 1/16..2 battute); il delay usa `1 / syncedRateHz`, limitato alla lunghezza massima della linea (2 s). Una sola tabella per LFO e delay, come chiede la doc di `arpBeatsPerStep` per non averne due che sembrano uguali.

## DSP: `dsp::StereoDelay` (`Source/dsp/StereoDelay.{h,cpp}`)

- Due `juce::dsp::DelayLine<float, Lagrange3rd>` da 2 s + margine, allocate in `prepare`; nessuna allocazione dopo.
- `setParameters (timeSeconds, feedback01, damp01, pingPong)`; il tempo insegue il bersaglio con `dsp::halfLifeCoefficient` (50 ms, come `kSizeSmoothingHalfLifeSeconds` del riverbero): cambiare il tempo fa scivolare l'intonazione degli echi, che è il suono di un delay analogico, e non produce clic.
- Feedback: `out = line.pop(); line.push (in + damp(out) * feedback)`; con ping-pong il feedback del canale sinistro entra nel destro e viceversa. Damp = passa-basso a un polo (`1 - exp(-2π f / sr)`), un coefficiente per canale ricalcolato solo quando cambia `damp01`.
- Il bagnato esce a guadagno 1 (il mix lo fa `DryWetMixer` fuori, come per chorus e riverbero).
- `process (left, right, n)` a fette di `kControlRateSamples` (`forEachSlice`), con i coefficienti di smoothing valutati per fetta.
- `float tailSeconds (timeSeconds, feedback01)` statica: `time · ceil (ln 1e-4 / ln feedback)`, `time` con feedback 0, limitata a 60 s: è la coda che `ringoutSamples()` e `getTailLengthSeconds` dichiarano.
- Costo: due `pop`/`push` Lagrange e due moltiplicazioni per campione. Senza libm nel loop.

## Motore: `processFxChunk` diventa un ciclo su slot ordinati

- `struct FxSlot { Kind kind; bool running; juce::SmoothedValue<float> gain; juce::dsp::DryWetMixer<float> mix; int primeSamples, primeLength; }` per i tre effetti; l'ordine è `std::array<FxSlot*, 3>` ricavato da `params_.fxOrder` una volta per blocco.
- Ogni slot ripete il pattern di oggi: se `want || !idle`: avvio (reset + `running`), bersaglio della rampa, copia del secco se in dissolvenza, `pushDrySamples`, processo, `mixWetSamples`, `crossfadeWithDry`. Le tre eccezioni restano dov'erano: il chorus riempie la linea prima di dissolvere (`prime`), il riverbero rampa l'ingresso, il delay non ha bisogno di nessuna delle due (la linea parte vuota e l'ingresso è continuo: gli echi crescono da zero per costruzione).
- **Bit-identità**: con `fx3On` falso e delay fermo, il ciclo con ordine `cdr` esegue esattamente le operazioni di oggi nello stesso ordine; `crd` e `cdr` con delay fermo devono dare uscite identiche campione per campione (test). Il caso "tutti spenti e fermi" esce senza toccare il buffer, come oggi.
- `ringoutSamples()` = max fra il ringout costante e le code degli effetti in esecuzione (riverbero: `tailSeconds()`; delay: `StereoDelay::tailSeconds (time, feedback)`).
- `stopFx()` azzera anche il delay.
- `prepare`: il delay alloca qui.

## Coda dichiarata all'host

`XerumAudioProcessor::getTailLengthSeconds()` resta `engine::SynthEngine::kDeclaredTailSeconds` (chorus + riverbero, verificato da `ReverbTests`) e aggiunge la coda del delay **dai parametri correnti** (`fx3On`, `dlTime`, `dlFeedback` letti dall'APVTS sul message thread): un delay lungo con feedback alto dichiara di più solo quando è acceso.

## UI (`FxTab`)

- Tre colonne (Chorus | Delay | Reverb), ognuna: toggle on/off, nome, knob `sm`: chorus 4 knob come oggi; delay Time, Feedback, Damp, Mix + due toggle piccoli Sync e Ping; riverbero 5 knob come oggi.
- `Select` dell'ordine nell'intestazione del plate (tre opzioni, etichette "Cho→Dly→Rev" ecc.).
- Il knob Time mostra la divisione quando `dlSync` è acceso, con la stessa `DIVISIONS` del knob rate dell'LFO.
- Nessun cambio a `WaveDisplay`, header, preset overlay.

## Preset

Nessun preset accende il delay: i dodici suonano come oggi. Un eventuale preset "con delay" è una scelta d'ascolto, fuori da questo lavoro.

## Test (TDD, ordine di scrittura)

1. `Tests/DelayTests.cpp` (dsp): impulso → primo eco a `round(time·sr)` campioni con ampiezza 1 (bagnato puro); secondo eco = feedback × primo; damping abbassa il centroide del secondo eco; ping-pong: impulso a sinistra, primo eco a destra; tempo sincronizzato a 120 bpm con divisione 1/4 = 0.5 s; un salto di tempo non produce un passo maggiore della pendenza naturale (nessun clic); `tailSeconds` con feedback 0 = time, con 0.5 ≈ 14·time; reset azzera.
2. `Tests/FxRackTests.cpp` (engine): con delay spento `cdr` e `crd` sono bit-identici; con delay acceso l'uscita cambia; `dcr` ≠ `cdr` con tutti accesi (RMS della differenza > soglia); solo delay acceso → lo stadio si spegne dopo la coda (stesso metodo del test di ringout del riverbero); nessuna allocazione in `process` (il contatore di ReverbTests); `collectEngineParams` porta i nuovi campi.
3. `Tests/PluginProcessorTests.cpp`: `getTailLengthSeconds` cresce con il delay acceso a tempo lungo e feedback alto, resta `kDeclaredTailSeconds` con il delay spento.
4. vitest: `FxTab` rende tre slot e il select dell'ordine; il knob Time mostra "1/4" con sync acceso; `params.freshness` (rigenerazione).

## Rischi

- Il tetto di headroom (+8 dBFS al clipper): il delay a feedback 90 % e mix 100 % somma energia; va misurato con lo stress test (`ModulationStressTests`, "matrix pieno e stadio FX acceso") e, se supera, il feedback massimo scende (80 %) prima di toccare `kVoiceHeadroomGain`.
- Lunghezza della linea (2 s) contro divisioni lunghe a tempi lenti: il tempo viene limitato, e la UI non lo dice. Accettato: è il comportamento di ogni delay con un massimo.
