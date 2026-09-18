#include "dsp/MipTable.h"

namespace dsp
{
MipTable::MipTable (int frames, int frameSize)
    : frames_ (frames), frameSize_ (frameSize)
{
    levelOffsets_.resize (kMaxLevel + 1);

    for (int level = 0; level <= kMaxLevel; ++level)
    {
        levelOffsets_[(size_t) level] = stridePerFrame_;
        stridePerFrame_ += frameSize_ >> level;
    }

    data_.assign ((size_t) frames_ * (size_t) stridePerFrame_, 0.0f);
}

int MipTable::indexOf (int frame, int level) const noexcept
{
    return frame * stridePerFrame_ + levelOffsets_[(size_t) level];
}

float* MipTable::writePointer (int frame, int level) noexcept
{
    return data_.data() + indexOf (frame, level);
}

const float* MipTable::samples (int frame, int level) const noexcept
{
    return data_.data() + indexOf (frame, level);
}
} // namespace dsp
