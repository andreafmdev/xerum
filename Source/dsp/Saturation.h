#pragma once

namespace dsp
{
/**
 * Soft clipper: guadagno unitario sul piccolo segnale, satura dolcemente e si ferma a 1.0
 * quando l'ingresso raggiunge 3. Niente tanh() per campione — costa tre volte tanto e da' meno.
 *
 * E' l'approssimante di Pade [3/2] di tanh, `x(27 + x^2) / (27 + 9x^2)`. Il denominatore ha una
 * radice **tripla** in 3 (x^3 - 9x^2 + 27x - 27 = (x-3)^3), quindi nel punto in cui la curva
 * tocca 1 sono nulle sia la derivata prima sia la seconda: il raccordo con il tratto piatto e'
 * C2. La derivata, in forma chiusa, e' f'(x) = 9(x^2-9)^2 / (27+9x^2)^2 — un quadrato, quindi
 * non negativa ovunque: la funzione e' monotona per costruzione, non per taratura.
 *
 * Provenienza: la stessa formula sta in `DaisySP/Source/Filters/ladder.cpp` come `fast_tanh`
 * (licenza **MIT**, (c) Richard van Hoesel / Infrasonic Audio), e in Surge XT come `wst_soft`.
 * E' comunque matematica pubblica: l'approssimante di Pade di tanh non e' l'invenzione di
 * nessuno. Nessun codice GPL e' entrato qui.
 *
 * Cosa sostituisce, e perche'. La curva precedente era `clamp(x, +-1.5)` seguito da
 * `c - c^3/6.75`: raccordava con derivata prima nulla ma lasciava la **seconda** che saltava da
 * -1.333 a 0, e sopra 1.5 appiattiva del tutto — il 24.8 % del ciclo a drive 6 dB, il 90.5 % a
 * 24 dB. Quello spigolo e quel tratto piatto sono la sorgente dell'alias: le armoniche decadono
 * come 1/n^3 invece che esponenzialmente, e tutto cio' che sta sopra Nyquist si ripiega in
 * banda su frequenze che non hanno relazione armonica con la nota. Misurato a 48 kHz, alias
 * rispetto alla fondamentale: a MIDI 84 e drive 6 dB da -75.4 a -151.9 dB, a MIDI 60 da -111.7
 * a -334 dB (in doppia precisione; il percorso float si ferma sul proprio arrotondamento,
 * -180). Sopra MIDI ~100 le due curve pareggiano e nessuna delle due puo' fare meglio: con la
 * fondamentale a 4 kHz ci stanno cinque armoniche sotto Nyquist, e da li' in poi servirebbe
 * l'oversampling, che e' una decisione diversa.
 *
 * **Non e' neutra sul suono**: a x = 1 vale 0.778 contro 0.852, e il ginocchio sta a 3.0 invece
 * che a 1.5. Satura prima e piu' dolcemente, e a parita' di `drive` l'uscita e' fra 0.1 e 0.8 dB
 * piu' bassa in RMS. Niente e' stato ritarato per compensare: ne' `drive`, ne' i preset, ne'
 * kVoiceHeadroomGain. E' una decisione che richiede di riascoltare, non di ricalcolare.
 *
 * I due rami sono il clamp, e non un `std::clamp` applicato al risultato: con x infinito la
 * forma razionale calcolerebbe inf/inf = NaN, e ogni confronto con NaN e' falso, quindi il
 * clamp lo lascerebbe passare intatto fino all'uscita. Cosi' invece l'infinito esce 1.
 *
 * Sta nell'header, e non piu' nel namespace anonimo di SynthVoice.cpp, perche' e' l'unica
 * nonlinearita' della catena: l'alias che genera e le sue proprieta' (dispari, monotona,
 * limitata) sono verificabili solo misurando *questa* funzione, non il motore intero. Il
 * template serve a quello: la produzione la istanzia a `float`, la misura a `double`, dove il
 * pavimento numerico sta centocinquanta decibel piu' in basso e l'alias vero resta visibile.
 */
template <typename T>
constexpr T saturateCurve (T x) noexcept
{
    if (x > T (3))
        return T (1);

    if (x < T (-3))
        return T (-1);

    const auto x2 = x * x;
    return x * (T (27) + x2) / (T (27) + T (9) * x2);
}

/**
 * L'istanza usata dal thread audio. Nessuna chiamata a libm: due confronti, quattro
 * moltiplicazioni, una divisione.
 *
 * Costo misurato in Release (clang -O3, Apple Silicon), al netto del ciclo a vuoto e con la
 * vettorizzazione spenta, che e' il caso vero — nel ciclo di render() il filtro introduce una
 * dipendenza seriale fra un campione e il successivo: **0.75 ns/campione contro 0.63** della
 * curva precedente. Sono 0.12 ns in piu', che a 16 voci per 8 copie di unison fanno 6.1 milioni
 * di chiamate al secondo, cioe' 0.7 ms per secondo di audio: lo 0.07 % di un core.
 *
 * `std::tanh` costerebbe 1.14 ns — una volta e mezza — e darebbe **meno**, non di piu': misurato
 * con la stessa DFT, a MIDI 84 alias -141.4 dB a drive 6 dB e -158.1 a 4.8, cioe' dieci
 * decibel peggio del Pade a ogni drive usabile. Il Pade non e' un'approssimazione economica di
 * tanh: sopra 3 e' esattamente piatto, mentre tanh continua a curvare all'infinito, e sono
 * quelle curvature residue a generare le armoniche alte che si ripiegano.
 */
inline float saturate (float x) noexcept { return saturateCurve (x); }

/**
 * Il guadagno di `drive` oltre il quale la saturazione entra al 100 %: +2 dB.
 *
 * Non e' un numero di comodo, e' l'ampiezza del gradino diviso la pendenza della curva. Il
 * gradino misurato all'innesco vale 0.94 dB di RMS, e nella prima parte della corsa la
 * saturazione restituisce circa mezzo decibel per ogni decibel di `drive`: serve quindi circa
 * un paio di decibel di corsa perche' il secco e il bagnato si raggiungano senza che la somma
 * scenda sotto il secco. Misurato: con la dissolvenza su 1 dB resta un avvallamento di 0.27 dB
 * a meta', su 2 dB scende a 0.09 dB, che e' sotto la risoluzione di un ascolto.
 */
inline constexpr float kDriveFadeGain = 1.2589254f; // 10^(2/20)
} // namespace dsp
