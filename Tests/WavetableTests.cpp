#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableOscillator.h"
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

        beginTest ("un frame troppo corto per tutti i livelli viene rifiutato, non crasha");
        {
            // 32 campioni < 2^kMaxLevel (64): al livello piu alto la dimensione andrebbe
            // a zero e l'ordine della FFT sarebbe negativo. Deve tornare nullptr, non UB.
            const auto bytes = makeHarmonicBlob (32, 1);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());

            const auto table = dsp::buildMipTable (*view);
            expect (table == nullptr, "un frame da 32 campioni non puo riempire tutti i livelli");
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

struct OscillatorTests final : juce::UnitTest
{
    OscillatorTests() : juce::UnitTest ("WavetableOscillator", "dsp") {}

    void runTest() override
    {
        beginTest ("il livello scelto non contiene armoniche sopra Nyquist");
        {
            // A 44.1 kHz, un La4 (440 Hz) ammette ~50 armoniche → serve un livello
            // da 128 campioni (64 armoniche) o più corto.
            expectEquals (dsp::levelForFrequency (440.0f, 44100.0, 2048), 5);
            // Un La1 (55 Hz) ammette ~400 armoniche → livello 2 (512 campioni, 256 armoniche).
            expectEquals (dsp::levelForFrequency (55.0f, 44100.0, 2048), 2);
            // Frequenze assurde non devono uscire dai limiti.
            expectEquals (dsp::levelForFrequency (0.0f, 44100.0, 2048), dsp::MipTable::kMaxLevel);
            expectEquals (dsp::levelForFrequency (20000.0f, 44100.0, 2048), dsp::MipTable::kMaxLevel);
        }

        beginTest ("senza tavola l'oscillatore tace invece di dereferenziare");
        {
            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (nullptr);
            osc.setFrequencyHz (440.0f);
            for (int i = 0; i < 64; ++i)
                expectWithinAbsoluteError (osc.getSample(), 0.0f, 0.0f);
        }

        beginTest ("un seno in tavola esce come un seno");
        {
            const auto bytes = makeHarmonicBlob (2048, 1); // una sola armonica
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (table.get());
            osc.setFramePosition (0.0f);
            osc.setFrequencyHz (441.0f); // 100 campioni per ciclo esatti

            std::vector<float> rendered (100);
            for (auto& s : rendered)
                s = osc.getSample();

            float maxError = 0.0f;
            for (int i = 0; i < 100; ++i)
                maxError = juce::jmax (maxError, std::abs (rendered[(size_t) i]
                                                           - std::sin (2.0f * juce::MathConstants<float>::pi * (float) i / 100.0f)));

            expect (maxError < 0.02f, "errore massimo " + juce::String (maxError));
        }

        beginTest ("la posizione fra due frame interpola invece di saltare");
        {
            // Due frame: costante -1 e costante +1 (l'interpolazione è leggibile a occhio).
            const uint32_t frames = 2, frameSize = 64;
            std::vector<char> bytes (12 + (size_t) frames * frameSize * sizeof (float));
            std::memcpy (bytes.data(), "XWT1", 4);
            std::memcpy (bytes.data() + 4, &frames, 4);
            std::memcpy (bytes.data() + 8, &frameSize, 4);
            for (uint32_t i = 0; i < frameSize; ++i)
            {
                const float lo = -1.0f, hi = 1.0f;
                std::memcpy (bytes.data() + 12 + i * sizeof (float), &lo, sizeof (float));
                std::memcpy (bytes.data() + 12 + (frameSize + i) * sizeof (float), &hi, sizeof (float));
            }

            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (table.get());
            osc.setFrequencyHz (100.0f);
            osc.setFramePosition (0.5f);

            expectWithinAbsoluteError (osc.getSample(), 0.0f, 0.05f);

            osc.setFramePosition (0.0f);
            expectWithinAbsoluteError (osc.getSample(), -1.0f, 0.05f);
        }

        beginTest ("frequenze negative o sopra la sample rate restano finite e in range");
        {
            const auto bytes = makeHarmonicBlob (2048, 1);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            dsp::WavetableOscillator osc;
            osc.prepare (44100.0);
            osc.setTable (table.get());
            osc.setFramePosition (0.0f);

            osc.setFrequencyHz (-441.0f); // frequenza negativa
            for (int i = 0; i < 200; ++i)
            {
                const auto sample = osc.getSample();
                expect (std::isfinite (sample), "campione " + juce::String (i) + " non finito");
                expect (std::abs (sample) < 10.0f, "campione " + juce::String (i) + " fuori range");
            }

            osc.setFrequencyHz (96000.0f); // sopra la sample rate (44100 Hz)
            for (int i = 0; i < 200; ++i)
            {
                const auto sample = osc.getSample();
                expect (std::isfinite (sample), "campione " + juce::String (i) + " non finito");
                expect (std::abs (sample) < 10.0f, "campione " + juce::String (i) + " fuori range");
            }
        }
    }
};

static OscillatorTests oscillatorTests;
