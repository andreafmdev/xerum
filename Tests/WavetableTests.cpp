#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"

#include <juce_core/juce_core.h>

#include <cmath>
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

namespace
{
/** Blob con un solo frame contenente la somma delle prime `harmonics` armoniche. */
std::vector<char> makeHarmonicBlob (uint32_t frameSize, int harmonics)
{
    const uint32_t frames = 1;
    std::vector<char> bytes (12 + (size_t) frames * frameSize * sizeof (float));
    std::memcpy (bytes.data(), "XWT1", 4);
    std::memcpy (bytes.data() + 4, &frames, 4);
    std::memcpy (bytes.data() + 8, &frameSize, 4);

    for (uint32_t i = 0; i < frameSize; ++i)
    {
        float v = 0.0f;
        for (int h = 1; h <= harmonics; ++h)
            v += std::sin (2.0f * juce::MathConstants<float>::pi * (float) h * (float) i / (float) frameSize) / (float) h;

        std::memcpy (bytes.data() + 12 + i * sizeof (float), &v, sizeof (float));
    }

    return bytes;
}

/** Ampiezza dell'armonica h in un ciclo lungo `size`. */
float harmonicAmplitude (const float* samples, int size, int h)
{
    float re = 0.0f;
    float im = 0.0f;

    for (int i = 0; i < size; ++i)
    {
        const auto a = 2.0f * juce::MathConstants<float>::pi * (float) h * (float) i / (float) size;
        re += samples[i] * std::cos (a);
        im -= samples[i] * std::sin (a);
    }

    return 2.0f * std::sqrt (re * re + im * im) / (float) size;
}
} // namespace

struct MipTableTests final : juce::UnitTest
{
    MipTableTests() : juce::UnitTest ("MipTable", "dsp") {}

    void runTest() override
    {
        beginTest ("ogni livello dimezza la lunghezza");
        {
            const auto bytes = makeHarmonicBlob (2048, 64);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());

            const auto table = dsp::buildMipTable (*view);
            expect (table != nullptr);
            expectEquals (table->getNumFrames(), 1);
            expectEquals (table->sizeAtLevel (0), 2048);
            expectEquals (table->sizeAtLevel (3), 256);
            expectEquals (table->sizeAtLevel (dsp::MipTable::kMaxLevel), 32);
        }

        beginTest ("il livello 0 conserva la forma d'onda originale");
        {
            const auto bytes = makeHarmonicBlob (2048, 64);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            float maxError = 0.0f;
            for (int i = 0; i < 2048; ++i)
                maxError = juce::jmax (maxError, std::abs (table->samples (0, 0)[i] - view->frame (0)[i]));

            expect (maxError < 1.0e-4f, "errore massimo " + juce::String (maxError));
        }

        beginTest ("i livelli alti tagliano le armoniche sopra la loro Nyquist");
        {
            const auto bytes = makeHarmonicBlob (2048, 200);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            // Livello 4 → 128 campioni → al massimo 64 armoniche.
            const auto* level4 = table->samples (0, 4);
            expect (harmonicAmplitude (level4, 128, 10) > 0.05f, "l'armonica 10 deve restare");
            expect (harmonicAmplitude (level4, 128, 63) < 0.05f, "l'armonica 63 deve essere attenuata o assente");
        }
    }
};

static MipTableTests mipTableTests;

struct WavetableStoreTests final : juce::UnitTest
{
    WavetableStoreTests() : juce::UnitTest ("WavetableStore", "dsp") {}

    void runTest() override
    {
        beginTest ("prima di setActive non c'è nessuna tavola attiva");
        {
            dsp::WavetableStore store;
            expect (store.active() == nullptr);
            expect (store.getNumTables() > 0);
        }

        beginTest ("setActive costruisce la tavola e la pubblica");
        {
            dsp::WavetableStore store;
            store.setActive (0);
            const auto* first = store.active();
            expect (first != nullptr);
            expectEquals (first->getNumFrames(), 64);
            expectEquals (first->sizeAtLevel (0), 2048);
        }

        beginTest ("tornare su una tavola già costruita restituisce lo stesso puntatore");
        {
            dsp::WavetableStore store;
            store.setActive (0);
            const auto* first = store.active();
            store.setActive (1);
            store.setActive (0);
            expect (store.active() == first, "le tavole costruite non vengono mai liberate");
        }

        beginTest ("un indice fuori range non cambia la tavola attiva");
        {
            dsp::WavetableStore store;
            store.setActive (0);
            const auto* first = store.active();
            store.setActive (99);
            store.setActive (-1);
            expect (store.active() == first);
        }
    }
};

static WavetableStoreTests wavetableStoreTests;
