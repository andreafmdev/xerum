#include "dsp/WavetableBlob.h"

#include <juce_core/juce_core.h>

#include <cstring>
#include <vector>

namespace
{
/** Blob .xwt sintetico: `frames` frame di `frameSize` campioni, tutti a `value`. */
std::vector<char> makeBlob (uint32_t frames, uint32_t frameSize, float value = 0.5f)
{
    std::vector<char> bytes (12 + (size_t) frames * frameSize * sizeof (float));
    std::memcpy (bytes.data(), "XWT1", 4);
    std::memcpy (bytes.data() + 4, &frames, 4);
    std::memcpy (bytes.data() + 8, &frameSize, 4);

    for (size_t i = 0; i < (size_t) frames * frameSize; ++i)
        std::memcpy (bytes.data() + 12 + i * sizeof (float), &value, sizeof (float));

    return bytes;
}
} // namespace

struct WavetableBlobTests final : juce::UnitTest
{
    WavetableBlobTests() : juce::UnitTest ("WavetableBlob", "dsp") {}

    void runTest() override
    {
        beginTest ("un blob valido viene accettato e i frame sono raggiungibili");
        {
            const auto bytes = makeBlob (4, 8, 0.25f);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());
            expectEquals (view->frames, 4);
            expectEquals (view->frameSize, 8);
            expectWithinAbsoluteError (view->frame (3)[7], 0.25f, 1.0e-6f);
        }

        beginTest ("magic sbagliato: rifiutato");
        {
            auto bytes = makeBlob (2, 4);
            bytes[1] = 'X';
            expect (! dsp::parseXwt (bytes.data(), bytes.size()).has_value());
        }

        beginTest ("blob troncato: rifiutato");
        {
            const auto bytes = makeBlob (4, 8);
            expect (! dsp::parseXwt (bytes.data(), bytes.size() - 4).has_value());
        }

        beginTest ("frameSize non potenza di due: rifiutato");
        {
            const auto bytes = makeBlob (2, 6);
            expect (! dsp::parseXwt (bytes.data(), bytes.size()).has_value());
        }

        beginTest ("dimensioni assurde: rifiutate senza leggere fuori");
        {
            auto bytes = makeBlob (2, 4);
            const uint32_t huge = 1000000;
            std::memcpy (bytes.data() + 4, &huge, 4);
            expect (! dsp::parseXwt (bytes.data(), bytes.size()).has_value());
        }

        beginTest ("puntatore nullo o buffer minuscolo: rifiutati");
        {
            expect (! dsp::parseXwt (nullptr, 1024).has_value());
            const char tiny[4] = { 'X', 'W', 'T', '1' };
            expect (! dsp::parseXwt (tiny, sizeof (tiny)).has_value());
        }
    }
};

static WavetableBlobTests wavetableBlobTests;
