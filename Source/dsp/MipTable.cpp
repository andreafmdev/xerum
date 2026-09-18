#include "dsp/MipTable.h"

namespace dsp
{
MipTable::MipTable (int frames, int frameSize)
    : frames_ (frames), frameSize_ (frameSize)
{
    // Tutti i livelli hanno la stessa lunghezza: lo stride per frame è un semplice
    // prodotto, non una somma di lunghezze decrescenti (vedi commento nell'header).
    data_.assign ((size_t) frames_ * (size_t) (kMaxLevel + 1) * (size_t) frameSize_, 0.0f);
}

int MipTable::indexOf (int frame, int level) const noexcept
{
    return (frame * (kMaxLevel + 1) + level) * frameSize_;
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
