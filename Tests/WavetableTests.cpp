#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableOscillator.h"
#include "dsp/WavetableStore.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <cstring>
#include <memory>
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
            // Il livello piu' alto tiene una sola armonica: la fondamentale, cioe' una sinusoide.
            expectEquals (table->harmonicsAtLevel (dsp::MipTable::kMaxLevel), 1);
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
            expectWithinAbsoluteError (level, 4.781f, 0.002f,
                    "il test assume che questa nota mescoli i livelli 4 (64 armoniche) e 5 (32)");
            // Il crossfade sfuma verso il livello piu' scuro ma non puo' aggiungere niente
            // sopra il piu' brillante dei due: l'ultima armonica possibile resta quella del
            // livello (int) level, tutto il resto sarebbe alias.
            const auto keptHarmonics = table->harmonicsAtLevel ((int) level);

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

        beginTest ("nessun alias udibile su tutta l'estensione della tastiera");
        {
            // Il test sopra copre una nota media. Questo copre l'estensione intera: la piramide
            // deve avere abbastanza livelli perche' anche l'ultima ottava trovi un livello le cui
            // armoniche stanno tutte sotto Nyquist. Se la piramide si ferma troppo presto,
            // levelForFrequency restituisce l'ultimo livello disponibile e quel livello ripiega.
            const auto bytes = makeHarmonicBlob (2048, 200);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            const auto table = dsp::buildMipTable (*view);
            expect (table != nullptr);

            constexpr double sampleRate = 44100.0;
            constexpr int numSamples = 1 << 15;

            for (int note = 36; note <= 120; note += 6)
            {
                const auto noteHz = 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);

                dsp::WavetableOscillator osc;
                osc.prepare (sampleRate);
                osc.setTable (table.get());
                osc.setFramePosition (0.0f);
                osc.setFrequencyHz (noteHz);

                std::vector<float> fftData ((size_t) numSamples * 2, 0.0f);
                for (int i = 0; i < numSamples; ++i)
                {
                    // Blackman-Harris a 4 termini: i sidelobi stanno a -92 dB, abbastanza in basso
                    // da non coprire l'alias che si sta cercando.
                    const double t = 2.0 * juce::MathConstants<double>::pi * i / (numSamples - 1);
                    const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t)
                                   - 0.01168 * std::cos (3 * t);
                    fftData[(size_t) i] = (float) (osc.getSample() * w);
                }

                juce::dsp::FFT fft (15);
                fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

                const double binHz = sampleRate / numSamples;
                std::vector<char> harmonic ((size_t) numSamples / 2, 0);
                double inBand = 0.0;

                for (int h = 1; h * noteHz < sampleRate * 0.5 - binHz * 6.0; ++h)
                {
                    const int centre = (int) std::lround (h * noteHz / binHz);
                    for (int bin = centre - 5; bin <= centre + 5; ++bin)
                        if (bin > 0 && bin < numSamples / 2)
                        {
                            harmonic[(size_t) bin] = 1;
                            inBand += (double) fftData[(size_t) bin] * fftData[(size_t) bin];
                        }
                }

                double outOfBand = 0.0;
                const int firstBin = (int) (25.0 / binHz) + 1;
                for (int bin = firstBin; bin < numSamples / 2; ++bin)
                    if (! harmonic[(size_t) bin])
                        outOfBand += (double) fftData[(size_t) bin] * fftData[(size_t) bin];

                expect (inBand > 0.0, "la nota " + juce::String (note) + " deve produrre qualcosa");
                const auto db = 10.0 * std::log10 (outOfBand / inBand);
                expect (db < -60.0, "nota " + juce::String (note) + " (" + juce::String (noteHz, 1)
                                        + " Hz): energia non armonica a " + juce::String (db, 1)
                                        + " dB, deve stare sotto -60 dB");
            }
        }

        beginTest ("un frame troppo corto per tutti i livelli viene rifiutato, non crasha");
        {
            // Sotto 2^(kMaxLevel + 1) == 2048 campioni, al livello piu alto resterebbero
            // (frameSize >> kMaxLevel) / 2 == 0 armoniche, cioe' silenzio. Deve tornare
            // nullptr, non una tavola con un livello muto.
            for (uint32_t frameSize : { 32u, 64u, 128u, 1024u })
            {
                const auto bytes = makeHarmonicBlob (frameSize, 1);
                const auto view = dsp::parseXwt (bytes.data(), bytes.size());
                expect (view.has_value());

                const auto table = dsp::buildMipTable (*view);
                expect (table == nullptr, "un frame da " + juce::String ((int) frameSize)
                                              + " campioni non puo riempire tutti i livelli");
            }

            // 2048 campioni invece bastano: un'armonica al livello piu alto. E' anche la
            // lunghezza di tutte le tavole vere, quindi il limite non tocca l'uso reale.
            {
                const auto bytes = makeHarmonicBlob (2048, 1);
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
            // La funzione restituisce un livello frazionario: la parte intera e' il primo
            // livello sicuro (quello che il codice sceglieva prima), la frazione e' il peso
            // del crossfade verso il livello successivo.
            //
            // A 44.1 kHz, un La4 (440 Hz) ammette ~50 armoniche → serve un livello che ne
            // tenga 64 o meno, cioe' il 5.
            const auto la4 = dsp::levelForFrequency (440.0f, 44100.0, 2048);
            expectEquals ((int) la4, 5);
            expectWithinAbsoluteError (la4, 5.353f, 0.002f);
            // Un La1 (55 Hz) ammette ~400 armoniche → livello 2 (256 armoniche).
            const auto la1 = dsp::levelForFrequency (55.0f, 44100.0, 2048);
            expectEquals ((int) la1, 2);
            expectWithinAbsoluteError (la1, 2.353f, 0.002f);
            // Un'ottava sposta il livello di esattamente 1: e' questo che rende il
            // crossfade continuo ai confini, dove la coppia di livelli cambia.
            expectWithinAbsoluteError (la4 - la1, 3.0f, 1.0e-4f);
            // Frequenze assurde non devono uscire dai limiti.
            expectWithinAbsoluteError (dsp::levelForFrequency (0.0f, 44100.0, 2048),
                    (float) dsp::MipTable::kMaxLevel, 0.0f);
            expectWithinAbsoluteError (dsp::levelForFrequency (20000.0f, 44100.0, 2048),
                    (float) dsp::MipTable::kMaxLevel, 0.0f);
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
            // 2048 e non meno: sotto 2^(kMaxLevel + 1) buildMipTable rifiuta il blob.
            const uint32_t frames = 2, frameSize = 2048;
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

namespace
{
/**
 * Blackman-Harris a 7 termini. Serve per misurare l'aliasing: con una Hann il pavimento
 * di misura sta a -44 dB e copre tutto quello che si sta cercando; qui i sidelobi stanno
 * sotto -180 dB, cioe' sotto il rumore numerico di una FFT in singola precisione.
 */
double blackmanHarris7 (int i, int n) noexcept
{
    static constexpr double coefficients[] = { 0.27105140069342, -0.43329793923448, 0.21812299954311,
                                               -0.06592544638803, 0.01081174209837, -0.00077658482522,
                                               0.00001388721735 };

    const double t = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) n;
    double w = 0.0;

    for (int k = 0; k < 7; ++k)
        w += coefficients[(size_t) k] * std::cos ((double) k * t);

    return w;
}

float midiNoteHz (int note) noexcept
{
    return 440.0f * std::pow (2.0f, ((float) note - 69.0f) / 12.0f);
}

/** Suona la nota e ne restituisce lo spettro di ampiezza (bin 0..numSamples/2). */
std::vector<float> renderSpectrum (const dsp::MipTable& table, float noteHz, double sampleRate, int fftOrder)
{
    const int numSamples = 1 << fftOrder;

    dsp::WavetableOscillator osc;
    osc.prepare (sampleRate);
    osc.setTable (&table);
    osc.setFramePosition (0.0f);
    osc.setFrequencyHz (noteHz);

    std::vector<float> data ((size_t) numSamples * 2, 0.0f);
    for (int i = 0; i < numSamples; ++i)
        data[(size_t) i] = (float) ((double) osc.getSample() * blackmanHarris7 (i, numSamples));

    juce::dsp::FFT fft (fftOrder);
    fft.performFrequencyOnlyForwardTransform (data.data(), true);
    return data;
}

/**
 * Centroide spettrale pesato in energia, diviso la fondamentale: quante volte la
 * fondamentale sta il "baricentro" del timbro. E' la misura della brillantezza che
 * il crossfade fra livelli deve rendere continua lungo la tastiera.
 */
double centroidOverFundamental (const std::vector<float>& spectrum, int numSamples, double sampleRate, double fundamental)
{
    const double binHz = sampleRate / (double) numSamples;
    double weighted = 0.0;
    double total = 0.0;

    for (int bin = 1; bin < numSamples / 2; ++bin)
    {
        const double power = (double) spectrum[(size_t) bin] * (double) spectrum[(size_t) bin];
        weighted += power * (double) bin * binHz;
        total += power;
    }

    return total > 0.0 ? weighted / total / fundamental : 0.0;
}

/** Energia non armonica / energia armonica, in dB. Esclude +-12 bin attorno a ogni armonica. */
double aliasToHarmonicDb (const std::vector<float>& spectrum, int numSamples, double sampleRate, double fundamental)
{
    const double binHz = sampleRate / (double) numSamples;
    const int half = numSamples / 2;

    std::vector<char> harmonic ((size_t) half, 0);
    double inBand = 0.0;

    for (int h = 1; (double) h * fundamental < sampleRate * 0.5; ++h)
    {
        const int centre = (int) std::lround ((double) h * fundamental / binHz);

        for (int bin = centre - 12; bin <= centre + 12; ++bin)
            if (bin > 0 && bin < half && ! harmonic[(size_t) bin])
            {
                harmonic[(size_t) bin] = 1;
                inBand += (double) spectrum[(size_t) bin] * (double) spectrum[(size_t) bin];
            }
    }

    double outOfBand = 0.0;
    for (int bin = 1; bin < half; ++bin)
        if (! harmonic[(size_t) bin])
            outOfBand += (double) spectrum[(size_t) bin] * (double) spectrum[(size_t) bin];

    return 10.0 * std::log10 (outOfBand / juce::jmax (inBand, 1.0e-30));
}
} // namespace

struct MipCrossfadeTests final : juce::UnitTest
{
    MipCrossfadeTests() : juce::UnitTest ("MipCrossfade", "dsp") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("la brillantezza cambia con continuita' da una nota all'altra");
        {
            // Rampa piena: 1024 armoniche a 1/h, cioe' tutto quello che un frame da 2048
            // campioni puo' contenere. Serve ricca perche' cosi' il livello scelto conta
            // davvero su tutta la tastiera, dalla nota 24 in su. Non si usa una tavola vera
            // (.xwt): la misura deve dipendere dalla scelta del livello, non dai numeri
            // esatti di un file che puo' essere rigenerato.
            const auto bytes = makeHarmonicBlob (2048, 1024);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());
            const auto table = dsp::buildMipTable (*view);
            expect (table != nullptr);

            constexpr int fftOrder = 15;
            constexpr int numSamples = 1 << fftOrder;

            std::vector<double> centroid;
            for (int note = 24; note <= 120; ++note)
            {
                const auto noteHz = midiNoteHz (note);
                const auto spectrum = renderSpectrum (*table, noteHz, sampleRate, fftOrder);
                centroid.push_back (centroidOverFundamental (spectrum, numSamples, sampleRate, (double) noteHz));
            }

            double worstStep = 0.0;
            int worstNote = 0;

            for (size_t i = 1; i < centroid.size(); ++i)
            {
                expect (centroid[i] > 0.0, "la nota deve produrre qualcosa");
                const auto step = std::abs (centroid[i] / centroid[i - 1] - 1.0);

                if (step > worstStep)
                {
                    worstStep = step;
                    worstNote = 24 + (int) i;
                }
            }

            // Con il livello intero la brillantezza restava congelata per un'ottava e poi
            // crollava di colpo: gradini dall'8% al 18% del centroide diviso la fondamentale
            // alle note 31, 43, 55, 67, 79, 91, 103, 115. Salendo di un semitono il suono non
            // si schiariva, si spegneva — udibile come irregolarita' timbrica a ogni ottava.
            logMessage ("centroide/fondamentale: " + juce::String (centroid.front(), 3) + " alla nota 24, "
                        + juce::String (centroid.back(), 3) + " alla nota 120, gradino massimo "
                        + juce::String (worstStep * 100.0, 2) + "% alla nota " + juce::String (worstNote));

            expect (worstStep < 0.03, "gradino massimo del centroide " + juce::String (worstStep * 100.0, 2)
                                          + "% fra le note " + juce::String (worstNote - 1) + " e "
                                          + juce::String (worstNote) + ", deve stare sotto il 3%");
        }

        beginTest ("nessun alias sopra -70 dB su tutta la tastiera");
        {
            // Qui la rampa ha 128 armoniche, non 1024: sopra quella banda a dominare la misura
            // era la distorsione dell'interpolazione fra campioni della tavola invece
            // dell'aliasing della piramide, che e' quello che questo test sorveglia. Con
            // Lagrange-3 quel contributo e' sceso di 34 dB e la soglia sotto non lo tocca piu'
            // nemmeno da lontano, ma la banda resta questa: il test misura la piramide, e
            // cambiargli il segnale sotto cambierebbe cosa vuol dire il suo numero.
            const auto bytes = makeHarmonicBlob (2048, 128);
            const auto view = dsp::parseXwt (bytes.data(), bytes.size());
            expect (view.has_value());
            const auto table = dsp::buildMipTable (*view);
            expect (table != nullptr);

            constexpr int fftOrder = 16;
            constexpr int numSamples = 1 << fftOrder;

            double worst = -1000.0;
            int worstNote = 0;

            for (int note = 24; note <= 120; ++note)
            {
                const auto noteHz = midiNoteHz (note);
                const auto spectrum = renderSpectrum (*table, noteHz, sampleRate, fftOrder);
                const auto db = aliasToHarmonicDb (spectrum, numSamples, sampleRate, (double) noteHz);

                if (db > worst)
                {
                    worst = db;
                    worstNote = note;
                }
            }

            logMessage ("alias peggiore " + juce::String (worst, 1) + " dB alla nota " + juce::String (worstNote));

            // Il riferimento e' il livello intero, che misurato cosi' stava fra -72 e -123 dB:
            // il crossfade non puo' peggiorarlo perche' non legge mai un livello piu' brillante
            // di quello sicuro, ma e' esattamente la cosa che una formula sbagliata romperebbe
            // per prima.
            //
            // La soglia era -70 dB e il peggiore stava a -72.7 (nota 38), cioe' 2.7 dB di
            // margine: quel numero non lo faceva la piramide ma l'interpolazione lineare
            // dentro il frame. Passata a Lagrange-3 il peggiore e' -107.0 dB, sempre alla nota
            // 38. La soglia scende con lui: tenerla a -70 vorrebbe dire lasciar passare in
            // silenzio una regressione di 37 dB.
            expect (worst < -100.0, "nota " + juce::String (worstNote) + ": energia non armonica a "
                                        + juce::String (worst, 1) + " dB, deve stare sotto -100 dB");
        }
    }
};

