#pragma once

#include <algorithm>
#include <cmath>

namespace dsp
{
/**
 * La lunghezza massima di una fetta a tasso di controllo, in campioni: 1500 Hz a 48 kHz.
 *
 * E' un numero solo per tutto il progetto — le sotto-fette del motore (SynthEngine), le fette di
 * modulazione del chorus e del riverbero — perche' e' la stessa scelta fatta per la stessa
 * ragione: il tasso a cui si ricalcola un bersaglio e' una proprieta' del DSP, non del buffer che
 * passa l'host. La motivazione per esteso, con le misure, sta accanto a
 * SynthEngine::kControlBlockSamples.
 */
inline constexpr int kControlRateSamples = 32;

/**
 * La soglia di silenzio: -80 dB. Sotto, una coda e' inudibile e cio' che la produce va spento —
 * l'inviluppo che dichiara finito un release, la dissolvenza di una voce rubata che si azzera.
 * Un decimillesimo dell'ampiezza di partenza: sotto il pavimento a 16 bit.
 */
inline constexpr float kSilenceFloor = 1.0e-4f;

/** Il coefficiente per campione di un polo singolo di emivita `halfLifeSeconds`: dopo quel
    tempo la distanza dal bersaglio si e' dimezzata. `1 - 2^(-1/(h*fs))`. */
inline float halfLifeCoefficient (float halfLifeSeconds, double sampleRate) noexcept
{
    const auto halfLifeSamples = (float) (halfLifeSeconds * sampleRate);
    return 1.0f - std::exp2 (-1.0f / std::max (1.0f, halfLifeSamples));
}

/** Chiama `fn (offset, length)` per ogni fetta di al piu' `maxSlice` campioni di un blocco. */
template <typename Fn>
inline void forEachSlice (int numSamples, int maxSlice, Fn&& fn) noexcept
{
    for (int offset = 0; offset < numSamples;)
    {
        const auto slice = std::min (maxSlice, numSamples - offset);
        fn (offset, slice);
        offset += slice;
    }
}
} // namespace dsp
