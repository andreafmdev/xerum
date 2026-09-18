#pragma once

#include <vector>

namespace dsp
{
/**
 * Una tavola pronta per il thread audio: `frames` frame, ognuno con la piramide
 * dei livelli band-limited. Il livello k conserva `(frameSize >> k) / 2` armoniche,
 * cioè metà di quelle del livello precedente — una ottava per livello.
 *
 * Tutti i livelli restano lunghi `frameSize` campioni: la limitazione di banda vive
 * nello spettro, non nella lunghezza del buffer. Decimare anche la lunghezza (la
 * versione precedente teneva `frameSize >> k` campioni) faceva leggere all'oscillatore
 * tavole da 64 o 32 campioni sulle note medio-alte, dove l'interpolazione lineare fra
 * campioni adiacenti introduce più distorsione di quanta aliasing ne tolga il filtro.
 * Il costo è memoria: (kMaxLevel + 1) × frameSize per frame invece di ≈ 2 × frameSize,
 * cioè ~3.7 MB per una tavola 64 × 2048 — una sola tavola per volta è viva.
 *
 * Costruita sul message thread, letta dal thread audio: dopo la costruzione è
 * immutabile e non viene mai distrutta finché vive il plugin.
 */
class MipTable
{
public:
    static constexpr int kMaxLevel = 6;

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