static MipCrossfadeTests mipCrossfadeTests;

namespace
{
/**
 * Tavola con la piramide **spenta**: tutti i livelli contengono la stessa forma d'onda,
 * la somma delle prime `harmonics` armoniche a 1/h, eventualmente ruotata di `rotation`
 * campioni.
 *
 * Serve a misurare l'interpolatore e basta. Con una piramide vera il numero di armoniche
 * che si sentono non lo decide il test ma `levelForFrequency`: a 43.2 Hz il livello
 * suonato e' l'1.88, cioe' quasi tutto 256 armoniche, e una tavola da 512 misurerebbe la
 * piramide invece dell'interpolazione. Spegnendola, "N armoniche a f0" vuol dire davvero
 * N armoniche che arrivano all'interpolatore, che e' l'esperimento che serve: l'errore
 * d'interpolazione dipende dalla derivata della tavola fra un campione e l'altro, cioe'
 * esattamente da quante armoniche ci sono dentro.
 *
 * Il rovescio: qui l'alias misurato **non** e' quello dello strumento (che non suona mai
 * 512 armoniche a 43.2 Hz), e' il contributo di un solo stadio. Lo strumento intero resta
 * sorvegliato dal test sull'intera tastiera in MipCrossfadeTests.
 */
std::unique_ptr<dsp::MipTable> makeFlatMipTable (int frameSize, int harmonics, int rotation = 0)
{
    auto table = std::make_unique<dsp::MipTable> (1, frameSize);

    for (int i = 0; i < frameSize; ++i)
    {
        const auto source = i + rotation;
        double v = 0.0;

        for (int h = 1; h <= harmonics; ++h)
            v += std::sin (2.0 * juce::MathConstants<double>::pi * (double) h * (double) source / (double) frameSize) / (double) h;

        for (int level = 0; level <= dsp::MipTable::kMaxLevel; ++level)
            table->writePointer (0, level)[i] = (float) v;
    }

    return table;
}
} // namespace

