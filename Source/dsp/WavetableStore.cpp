#include "dsp/WavetableStore.h"

#include <juce_dsp/juce_dsp.h>

#include <cstring>

// L'header generato si chiama WavetableData.h e non BinaryData.h: quest'ultimo
// nome è già usato dal target WebUIAssets (embedding della Web UI in Release),
// e due header omonimi renderebbero l'ordine degli include a decidere quale
// si vede davvero.
#include "WavetableData.h"

namespace dsp
{
namespace
{
constexpr int kFftOrder = 11; // 2048

/** I file .xwt nell'ordine delle opzioni di `wtIndex` in parameters.json. */
const char* const kTableFiles[] = { "basic.xwt", "saws.xwt", "grit.xwt", "vocal.xwt", "bells.xwt", "pwm.xwt" };

/**
 * `juce_add_binary_data` genera i blob come `unsigned char[]` senza alignas:
 * l'indirizzo non è garantito multiplo di 4, e `parseXwt` rifiuta (giustamente)
 * un puntatore non allineato invece di leggerlo come float alla cieca. Si copia
 * quindi il blob in un buffer di float, naturalmente allineato; la dimensione
 * è sempre un multiplo di 4 (header 12 byte + campioni float), quindi la copia
 * è esatta e non lascia byte residui. `storage` deve restare viva finché la
 * `BlobView` restituita viene usata (cioè fino alla fine di `buildMipTable`).
 */
std::optional<BlobView> lookupBlob (const char* fileName, std::vector<float>& storage)
{
    for (int i = 0; i < WavetableAssets::namedResourceListSize; ++i)
    {
        if (juce::String (WavetableAssets::originalFilenames[i]) != fileName)
            continue;

        int size = 0;
        const auto* data = WavetableAssets::getNamedResource (WavetableAssets::namedResourceList[i], size);

        if (data == nullptr || size <= 0)
            return std::nullopt;

        storage.assign ((size_t) size / sizeof (float), 0.0f);
        std::memcpy (storage.data(), data, (size_t) size);

        return parseXwt (storage.data(), (size_t) size);
    }

    return std::nullopt;
}
} // namespace

std::unique_ptr<MipTable> buildMipTable (const BlobView& blob)
{
    auto table = std::make_unique<MipTable> (blob.frames, blob.frameSize);

    juce::dsp::FFT forward { kFftOrder };
    std::vector<juce::dsp::Complex<float>> timeDomain ((size_t) blob.frameSize);
    std::vector<juce::dsp::Complex<float>> spectrum ((size_t) blob.frameSize);

    for (int frame = 0; frame < blob.frames; ++frame)
    {
        const auto* source = blob.frame (frame);

        for (int i = 0; i < blob.frameSize; ++i)
            timeDomain[(size_t) i] = { source[i], 0.0f };

        forward.perform (timeDomain.data(), spectrum.data(), false);

        for (int level = 0; level <= MipTable::kMaxLevel; ++level)
        {
            const int size = blob.frameSize >> level;
            const int harmonics = size / 2;

            juce::dsp::FFT inverse { kFftOrder - level };
            std::vector<juce::dsp::Complex<float>> shortSpectrum ((size_t) size, { 0.0f, 0.0f });
            std::vector<juce::dsp::Complex<float>> shortTime ((size_t) size);

            // Si tengono le armoniche 0..harmonics-1 e i loro gemelli negativi:
            // tutto quello che sta sopra produrrebbe aliasing a questa lunghezza.
            for (int bin = 0; bin < harmonics; ++bin)
                shortSpectrum[(size_t) bin] = spectrum[(size_t) bin];

            for (int bin = 1; bin < harmonics; ++bin)
                shortSpectrum[(size_t) (size - bin)] = spectrum[(size_t) (blob.frameSize - bin)];

            inverse.perform (shortSpectrum.data(), shortTime.data(), true);

            // L'antitrasformata divide per `size`, ma i bin vengono da una FFT
            // lunga `frameSize`: il fattore di scala rimette le cose a posto.
            const auto scale = (float) size / (float) blob.frameSize;
            auto* destination = table->writePointer (frame, level);

            for (int i = 0; i < size; ++i)
                destination[i] = shortTime[(size_t) i].real() * scale;
        }
    }

    return table;
}

WavetableStore::WavetableStore()
{
    tables_.resize (std::size (kTableFiles));
}

void WavetableStore::setActive (int index)
{
    if (index < 0 || index >= getNumTables())
        return;

    if (tables_[(size_t) index] == nullptr)
    {
        std::vector<float> storage;
        const auto blob = lookupBlob (kTableFiles[index], storage);

        if (! blob.has_value())
            return; // blob assente o corrotto: si resta sulla tavola precedente

        tables_[(size_t) index] = buildMipTable (*blob);
    }

    active_.store (tables_[(size_t) index].get(), std::memory_order_release);
}
} // namespace dsp
