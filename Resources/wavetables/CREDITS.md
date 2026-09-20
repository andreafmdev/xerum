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

## Tavole importate da Serum

Estratte da *Retro Synthwave Pack 2* (autore `zak235`) con
`scripts/import-serum-wavetables.mjs` il 2026-09-20. I preset `.fxp` del pack sono stato di
Xfer Serum; alcuni portano una wavetable custom in float32 grezzi dentro un secondo stream
zlib del chunk. Sette tavole distinte, ridotte da 256/278/22 frame ai 64 di Xerum
interpolando lungo l'asse del morph, poi normalizzate con un solo fattore globale.

**Licenza: non risolta.** Il pack è un prodotto commerciale di terzi. Queste sette tavole
stanno qui per la build personale; **non possono essere ridistribuite in un Xerum pubblicato**
senza permesso scritto dell'autore. Se il permesso non arriva, vanno rimosse insieme alle loro
opzioni in `parameters.json` e alle voci in `kWavetableFiles`.

| tavola | frame sorgente | origine |
|---|---|---|
| `retro-racing` | 256 frame | 13 preset, es. `BS-RacingDestructionKit5` |
| `retro-ggsisters` | 256 frame | 2 preset, es. `FX-GGSisters4` |
| `retro-leaderboard` | 22 frame | 6 preset, es. `FX-Leaderboard0` |
| `retro-spindizzy` | 256 frame | 10 preset, es. `FX-Spindizzy` |
| `retro-uridium` | 22 frame | 1 preset, es. `KY-Uridium2` |
| `retro-commando` | 256 frame | 2 preset, es. `LD-Commando18` |
| `retro-uridium-pad` | 278 frame | 1 preset, es. `PD-Uridium1` |