struct WavetableInterpolationTests final : juce::UnitTest
{
    WavetableInterpolationTests() : juce::UnitTest ("WavetableInterpolation", "dsp") {}

    void runTest() override
    {
        beginTest ("il pavimento di aliasing dell'interpolazione crolla dimezzando le armoniche");
        {
            // 43.2 Hz su un frame da 2048 campioni e' il caso peggiore dello strumento: la
            // nota piu' grave che si suona davvero, cioe' il passo di lettura piu' lungo
            // (1.84 campioni di tavola per campione d'uscita) e quindi la frazione peggiore
            // da indovinare. Meno armoniche in tavola significa curva piu' liscia fra due
            // campioni: e' la stessa cosa che salire di un'ottava.
            //
            // La tavola e' sintetica e con la piramide spenta apposta (vedi makeFlatMipTable):
            // i .xwt su disco possono essere rigenerati, e qui si sorveglia un solo stadio.
            //
            // Prima, con l'interpolazione lineare a 2 punti, questa stessa misura dava
            // -58.3 / -65.3 / -74.3 / -83.1 dB: ~8 dB di guadagno per dimezzamento e basta.
            // La lineare sbaglia come la derivata seconda della tavola, Lagrange-3 come la
            // quarta: il pavimento non scende di un gradino, cambia pendenza — 18 dB piu' in
            // basso gia' nel caso peggiore, 46 nel migliore.
            constexpr double sampleRate = 48000.0;
            constexpr float noteHz = 43.2f;
            constexpr int fftOrder = 16;
            constexpr int numSamples = 1 << fftOrder;

            struct Case
            {
                int harmonics;
                double linearDb;
                double maxDb;
            };

            // Colonna 2: la misura con la lineare, cioe' quello che questo test deve battere.
            // Colonna 3: la soglia, ~4 dB sopra il misurato con Lagrange-3 (-72.3 / -87.6 /
            // -108.8 / -129.0 dB) — abbastanza da non essere fragile, abbastanza stretta da
            // non lasciar rientrare la lineare da nessuna parte.
            const Case cases[] = {
                { 512, -58.3, -68.0 },
                { 256, -65.3, -84.0 },
                { 128, -74.3, -105.0 },
                { 64, -83.1, -125.0 },
            };

            for (const auto& c : cases)
            {
                const auto table = makeFlatMipTable (2048, c.harmonics);
                const auto spectrum = renderSpectrum (*table, noteHz, sampleRate, fftOrder);
                const auto db = aliasToHarmonicDb (spectrum, numSamples, sampleRate, (double) noteHz);

                logMessage (juce::String (c.harmonics) + " armoniche a 43.2 Hz: " + juce::String (db, 1)
                            + " dB (lineare: " + juce::String (c.linearDb, 1) + " dB, guadagno "
                            + juce::String (c.linearDb - db, 1) + " dB)");

                expect (db < c.maxDb, juce::String (c.harmonics) + " armoniche: energia non armonica a "
                                          + juce::String (db, 1) + " dB, deve stare sotto "
                                          + juce::String (c.maxDb, 1) + " dB");
            }
        }

        beginTest ("a fase intera l'uscita e' il campione, non una media dei vicini");
        {
            // Proprieta' di Lagrange: ogni polinomio di base vale 1 sul proprio nodo e 0 su
            // tutti gli altri, quindi a frazione 0 l'uscita e' esattamente x[0]. E' anche la
            // sentinella sugli indici dei quattro tap: sbagliarne l'offset di uno sposta il
            // nodo e questo confronto salta subito, mentre la misura di aliasing continuerebbe
            // a sembrare plausibile.
            //
            // 2048 Hz di sample rate e 0.25 Hz di nota: l'incremento di fase e' 2^-13, esatto
            // in binario, e la posizione nel frame avanza di un quarto di campione per volta.
            // Un campione su quattro cade su un indice intero.
            constexpr int frameSize = 2048;
            const auto table = makeFlatMipTable (frameSize, 400);
            const auto* frame = table->samples (0, 0);

            dsp::WavetableOscillator osc;
            osc.prepare (2048.0);
            osc.setTable (table.get());
            osc.setFramePosition (0.0f);
            osc.setFrequencyHz (0.25f);

            float worst = 0.0f;
            int worstIndex = 0;

            for (int i = 0; i < 4 * 96; ++i)
            {
                const auto sample = osc.getSample();

                if (i % 4 == 0)
                {
                    const auto error = std::abs (sample - frame[i / 4]);

                    if (error > worst)
                    {
                        worst = error;
                        worstIndex = i / 4;
                    }
                }
            }

            expect (worst < 1.0e-6f, "a fase intera l'errore massimo e' " + juce::String (worst)
                                         + " al campione " + juce::String (worstIndex));
        }

        beginTest ("il frame e' ciclico su entrambi i lati: ruotarlo sposta il suono, non lo rompe");
        {
            // La finestra a 4 punti ha bisogno di x[-1] e di x[2]: due wrap, uno per lato.
            // Qui la formula non viene riscritta nel test, si verifica la proprieta' che il
            // wrap deve garantire: una tavola ruotata di un campione, letta un campione piu'
            // indietro, e' lo stesso identico segnale. Se un solo lato del wrap e' sbagliato
            // le due letture divergono proprio nei passi in cui la finestra scavalca il
            // confine — ed e' li' che partono, apposta.
            //
            // 400 armoniche: campioni adiacenti molto diversi fra loro, cosi' un tap preso
            // dal posto sbagliato non si confonde con il rumore di misura. Le due tavole
            // hanno campioni identici a meno della rotazione (piramide spenta), quindi le due
            // catene di moltiplicazioni sono le stesse: la differenza attesa e' zero esatto.
            constexpr int frameSize = 2048;
            const auto plain = makeFlatMipTable (frameSize, 400);
            const auto rotated = makeFlatMipTable (frameSize, 400, 1);

            const auto start = [] (dsp::WavetableOscillator& osc, const dsp::MipTable* table, float phase)
            {
                osc.prepare (2048.0);
                osc.setTable (table);
                osc.setFramePosition (0.0f);
                osc.setFrequencyHz (0.25f);
                osc.resetToPhase (phase);
            };

            // La tavola ruotata contiene x[i + 1]: per sentire la stessa cosa deve partire un
            // campione piu' indietro dell'altra. Entrambe partono a ridosso della fine del
            // frame e ci passano sopra entro i primi passi.
            dsp::WavetableOscillator ahead;
            dsp::WavetableOscillator behind;
            start (ahead, rotated.get(), (float) (frameSize - 2) / (float) frameSize);
            start (behind, plain.get(), (float) (frameSize - 1) / (float) frameSize);

            float worst = 0.0f;
            int worstStep = 0;

            for (int i = 0; i < 64; ++i)
            {
                const auto error = std::abs (ahead.getSample() - behind.getSample());

                if (error > worst)
                {
                    worst = error;
                    worstStep = i;
                }
            }

            expect (worst < 1.0e-6f, "attraversando il confine del frame le due letture divergono di "
                                         + juce::String (worst) + " al passo " + juce::String (worstStep));
        }
    }
};

