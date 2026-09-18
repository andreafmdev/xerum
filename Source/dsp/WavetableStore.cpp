#include "dsp/WavetableStore.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
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
/** Ordine della FFT per un frame lungo `frameSize` campioni (potenza di due garantita da parseXwt). */
int fftOrderFor (int frameSize) noexcept
{
    int order = 0;
    while ((1 << order) < frameSize)
        ++order;
    return order;
}

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

        // Arrotondato per eccesso: una risorsa la cui dimensione non e' multipla di 4 byte
        // lascerebbe il memcpy successivo scrivere fino a 3 byte oltre la fine del buffer.
        storage.assign (((size_t) size + 3) / 4, 0.0f);
        std::memcpy (storage.data(), data, (size_t) size);

        return parseXwt (storage.data(), (size_t) size);
    }

    return std::nullopt;
}
} // namespace

std::unique_ptr<MipTable> buildMipTable (const BlobView& blob)
{
    // MipTable::kMaxLevel è fisso: al livello più alto restano (frameSize >> kMaxLevel) / 2
    // armoniche. Sotto 2^(kMaxLevel + 1) campioni quel conto scende a zero e il livello
    // sarebbe silenzio, peggio di nessun livello: si rifiuta prima di costruire qualunque
    // cosa. Le vere wavetable sono sempre da 2048 campioni, quindi non tocca l'uso reale.
    if (blob.frameSize < (1 << (MipTable::kMaxLevel + 1)))
        return nullptr;

    auto table = std::make_unique<MipTable> (blob.frames, blob.frameSize);

    // Costruzione tavola: solo message thread (buildMipTable non è mai chiamata dal thread audio),
    // quindi allocare e calcolare qui non viola le regole real-time.
    const int fftOrder = fftOrderFor (blob.frameSize);
    juce::dsp::FFT fft { fftOrder };
    std::vector<juce::dsp::Complex<float>> timeDomain ((size_t) blob.frameSize);
    std::vector<juce::dsp::Complex<float>> spectrum ((size_t) blob.frameSize);
    std::vector<juce::dsp::Complex<float>> bandLimited ((size_t) blob.frameSize);
    std::vector<juce::dsp::Complex<float>> result ((size_t) blob.frameSize);

    for (int frame = 0; frame < blob.frames; ++frame)
    {
        const auto* source = blob.frame (frame);

        for (int i = 0; i < blob.frameSize; ++i)
            timeDomain[(size_t) i] = { source[i], 0.0f };

        fft.perform (timeDomain.data(), spectrum.data(), false);

        for (int level = 0; level <= MipTable::kMaxLevel; ++level)
        {
            const int harmonics = table->harmonicsAtLevel (level);

            // Si azzera tutto sopra `harmonics` in entrambe le metà dello spettro: il buffer
            // resta lungo frameSize, quindi la trasformata inversa è dello stesso ordine della
            // diretta e il suo 1/N annulla esattamente quello della diretta — nessun fattore
            // di scala da rimettere a mano. Mantenere il gemello negativo (bin frameSize - h)
            // è ciò che tiene il risultato reale: senza, l'antitrasformata avrebbe una parte
            // immaginaria e la forma d'onda uscirebbe deformata.
            std::fill (bandLimited.begin(), bandLimited.end(), juce::dsp::Complex<float> { 0.0f, 0.0f });

            bandLimited[0] = spectrum[0];

            for (int bin = 1; bin < harmonics; ++bin)
            {
                bandLimited[(size_t) bin] = spectrum[(size_t) bin];
                bandLimited[(size_t) (blob.frameSize - bin)] = spectrum[(size_t) (blob.frameSize - bin)];
            }

            fft.perform (bandLimited.data(), result.data(), true);

            auto* destination = table->writePointer (frame, level);

            for (int i = 0; i < blob.frameSize; ++i)
                destination[i] = result[(size_t) i].real();
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

        auto built = buildMipTable (*blob);

        if (built == nullptr)
            return; // frame troppo corto per tutti i livelli: si resta sulla tavola precedente

        tables_[(size_t) index] = std::move (built);
    }

    active_.store (tables_[(size_t) index].get(), std::memory_order_release);
}
} // namespace dsp
