#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableOscillator.h"
#include "dsp/WavetableStore.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

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
        beginTest ("ogni livello dimezza le armoniche, la lunghezza resta piena");
        {
            const auto bytes = makeHarmonicBlob (2048, 64);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());

            const auto table = dsp::buildMipTable (*view);
            expect (table != nullptr);
            expectEquals (table->getNumFrames(), 1);

            // La limitazione di banda vive nello spettro, non nella lunghezza del buffer:
            // decimare anche quest'ultima faceva leggere all'oscillatore tavole da 64 o 32
            // campioni sulle note medio-alte, dove l'interpolazione lineare distorce piu' di
            // quanto il filtro tolga.
            expectEquals (table->getFrameSize(), 2048);
            expectEquals (table->harmonicsAtLevel (0), 1024);
            expectEquals (table->harmonicsAtLevel (3), 128);
            expectEquals (table->harmonicsAtLevel (dsp::MipTable::kMaxLevel), 16);
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

        beginTest ("i livelli alti tagliano davvero le armoniche sopra la loro Nyquist");
        {
            const auto bytes = makeHarmonicBlob (2048, 200);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            const auto* level0 = table->samples (0, 0);
            const auto* level4 = table->samples (0, 4); // 64 armoniche conservate

            expectEquals (table->harmonicsAtLevel (4), 64);

            // Ora che ogni livello resta lungo 2048 campioni la misura e' diretta: l'armonica
            // 100 non e' rappresentabile "in modo ambiguo" come su un buffer da 128 campioni,
            // dev'essere semplicemente sparita. Il vecchio test doveva passare per l'identita'
            // con la sua immagine speculare (bin 28) proprio perche' il buffer era corto.
            const auto amp100 = harmonicAmplitude (level4, 2048, 100);
            const auto amp70 = harmonicAmplitude (level4, 2048, 70);
            expect (amp100 < 1.0e-4f, "l'armonica 100 deve essere tolta, misurata " + juce::String (amp100));
            expect (amp70 < 1.0e-4f, "l'armonica 70 deve essere tolta, misurata " + juce::String (amp70));

            // Le armoniche davvero sotto il limite del livello devono restare intatte, con la
            // stessa ampiezza che hanno al livello 0: la limitazione di banda non deve
            // attenuare quello che tiene.
            for (int h : { 1, 10, 28, 63 })
            {
                const auto atLevel0 = harmonicAmplitude (level0, 2048, h);
                const auto atLevel4 = harmonicAmplitude (level4, 2048, h);
                expect (atLevel0 > 0.005f, "l'armonica " + juce::String (h) + " deve esistere al livello 0");
                expectWithinAbsoluteError (atLevel4, atLevel0, atLevel0 * 0.02f,
                        "armonica " + juce::String (h) + ": livello 4 " + juce::String (atLevel4)
                            + ", livello 0 " + juce::String (atLevel0));
            }
        }

        beginTest ("una nota media suonata davvero non riporta indietro nessun alias");
        {
            // Controllo end-to-end (spec 10): non basta che il MipTable sia corretto in
            // isolamento (test sopra, che chiama table->samples() direttamente) — deve restare
            // corretto quando l'oscillatore lo suona per davvero: setFrequencyHz() sceglie il
            // livello da solo (levelForFrequency), poi getSample() lo legge.
            //
            // La nota e' 44100 * 55 / 8192 Hz: 55 cicli esatti dentro il buffer di analisi da
            // 8192 campioni, quindi le armoniche cadono su bin interi senza leakage. 55 non
            // divide 8192, e questo e' il punto: se un'armonica sopra il limite si ripiegasse,
            // il suo alias cadrebbe al bin 8192 - 55h, che non e' mai multiplo di 55 — cioe'
            // **non** puo' nascondersi dentro un'armonica legittima. Con un rapporto che divide
            // (il vecchio test usava 64 cicli su 8192) ogni alias ricade esattamente su
            // un'armonica vera e la misura non distingue piu' le due cose.
            const auto bytes = makeHarmonicBlob (2048, 200);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);

            constexpr double sampleRate = 44100.0;
            constexpr int numSamples = 8192;
            constexpr int fftOrder = 13;              // 2^13 == numSamples
            constexpr int fundamentalBin = 55;
            const auto noteHz = (float) (sampleRate * (double) fundamentalBin / (double) numSamples);

            const auto level = dsp::levelForFrequency (noteHz, sampleRate, table->getFrameSize());
            expectEquals (level, 4, "il test assume che questa nota scelga il livello 4 (64 armoniche)");
            const auto keptHarmonics = table->harmonicsAtLevel (level);

            dsp::WavetableOscillator osc;
            osc.prepare (sampleRate);
            osc.setTable (table.get());
            osc.setFramePosition (0.0f);
            osc.setFrequencyHz (noteHz);

            std::vector<float> fftData ((size_t) numSamples * 2, 0.0f);
            for (int i = 0; i < numSamples; ++i)
                fftData[(size_t) i] = osc.getSample();

            juce::dsp::FFT fft (fftOrder);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

            // Energia dentro le armoniche legittime contro tutto il resto dello spettro.
            // Un filtro che non taglia (o che taglia e poi ripiega) riversa energia fuori dai
            // bin multipli di 55: verificato disattivando il taglio (harmonics = frameSize / 2
            // a ogni livello in buildMipTable), il rapporto sale da ~0.001 a ~0.35.
            const auto isHarmonicBin = [keptHarmonics] (int bin)
            {
                if (bin % fundamentalBin != 0)
                    return false;
                const auto h = bin / fundamentalBin;
                return h >= 1 && h <= keptHarmonics;
            };

            double inBand = 0.0;
            double outOfBand = 0.0;

            for (int bin = 1; bin < numSamples / 2; ++bin)
            {
                const auto magnitude = (double) fftData[(size_t) bin];
                (isHarmonicBin (bin) ? inBand : outOfBand) += magnitude * magnitude;
            }

            expect (inBand > 0.0, "la nota deve produrre qualcosa");
            const auto ratio = outOfBand / inBand;
            expect (ratio < 0.01, "energia fuori dalle armoniche conservate: " + juce::String (ratio)
                                      + " dell'energia in banda");
        }

        beginTest ("un frame troppo corto per tutti i livelli viene rifiutato, non crasha");
        {
            // 64 campioni < 2^(kMaxLevel + 1) (128): al livello piu alto resterebbero
            // (64 >> 6) / 2 == 0 armoniche, cioe' silenzio. Deve tornare nullptr, non una
            // tavola con un livello muto.
            for (uint32_t frameSize : { 32u, 64u })
            {
                const auto bytes = makeHarmonicBlob (frameSize, 1);
                const auto view = dsp::parseXwt (bytes.data(), bytes.size());
                expect (view.has_value());

                const auto table = dsp::buildMipTable (*view);
                expect (table == nullptr, "un frame da " + juce::String ((int) frameSize)
                                              + " campioni non puo riempire tutti i livelli");
            }

            // 128 campioni invece bastano: un'armonica al livello piu alto.
            {
                const auto bytes = makeHarmonicBlob (128, 1);
                const auto view = dsp::parseXwt (bytes.data(), bytes.size());
                const auto table = dsp::buildMipTable (*view);
                expect (table != nullptr);
                expectEquals (table->harmonicsAtLevel (dsp::MipTable::kMaxLevel), 1);
            }
        }
    }
};

static MipTableTests mipTableTests;

struct WavetableStoreTests final : juce::UnitTest
{
    WavetableStoreTests() : juce::UnitTest ("WavetableStore", "dsp") {}

    void runTest() override
    {
        beginTest ("before setActive no table is active");
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
            expectEquals (first->getFrameSize(), 2048);
        }

        beginTest ("returning to a previously built table returns same pointer");
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
            // 128 e non 64: sotto 2^(kMaxLevel + 1) buildMipTable rifiuta il blob.
            const uint32_t frames = 2, frameSize = 128;
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