static WavetableInterpolationTests wavetableInterpolationTests;

/**
 * Warp = sync: la fase letta corre 1 + 3·warp volte per periodo, cioe' da uno a quattro cicli
 * del frame dentro un periodo della nota, con lo stesso mapping che WaveDisplay disegna.
 */
struct WarpTests final : juce::UnitTest
{
    WarpTests() : juce::UnitTest ("WavetableOscillator: warp (sync)", "dsp") {}

    static std::vector<float> render (dsp::WavetableOscillator& osc, int numSamples)
    {
        std::vector<float> out ((size_t) numSamples);
        for (auto& v : out)
            v = osc.getSample();
        return out;
    }

    /** Modulo dello spettro (bin 0..N/2) del segnale, N potenza di due. */
    static std::vector<float> magnitudes (const std::vector<float>& x, int fftOrder)
    {
        std::vector<float> data (x.size() * 2, 0.0f);
        std::copy (x.begin(), x.end(), data.begin());
        juce::dsp::FFT fft (fftOrder);
        fft.performFrequencyOnlyForwardTransform (data.data(), true);
        data.resize (x.size() / 2 + 1);
        return data;
    }

    static double centroidBin (const std::vector<float>& mags)
    {
        double num = 0.0, den = 0.0;
        for (size_t i = 1; i < mags.size(); ++i)
        {
            const auto e = (double) mags[i] * (double) mags[i];
            num += (double) i * e;
            den += e;
        }
        return den > 0.0 ? num / den : 0.0;
    }

