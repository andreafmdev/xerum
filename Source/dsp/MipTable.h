#pragma once

#include <vector>

namespace dsp
{
/**
 * Una tavola pronta per il thread audio: `frames` frame, ognuno con la piramide
 * dei livelli band-limited. Il livello k ha `frameSize >> k` campioni, cioè metà
 * armoniche del livello precedente. Memoria totale per frame ≈ 2 × frameSize,
 * non (kMaxLevel + 1) × frameSize.
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
    int sizeAtLevel (int level) const noexcept { return frameSize_ >> level; }

    /** Solo durante la costruzione. */
    float* writePointer (int frame, int level) noexcept;

    /** Thread audio. */
    const float* samples (int frame, int level) const noexcept;

private:
    int indexOf (int frame, int level) const noexcept;

    int frames_;
    int frameSize_;
    int stridePerFrame_ { 0 };
    std::vector<int> levelOffsets_;
    std::vector<float> data_;
};
} // namespace dsp
