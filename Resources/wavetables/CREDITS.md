# Wavetables

Generate da `scripts/fetch-wavetables.mjs` il 2026-09-18, ri-processate da
`scripts/realign-wavetables.mjs` il 2026-09-19 (allineamento di fase fra i frame,
equalizzazione parziale dell'RMS e normalizzazione globale).

Fonte: **Adventure Kid Waveforms (AKWF)** — Kristoffer Ekstrand
<https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE>

Licenza: **CC0-1.0** (pubblico dominio). L'attribuzione non è dovuta: è qui per tracciare la provenienza.

## Pipeline

Per ogni tavola si scaricano fino a 32 onde della famiglia, che fanno da ancore del morph.

1. ogni onda viene ricampionata a 2048 campioni per ciclo (serie di Fourier) e le si toglie la continua;
2. **allineamento di fase**: ogni ancora viene ruotata sullo shift circolare che la correla al massimo con
   la precedente. È solo un offset di fase, lo spettro di ampiezza non cambia, ma senza questo passaggio il
   crossfade fra onde sfasate le fa cancellare a pettine;
3. i 64 frame si ricavano interpolando linearmente fra ancore adiacenti;
4. se a metà strada fra due frame si perde più di 1 dB di RMS, si applica anche la continuità
   di fase per armonica (fase srotolata lungo i frame e riscritta come retta, ampiezze invariate);
5. **equalizzazione parziale dell'RMS**: ogni frame prende un guadagno `(rms_mediano / rms)^0.7`. Le onde
   AKWF sono già normalizzate a picco 1.0 una per una, quindi il loro RMS varia di una decina di dB dentro la
   stessa famiglia. L'esponente 0.7 comprime l'escursione al 30% invece di appiattirla: in uno sweep PWM
   l'impulso che si stringe deve calare di volume. È un guadagno costante per frame, quindi timbralmente neutro;
6. **normalizzazione globale**: un solo fattore di scala per tutta la tavola, quello che porta a 1 il massimo
   campione su tutti i frame. Normalizzare frame per frame farebbe cambiare volume a Position.

Le stesse operazioni si possono applicare a tavole già scritte con `node scripts/realign-wavetables.mjs`,
che stampa le metriche prima e dopo.

| tavola | famiglia AKWF |
|---|---|
| Basic Shapes (`basic`) | `AKWF_bw_perfectwaves` |
| Analog Saws (`saws`) | `AKWF_bw_saw` |
| Digital Grit (`grit`) | `AKWF_bitreduced` |
| Vocal Formant (`vocal`) | `AKWF_hvoice` |
| Glass Bells (`bells`) | `AKWF_fmsynth` |
| PWM Sweep (`pwm`) | `AKWF_bw_squ` |

<!-- fetch-wavetables: sotto questa riga il contenuto non e' generato da questo script — non verra' toccato da una rigenerazione -->

## Tavole importate da Serum

Estratte da *Retro Synthwave Pack 2* (autore `zak235`) con
`scripts/import-serum-wavetables.mjs` il 2026-09-20. I preset `.fxp` del pack sono stato di
Xfer Serum; alcuni portano una wavetable custom in float32 grezzi dentro un secondo stream
zlib del chunk. Il pack ne conteneva sette tavole distinte per sha1 dello stream grezzo,
ridotte da 256/278/22 frame ai 64 di Xerum interpolando lungo l'asse del morph, poi
normalizzate con un solo fattore globale. **Se ne spediscono cinque**: due delle sette erano
la stessa tavola salvata due volte nel pack con arrotondamenti float diversi — `retro-spindizzy`
è `retro-racing` (differenza massima campione per campione 5.96e-08, ~1 ULP float32) e
`retro-uridium` è `retro-leaderboard` (4.77e-07) — la deduplica per sha1 non lo vedeva perché
gli stream sorgente differiscono davvero, di rumore. `scripts/import-serum-wavetables.mjs`
riconosce ora questi due sha1 come duplicati noti e li scarta segnalandolo esplicitamente,
invece di sparire dietro l'avviso generico "nessuno slug noto".

**Licenza: non risolta.** Il pack è un prodotto commerciale di terzi. Queste cinque tavole
stanno qui per la build personale; **non possono essere ridistribuite in un Xerum pubblicato**
senza permesso scritto dell'autore. Se il permesso non arriva, vanno rimosse insieme alle loro
opzioni in `parameters.json` e alle voci in `kWavetableFiles`.

| tavola | frame sorgente | origine |
|---|---|---|
| `retro-racing` | 256 frame | 13 preset, es. `BS-RacingDestructionKit5` |
| `retro-ggsisters` | 256 frame | 2 preset, es. `FX-GGSisters4` |
| `retro-leaderboard` | 22 frame | 6 preset, es. `FX-Leaderboard0` |
| `retro-commando` | 256 frame | 2 preset, es. `LD-Commando18` |
| `retro-uridium-pad` | 278 frame | 1 preset, es. `PD-Uridium1` |

### Riallineamento tentato su due tavole

Misurate con gli strumenti di `scripts/wavetable-dsp.mjs` (le stesse sei metriche della
pipeline AKWF sopra), tre delle cinque tavole spedite già reggono il morph senza alcun
trattamento — `retro-racing` (correlazione minima 0.953, peggiore perdita a metà morph
-0.10 dB; la sua escursione RMS di 17.94 dB è stata guardata e lasciata così di proposito),
`retro-ggsisters` (0.996, -0.01 dB) e `retro-leaderboard` (0.627, -0.87 dB). Le altre due no:
`retro-commando` arrivava con frame adiacenti in **antifase** (correlazione minima -0.225,
peggiore perdita a metà morph -4.11 dB) e `retro-uridium-pad` di fatto scorrelata (0.013,
-2.95 dB).

Per queste due si è tentato il riallineamento con la stessa pipeline delle sei tavole AKWF
sopra — allineamento di fase, continuità di fase per armonica, equalizzazione parziale
dell'RMS, `scripts/import-serum-wavetables.mjs`, opt-in per tavola (`REALIGN_TABLES`). Il
tentativo **non ha raggiunto il criterio di riuscita** (correlazione minima positiva e
perdita a metà morph non peggiore di -1.0 dB):

| tavola | correlazione minima | peggiore perdita a metà morph |
|---|---|---|
| `retro-commando` | -0.225 → 0.290 | -4.11 dB → -1.90 dB |
| `retro-uridium-pad` | 0.013 → 0.352 | -2.95 dB → -1.70 dB |

La correlazione torna positiva, ma la perdita resta sotto soglia per entrambe: il limite che
la sola fase non può superare (differenza fra gli spettri di ampiezza dei frame adiacenti,
`midMorphLossBounds`) è già -1.88 dB e -1.66 dB rispettivamente, quindi è un limite del
materiale — due frame timbricamente molto diversi da qualche parte nella sequenza originale —
non della pipeline. Per questo **i due `.xwt` non sono stati toccati**: restano quelli
originariamente importati, non allineati in fase. Nessun passo ulteriore è stato tentato oltre
quelli già elencati.

Nota per chi rilancia l'importatore con i `.fxp` originali (non presenti in questo
ambiente): la misura sopra è stata fatta sui 64 frame già ridotti nel repo, come proxy —
`selectFrames` con lunghezza di ingresso e uscita uguali è un'identità, quindi il calcolo è
corretto per quei dati, ma un rilancio dai 256/278 frame grezzi di Serum potrebbe interpolare
su più materiale intermedio e dare un risultato diverso.
