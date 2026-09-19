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
