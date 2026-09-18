#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace dsp
{
/** Vista di sola lettura su un blob .xwt già in memoria (BinaryData): non possiede niente. */
struct BlobView
{
    const float* samples { nullptr };
    int frames { 0 };
    int frameSize { 0 };

    const float* frame (int index) const noexcept
    {
        return samples + (size_t) index * (size_t) frameSize;
    }
};

/** Valida magic, dimensioni, allineamento e lunghezza. Nessuna allocazione, nessuna eccezione. */
std::optional<BlobView> parseXwt (const void* data, size_t sizeInBytes) noexcept;
} // namespace dsp
