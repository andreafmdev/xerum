#include "dsp/WavetableBlob.h"

#include <cstring>

namespace dsp
{
namespace
{
// Limiti di sanità: un blob che dichiara di più è corrotto, non ambizioso.
constexpr uint32_t kMaxFrames = 256;
constexpr uint32_t kMaxFrameSize = 4096;

uint32_t readLittleEndian32 (const uint8_t* p) noexcept
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}
} // namespace

std::optional<BlobView> parseXwt (const void* data, size_t sizeInBytes) noexcept
{
    if (data == nullptr || sizeInBytes < 12)
        return std::nullopt;

    const auto* bytes = static_cast<const uint8_t*> (data);

    if (std::memcmp (bytes, "XWT1", 4) != 0)
        return std::nullopt;

    const auto frames = readLittleEndian32 (bytes + 4);
    const auto frameSize = readLittleEndian32 (bytes + 8);

    if (frames == 0 || frames > kMaxFrames || frameSize == 0 || frameSize > kMaxFrameSize)
        return std::nullopt;

    // Potenza di due: la mipmap dimezza la lunghezza a ogni livello.
    if ((frameSize & (frameSize - 1)) != 0)
        return std::nullopt;

    const auto expected = (size_t) 12 + (size_t) frames * (size_t) frameSize * sizeof (float);

    if (sizeInBytes < expected)
        return std::nullopt;

    // I campioni vengono letti come float: se il blob non è allineato, meglio
    // rifiutarlo che fare una lettura non allineata.
    if ((reinterpret_cast<uintptr_t> (bytes + 12) % alignof (float)) != 0)
        return std::nullopt;

    BlobView view;
    view.samples = reinterpret_cast<const float*> (bytes + 12);
    view.frames = (int) frames;
    view.frameSize = (int) frameSize;
    return view;
}
} // namespace dsp
