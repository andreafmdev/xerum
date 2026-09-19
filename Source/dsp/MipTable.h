#pragma once

#include <vector>

namespace dsp
{
/**
 * Una tavola pronta per il thread audio: `frames` frame, ognuno con la piramide
 * dei livelli band-limited. Il livello k conserva `(frameSize >> k) / 2` armoniche,
 * cioè metà di quelle del livello precedente — una ottava per livello.
 *
 * Un'ottava è un salto grosso: suonare un livello secco per tutta l'ottava in cui è il
 * primo sicuro congela la brillantezza e poi la fa crollare di colpo al confine. Per
 * questo l'oscillatore non sceglie un livello ma ne mescola due adiacenti (vedi
 * `levelForFrequency` in WavetableOscillator.h): la piramide resta spaziata di un'ottava,
 * quello che si sente no.
 *
 * Tutti i livelli restano lunghi `frameSize` campioni: la limitazione di banda vive
 * nello spettro, non nella lunghezza del buffer. Decimare anche la lunghezza (la
 * versione precedente teneva `frameSize >> k` campioni) faceva leggere all'oscillatore
 * tavole da 64 o 32 campioni sulle note medio-alte, dove l'interpolazione lineare fra
 * campioni adiacenti introduce più distorsione di quanta aliasing ne tolga il filtro.
 * Il costo è memoria: (kMaxLevel + 1) × frameSize per frame invece di ≈ 2 × frameSize,
 * cioè ~5.8 MB per una tavola 64 × 2048 — una sola tavola per volta è viva.
 *
 * `kMaxLevel` vale 10 perché è il primo livello che, su un frame da 2048 campioni,
 * conserva **una sola** armonica: la fondamentale e basta, cioè una sinusoide. Serve
 * per davvero — a 12.5 kHz (MIDI 127) sotto Nyquist ci sta solo la fondamentale.
 * Fermarsi prima non risparmia lavoro, ripiega: con il valore precedente (6) il livello
 * più alto teneva 16 armoniche e `levelForFrequency` non aveva niente di più stretto da
 * scegliere, così ogni nota sopra ~1.4 kHz suonava le sue armoniche sopra Nyquist
 * ripiegate all'indietro — misurato a -25 dB sotto il segnale a MIDI 90 e a -6 dB a
 * MIDI 120, cioè più rumore che nota.
 *
 * Costruita sul message thread, letta dal thread audio: dopo la costruzione è
 * immutabile e non viene mai distrutta finché vive il plugin.
 */
class MipTable
{
public:
    static constexpr int kMaxLevel = 10;

    MipTable (int frames, int frameSize);

    int getNumFrames() const noexcept { return frames_; }
    int getFrameSize() const noexcept { return frameSize_; }

    /** Armoniche conservate al livello `level`. Serve solo ai test e alla costruzione. */
    int harmonicsAtLevel (int level) const noexcept { return (frameSize_ >> level) / 2; }

    /** Solo durante la costruzione. */
    float* writePointer (int frame, int level) noexcept;

    /** Thread audio. */
    const float* samples (int frame, int level) const noexcept;

private:
    int indexOf (int frame, int level) const noexcept;

    int frames_;
    int frameSize_;
    std::vector<float> data_;
};
} // namespace dsp