    void runTest() override
    {
        const auto bytes = makeHarmonicBlob (2048, 200);
        const auto view = dsp::parseXwt (bytes.data(), bytes.size());
        const auto table = dsp::buildMipTable (*view);

        constexpr double sampleRate = 44100.0;
        constexpr int numSamples = 8192;
        constexpr int fftOrder = 13;
        constexpr int fundamentalBin = 55;
        const auto noteHz = (float) (sampleRate * (double) fundamentalBin / (double) numSamples);

        const auto make = [&] (float warp)
        {
            dsp::WavetableOscillator osc;
            osc.prepare (sampleRate);
            osc.setTable (table.get());
            osc.setFramePosition (0.0f);
            osc.setFrequencyHz (noteHz);
            osc.setWarp (warp);
            return osc;
        };

        beginTest ("warp 0 e' l'identita': campione per campione come senza setWarp");
        {
            dsp::WavetableOscillator plain;
            plain.prepare (sampleRate);
            plain.setTable (table.get());
            plain.setFramePosition (0.0f);
            plain.setFrequencyHz (noteHz);
            auto zero = make (0.0f);

            const auto a = render (plain, 4096);
            const auto b = render (zero, 4096);
            int mismatches = 0;
            for (size_t i = 0; i < a.size(); ++i)
                mismatches += (a[i] != b[i]) ? 1 : 0;   // NOLINT: uguaglianza esatta voluta
            expectEquals (mismatches, 0, "warp 0 deve essere ×1 esatto, non un'approssimazione");
        }

        beginTest ("a warp 1 il segnale suona sulla quarta armonica: niente energia sulla prima, seconda e terza");
        {
            auto osc = make (1.0f);
            const auto mags = magnitudes (render (osc, numSamples), fftOrder);
            const auto at = [&] (int h) { return (double) mags[(size_t) (fundamentalBin * h)]; };
            expect (at (4) > 0.0, "la quarta armonica della nota c'e'");
            for (int h = 1; h <= 3; ++h)
                expect (at (h) < 1.0e-3 * at (4), "armonica " + juce::String (h) + " presente a warp 1: " + juce::String (at (h) / at (4)));
        }

        beginTest ("a warp 1 il livello mipmap segue la frequenza efficace: niente sopra il limite di 4f");
        {
            // Il limite e' quello che levelForFrequency sceglie per 4·f, non per f: con il livello
            // di f la tavola porterebbe quattro volte le armoniche che stanno sotto Nyquist.
            const auto level = dsp::levelForFrequency (noteHz * 4.0f, sampleRate, table->getFrameSize());
            const auto kept = table->harmonicsAtLevel ((int) level);
            auto osc = make (1.0f);
            const auto mags = magnitudes (render (osc, numSamples), fftOrder);

            double inside = 0.0, above = 0.0;
            for (size_t i = 1; i < mags.size(); ++i)
            {
                const auto e = (double) mags[i] * (double) mags[i];
                if ((int) i <= kept * fundamentalBin * 4) inside += e; else above += e;
            }
            expect (above < 0.01 * inside, "energia sopra il limite band-limited: " + juce::String (above / inside));
        }

        beginTest ("il warp e' monotono in brillantezza e la nota resta la stessa");
        {
            auto o0 = make (0.0f); auto o1 = make (0.5f); auto o2 = make (1.0f);
            const auto m0 = magnitudes (render (o0, numSamples), fftOrder);
            const auto m1 = magnitudes (render (o1, numSamples), fftOrder);
            const auto m2 = magnitudes (render (o2, numSamples), fftOrder);
            expect (centroidBin (m0) < centroidBin (m1) && centroidBin (m1) < centroidBin (m2), "centroide non crescente con il warp");

            // A warp intermedio il periodo e' ancora quello della nota: l'energia sta sui
            // multipli della fondamentale (il wrap del sync e' periodico con la nota).
            double harmonic = 0.0, total = 0.0;
            for (size_t i = 1; i < m1.size(); ++i)
            {
                const auto e = (double) m1[i] * (double) m1[i];
                total += e;
                if ((int) i % fundamentalBin == 0) harmonic += e;
            }
            expect (harmonic > 0.9 * total, "energia fuori dalle armoniche della nota: " + juce::String (1.0 - harmonic / total));
        }

        beginTest ("warp fuori range viene limitato a 0..1");
        {
            auto a = make (1.0f); auto b = make (7.0f);
            const auto x = render (a, 2048), y = render (b, 2048);
            int mismatches = 0;
            for (size_t i = 0; i < x.size(); ++i) mismatches += (x[i] != y[i]) ? 1 : 0;
            expectEquals (mismatches, 0);
        }
    }
};

static WarpTests warpTests;
