#include "dsp/Chorus.h"
#include "dsp/PlateReverb.h"
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

// -------------------------------------------------------------------------------------------
// Contatore di allocazioni
//
// "Nessuna allocazione sul thread audio" e' un criterio di accettazione, e un criterio che non
// si misura e' un'opinione. Sostituire `operator new` globale e' l'unico modo di misurarlo senza
// strumenti esterni: il contatore sale **solo** mentre la bandiera e' alzata, cioe' intorno alle
// chiamate a process(), quindi tutto il resto della suite gira come prima.
//
// La sostituzione vale per l'intero binario di test, ma si limita a contare e a delegare a
// malloc/free: non cambia il comportamento di nessun altro test. Le varianti allineate
// (`operator new (size_t, align_val_t)`) non sono sostituite di proposito — non le usiamo, e
// lasciarle al runtime evita di doverne accoppiare anche le delete.
// -------------------------------------------------------------------------------------------

namespace
{
std::atomic<int> gAllocations { 0 };
std::atomic<bool> gWatchingAllocations { false };
} // namespace

void* operator new (std::size_t size)
{
    if (gWatchingAllocations.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);

    auto* memory = std::malloc (size != 0 ? size : 1);

    if (memory == nullptr)
        throw std::bad_alloc();

    return memory;
}

void* operator new[] (std::size_t size) { return ::operator new (size); }
void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 128;

/** Gli stessi parametri di partenza di Tests/ChorusTests.cpp e Tests/EngineTests.cpp. */
engine::EngineParams baseParams()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.level = 1.0f;
    p.filterOn = false;
    p.cutoffHz = 8000.0f;
    p.attackSeconds = 0.001f;
    p.decaySeconds = 0.01f;
    p.sustain = 1.0f;
    p.releaseSeconds = 0.05f;
    p.pan = 0.0f;
    return p;
}

/** I default di parameters.json per il riverbero, in unita' di dsp::PlateReverb. */
engine::EngineParams reverbDefaults()
{
    auto p = baseParams();
    p.reverbOn = true;
    p.reverbSize01 = 0.6f;
    p.reverbDecay01 = 0.5f;
    p.reverbDamp01 = 0.4f;
    p.reverbPredelaySeconds = 0.02f;
    p.reverbMix01 = 0.25f;
    return p;
}

void prepareEngine (engine::SynthEngine& synth, dsp::WavetableStore& store, int numChannels = 2)
{
    engine::EngineSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = kBlock;
    spec.numChannels = numChannels;
    synth.prepare (spec);
    synth.setWavetable (store.active());
}

struct Rendered
{
    std::vector<float> left, right;
};

Rendered renderHeldNote (const engine::EngineParams& params, dsp::WavetableStore& store, int numBlocks,
                         float masterGain = 0.8f, int note = 60)
{
    engine::SynthEngine synth;
    prepareEngine (synth, store);
    synth.setParams (params);
    synth.setMasterGainLinear (masterGain);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);

    Rendered out;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        buffer.clear();
        synth.process (buffer, midi);
        midi.clear();

        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        out.left.insert (out.left.end(), l, l + kBlock);
        out.right.insert (out.right.end(), r, r + kBlock);
    }

    return out;
}

double rms (const std::vector<float>& x, size_t from = 0)
{
    double sum = 0.0;

    for (size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * (double) x[i];

    return x.size() > from ? std::sqrt (sum / (double) (x.size() - from)) : 0.0;
}

/** La risposta all'impulso del canale sinistro, `numSamples` campioni, riverbero da solo. */
std::vector<float> impulseResponse (float size01, float decay01, float damp01, float predelaySeconds,
                                    int numSamples)
{
    dsp::PlateReverb reverb;
    reverb.prepare (kSampleRate);
    reverb.setParameters (size01, decay01, damp01, predelaySeconds);

    std::vector<float> left ((size_t) numSamples, 0.0f), right ((size_t) numSamples, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    reverb.process (left.data(), right.data(), numSamples);
    return left;
}

/**
 * Il tempo di riverbero misurato su una risposta all'impulso, con il metodo T20 di Schroeder.
 *
 * Si integra all'indietro l'energia — `E(t) = somma da t in poi di h^2` — che e' la curva di
 * decadimento che si otterrebbe mediando infinite eccitazioni a rumore, poi si prendono i due
 * istanti in cui scende di 5 e di 25 dB e si estrapola la retta fra loro fino a -60. E' lo
 * standard (ISO 3382) e non "il primo campione sotto una soglia", che su una coda rumorosa
 * misurerebbe il rumore invece della pendenza.
 *
 * Ritorna 0 se la coda non e' scesa abbastanza dentro la finestra data.
 */
double reverbTimeT20 (const std::vector<float>& h)
{
    std::vector<double> decay (h.size(), 0.0);
    double running = 0.0;

    for (size_t i = h.size(); i-- > 0;)
    {
        running += (double) h[i] * (double) h[i];
        decay[i] = running;
    }

    if (running <= 0.0)
        return 0.0;

    const auto crossing = [&decay, &running] (double db) -> double
    {
        const auto level = running * std::pow (10.0, db / 10.0);

        for (size_t i = 0; i < decay.size(); ++i)
            if (decay[i] <= level)
                return (double) i;

        return -1.0;
    };

    const auto at5 = crossing (-5.0);
    const auto at25 = crossing (-25.0);

    if (at5 < 0.0 || at25 < 0.0 || at25 <= at5)
        return 0.0;

    return 3.0 * (at25 - at5) / kSampleRate;
}

/**
 * L'istante, in secondi, dopo il quale nella risposta all'impulso non c'e' piu' un campione
 * sopra -60 dB rispetto al suo picco.
 *
 * E' letteralmente cio' che `getTailLengthSeconds()` promette all'host — "dopo questo tempo non
 * esce piu' niente di udibile" — e per questo si misura cosi' e non con il T20, che e'
 * un'estrapolazione di pendenza e su una coda che non decade esattamente in modo esponenziale
 * puo' cadere dalla parte sbagliata.
 */
double timeToMinus60dB (const std::vector<float>& h)
{
    auto peak = 0.0f;

    for (const auto v : h)
        peak = juce::jmax (peak, std::abs (v));

    const auto floorLevel = peak * 1.0e-3f;

    for (size_t i = h.size(); i-- > 0;)
        if (std::abs (h[i]) > floorLevel)
            return (double) (i + 1) / kSampleRate;

    return 0.0;
}

/** Il primo campione il cui modulo supera la soglia: l'inizio della prima riflessione. */
int onsetIndex (const std::vector<float>& h, float threshold)
{
    for (size_t i = 0; i < h.size(); ++i)
        if (std::abs (h[i]) > threshold)
            return (int) i;

    return -1;
}

/** Il salto peggiore fra campioni adiacenti in [from, to). */
float worstStep (const std::vector<float>& x, size_t from, size_t to)
{
    auto worst = 0.0f;

    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        worst = juce::jmax (worst, std::abs (x[i] - x[i - 1]));

    return worst;
}
} // namespace

/**
 * Il riverbero da solo: topologia, tempi, livelli.
 */
struct PlateReverbTests final : juce::UnitTest
{
    PlateReverbTests() : juce::UnitTest ("riverbero a piastra", "dsp") {}

    void runTest() override
    {
        juce::ScopedNoDenormals noDenormals;

        beginTest ("il predelay ritarda davvero la prima riflessione");
        {
            // La prima cosa che esce non e' il diffusore: e' il primo tap che pesca da del1 del
            // tank, quindi l'inizio assoluto dipende anche da `size`. Ma la **differenza** fra
            // due predelay diversi, a size uguale, e' il predelay e nient'altro — ed e' la
            // misura che dimostra che il parametro e' cablato invece di essere un altro knob
            // attaccato a niente, che e' il difetto da cui parte tutto questo lavoro.
            const auto none = impulseResponse (0.6f, 0.5f, 0.4f, 0.0f, 48000);
            const auto fifty = impulseResponse (0.6f, 0.5f, 0.4f, 0.05f, 48000);
            const auto hundred = impulseResponse (0.6f, 0.5f, 0.4f, 0.1f, 48000);

            const auto onsetNone = onsetIndex (none, 1.0e-4f);
            const auto onsetFifty = onsetIndex (fifty, 1.0e-4f);
            const auto onsetHundred = onsetIndex (hundred, 1.0e-4f);

            logMessage ("prima riflessione: " + juce::String (onsetNone) + " campioni senza predelay, "
                        + juce::String (onsetFifty) + " a 50 ms, " + juce::String (onsetHundred) + " a 100 ms");

            expect (onsetNone > 0, "senza predelay non esce niente");

            const auto expectedFifty = (int) std::lround (0.05 * kSampleRate);
            const auto expectedHundred = (int) std::lround (0.1 * kSampleRate);

            expect (std::abs ((onsetFifty - onsetNone) - expectedFifty) <= 4,
                    "50 ms di predelay spostano l'inizio di " + juce::String (onsetFifty - onsetNone)
                        + " campioni invece di " + juce::String (expectedFifty));
            expect (std::abs ((onsetHundred - onsetNone) - expectedHundred) <= 4,
                    "100 ms di predelay spostano l'inizio di " + juce::String (onsetHundred - onsetNone)
                        + " campioni invece di " + juce::String (expectedHundred));
        }

        beginTest ("l'energia dopo un impulso dura quanto rvDecay dice");
        {
            // Tre punti della corsa di `decay`, a size fissa, misurati con il T20 di Schroeder.
            // Il contratto e' che il tempo **cresce** con il parametro: se non lo facesse, il
            // knob non sarebbe cablato, che e' la condizione da cui questo lavoro parte.
            const float decays[] = { 0.0f, 0.5f, 1.0f };
            double previous = 0.0;

            for (const auto decay01 : decays)
            {
                dsp::PlateReverb reverb;
                reverb.prepare (kSampleRate);
                reverb.setParameters (0.6f, decay01, 0.4f, 0.0f);

                const auto predicted = (double) reverb.tailSeconds();
                const auto h = impulseResponse (0.6f, decay01, 0.4f, 0.0f,
                                                (int) (predicted * 1.5 * kSampleRate));
                const auto measured = reverbTimeT20 (h);

                logMessage ("rvDecay " + juce::String (juce::roundToInt (decay01 * 100.0f))
                            + " %: T20 " + juce::String (measured, 3) + " s, coda sopra -60 dB "
                            + juce::String (timeToMinus60dB (h), 3) + " s, formula "
                            + juce::String (predicted, 3) + " s");

                expect (measured > previous * 1.2,
                        "il tempo di riverbero non cresce con rvDecay: " + juce::String (measured, 3)
                            + " s contro " + juce::String (previous, 3));

                previous = measured;
            }
        }

        beginTest ("la formula della coda copre quella misurata");
        {
            // tailSeconds() e' cio' che il plugin dichiara all'host e cio' su cui lo stadio FX
            // dimensiona il ringout: se la coda vera fosse piu' lunga, il render offline la
            // taglierebbe e il ringout la spegnerebbe. Il confronto e' sull'ultimo campione sopra
            // -60 dB, che e' la promessa alla lettera, su tutti e nove gli angoli della corsa di
            // size per decay. kTailMargin esiste per il rapporto peggiore di questa tabella.
            auto worstRatio = 0.0;

            for (const auto size01 : { 0.0f, 0.5f, 1.0f })
            {
                for (const auto decay01 : { 0.0f, 0.5f, 1.0f })
                {
                    dsp::PlateReverb reverb;
                    reverb.prepare (kSampleRate);
                    reverb.setParameters (size01, decay01, 0.4f, 0.0f);

                    const auto predicted = (double) reverb.tailSeconds();
                    const auto h = impulseResponse (size01, decay01, 0.4f, 0.0f,
                                                    (int) (predicted * 1.5 * kSampleRate));
                    const auto measured = timeToMinus60dB (h);
                    const auto ratio = measured / predicted;

                    worstRatio = juce::jmax (worstRatio, ratio);

                    logMessage ("size " + juce::String (size01, 1) + ", decay "
                                + juce::String (decay01, 1) + ": misurata " + juce::String (measured, 3)
                                + " s, formula " + juce::String (predicted, 3) + " s, rapporto "
                                + juce::String (ratio, 3));

                    expect (measured < predicted,
                            "size " + juce::String (size01, 1) + ", decay " + juce::String (decay01, 1)
                                + ": coda misurata " + juce::String (measured, 3)
                                + " s contro formula " + juce::String (predicted, 3) + " s");
                }
            }

            logMessage ("rapporto peggiore fra coda misurata e formula: " + juce::String (worstRatio, 3)
                        + " (kTailMargin vale " + juce::String (dsp::PlateReverb::kTailMargin, 2) + ")");
        }

        beginTest ("l'energia dopo un impulso dura quanto rvSize dice");
        {
            // Stessa misura sull'altro parametro che entra nella formula della coda: il giro
            // della figura a otto e' proporzionale a sizeRatio, quindi lo e' anche il T20.
            const auto shortTail = reverbTimeT20 (impulseResponse (0.0f, 0.5f, 0.4f, 0.0f, 480000));
            const auto longTail = reverbTimeT20 (impulseResponse (1.0f, 0.5f, 0.4f, 0.0f, 480000));

            logMessage ("T20 a size 0 %: " + juce::String (shortTail, 3) + " s; a 100 %: "
                        + juce::String (longTail, 3) + " s");

            // Le due corse estreme di sizeRatio stanno in rapporto 3 (0.25 contro 0.75): il
            // rapporto dei tempi deve seguirle da vicino, perche' nella formula size e' un
            // fattore moltiplicativo puro.
            const auto ratio = longTail / juce::jmax (1.0e-6, shortTail);
            expect (ratio > 2.0 && ratio < 4.0,
                    "il rapporto fra i due tempi e' " + juce::String (ratio, 2) + ", non circa tre");
        }

        beginTest ("il bagnato del riverbero sta al livello del secco");
        {
            // kWetGain esiste per questo numero, ed e' questo numero a fissarlo: i sette tap si
            // sommano senza normalizzazione, quindi senza compensazione il mix equal-power dello
            // stadio FX diventerebbe un controllo di volume travestito — e il picco presentato
            // al soft clipper dipenderebbe da `rvMix` invece che dalle note.
            //
            // Rumore passa-bassato a 2 kHz e non bianco, per la stessa ragione del test gemello
            // del chorus: l'interpolazione lineare delle linee non e' piatta fino a Nyquist, e
            // con rumore a piena banda si misurerebbe l'interpolatore invece del livello.
            juce::Random rng (20260919);
            std::vector<float> dry, left, right;
            const auto coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 2000.0f
                                                / (float) kSampleRate);
            float state = 0.0f;

            for (int i = 0; i < (int) (4.0 * kSampleRate); ++i)
            {
                state += (rng.nextFloat() * 2.0f - 1.0f - state) * coeff;
                dry.push_back (state);
                left.push_back (state);
                right.push_back (state);
            }

            dsp::PlateReverb reverb;
            reverb.prepare (kSampleRate);
            reverb.setParameters (0.6f, 0.5f, 0.4f, 0.02f); // i default di parameters.json
            reverb.process (left.data(), right.data(), (int) left.size());

            // Si scarta il primo secondo: la coda non e' ancora a regime.
            const auto skip = (size_t) kSampleRate;
            const auto ratioDb = juce::Decibels::gainToDecibels ((float) (rms (left, skip) / rms (dry, skip)));

            logMessage ("bagnato rispetto al secco, ai default: " + juce::String (ratioDb, 2) + " dB");
            expect (std::abs (ratioDb) < 1.0f,
                    "il bagnato sta " + juce::String (ratioDb, 2)
                        + " dB rispetto al secco: kWetGain non e' piu' tarato");
        }

        beginTest ("automare rvSize non produce zipper");
        {
            // Il primo dei tre caveat sul codice adottato: `setSize()` di Gin sposta i quattordici
            // tap **istantaneamente**, e il tap piu' lungo sta a oltre 4000 campioni. Un salto di
            // corsa piena manderebbe il puntatore di lettura in un punto scorrelato della linea
            // fra due campioni adiacenti, cioe' un clic grande quanto la coda stessa.
            //
            // Qui `sizeRatio` passa da un polo singolo per campione, quindi il salto diventa una
            // scivolata Doppler. La misura e' quella del bypass del chorus: il salto peggiore fra
            // campioni adiacenti nella finestra della transizione, contro la pendenza naturale
            // della stessa coda quando non si muove niente.
            const auto sweep = [] (bool move)
            {
                dsp::PlateReverb reverb;
                reverb.prepare (kSampleRate);

                // Damping a fondo corsa, cioe' 500 Hz: e' la condizione che rende la misura
                // capace di vedere qualcosa. La coda di una piastra e' rumore, e la differenza
                // fra due campioni adiacenti di rumore e' gia' grande quanto il rumore stesso —
                // con la coda brillante un tap che salta in un punto scorrelato non si distingue
                // dal fruscio. Filtrata a 500 Hz la coda e' liscia, e il salto sporge.
                reverb.setParameters (0.0f, 0.7f, 1.0f, 0.0f);

                std::vector<float> left, right;

                // Mezzo secondo di seno a 220 Hz per riempire la coda, poi il salto, poi 200 ms.
                const int settle = (int) (0.5 * kSampleRate);
                const int after = (int) (0.2 * kSampleRate);

                for (int i = 0; i < settle + after; ++i)
                {
                    const auto v = 0.25f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                                     * (float) i / (float) kSampleRate);
                    left.push_back (v);
                    right.push_back (v);
                }

                reverb.process (left.data(), right.data(), settle);

                if (move)
                    reverb.setParameters (1.0f, 0.7f, 1.0f, 0.0f);

                reverb.process (left.data() + settle, right.data() + settle, after);

                return std::pair<std::vector<float>, int> { left, settle };
            };

            const auto still = sweep (false);
            const auto moved = sweep (true);

            const auto natural = worstStep (still.first, (size_t) still.second,
                                            (size_t) still.second + 4800);
            const auto swept = worstStep (moved.first, (size_t) moved.second,
                                          (size_t) moved.second + 4800);

            logMessage ("salto peggiore dopo il salto di rvSize: " + juce::String (swept)
                        + " contro una pendenza naturale di " + juce::String (natural));

            // Il fattore non e' uno: durante la transizione il puntatore di lettura corre piu'
            // veloce del tempo reale (e' il Doppler), quindi la coda esce legittimamente piu'
            // ripida. Misurato: **0.0101 contro 0.0093**, il sette per cento sopra la pendenza
            // naturale. Con lo smussamento di `sizeRatio` disattivato — cioe' con il `setSize()`
            // dell'originale — la stessa misura da' **0.119**, tredici volte la pendenza
            // naturale: e' quello il clic, ed e' questa riga a tenerlo fuori.
            expect (swept < natural * 1.5f,
                    "salto " + juce::String (swept) + " contro pendenza naturale "
                        + juce::String (natural) + ": rvSize produce zipper");
        }

        beginTest ("agli estremi resta finito e limitato");
        {
            // Decadimento e size a fondo corsa, damping spalancato, fondo scala continuo in
            // ingresso: il caso in cui l'anello accumula piu' energia. Non c'e' nessuna
            // saturazione dentro il riverbero — gli allpass sono passa-tutto e il tank e'
            // limitato dal solo decay^4 — quindi se il guadagno d'anello fosse >= 1 per qualche
            // combinazione, e' qui che si vedrebbe.
            dsp::PlateReverb reverb;
            reverb.prepare (kSampleRate);
            reverb.setParameters (1.0f, 1.0f, 0.0f, 0.1f);

            auto peak = 0.0f;

            // I due canali **in fase**, e non uno l'opposto dell'altro come nel test gemello del
            // chorus: il riverbero e' "synthetic stereo", sente la somma mono, e un ingresso in
            // opposizione di fase gli arriva come silenzio esatto. E' una proprieta' vera
            // dell'algoritmo — vale anche per l'originale di Gin, che somma invece di mediare —
            // e non un caso da correggere: un segnale che non esiste in mono non ha una stanza in
            // cui riverberare. Qui pero' renderebbe il test una misura del nulla.
            for (int b = 0; b < 4000; ++b)
            {
                std::vector<float> left ((size_t) kBlock, 1.0f), right ((size_t) kBlock, 1.0f);
                reverb.process (left.data(), right.data(), kBlock);

                for (int i = 0; i < kBlock; ++i)
                {
                    expect (std::isfinite (left[(size_t) i]) && std::isfinite (right[(size_t) i]),
                            "campione non finito");
                    peak = juce::jmax (peak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]));
                }
            }

            logMessage ("picco del bagnato agli estremi su un fondo scala continuo: " + juce::String (peak));
            expect (peak < 20.0f, "picco " + juce::String (peak) + ": l'anello non e' limitato");
        }

        beginTest ("il valore dichiarato all'host copre la coda peggiore");
        {
            // getTailLengthSeconds() in Source/plugin/PluginProcessor.h dichiara 9.0 s, e il
            // numero viene da qui. PluginProcessor non e' compilato in questa suite (tira dentro
            // juce_audio_processors e mezzo plugin), quindi il legame e' questa riga: cambiare
            // kMaxDecay o kMaxSizeRatio senza tornare a quel file rompe il test invece
            // dell'export dell'utente.
            const auto reverbTail = (double) dsp::PlateReverb::tailSecondsAtExtremes();

            // La coda del chorus, che sta in serie **prima**: a feedback pieno il suo contenuto
            // decade di kMaxFeedback per ogni giro di un ritardo massimo di 16.5 ms, cioe' di
            // 60 dB in dieci giri.
            const auto chorusTail = 10.0 * dsp::Chorus::kBaseDelayMs * 0.001
                                    * (1.0 + dsp::Chorus::kMaxDepthFraction);

            logMessage ("coda peggiore: riverbero " + juce::String (reverbTail, 3) + " s, chorus "
                        + juce::String (chorusTail, 3) + " s, totale "
                        + juce::String (reverbTail + chorusTail, 3) + " s");

            expect (reverbTail + chorusTail <= 11.0,
                    "la coda peggiore e' " + juce::String (reverbTail + chorusTail, 3)
                        + " s: getTailLengthSeconds() dichiara 11.0 e sta mentendo");
        }
    }
};

static PlateReverbTests plateReverbTests;

/**
 * Il riverbero dentro lo stadio FX: bypass, serie con il chorus, ringout, margine.
 */
struct ReverbStageTests final : juce::UnitTest
{
    ReverbStageTests() : juce::UnitTest ("stadio FX: riverbero", "engine") {}

    void runTest() override
    {
        juce::ScopedNoDenormals noDenormals;

        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("con il riverbero spento l'uscita e' bit per bit quella di prima");
        {
            // Il criterio di accettazione che conta piu' di tutti. Due volte, perche' ci sono due
            // catene da preservare: quella senza effetti (l'uscita di prima che lo stadio FX
            // esistesse) e quella con il solo chorus (l'uscita di prima di questo lavoro).
            for (const bool chorusOn : { false, true })
            {
                auto off = baseParams();
                off.filterOn = true;
                off.chorusOn = chorusOn;
                off.chorusRateHz = 1.6f;
                off.chorusDepth01 = 0.4f;
                off.chorusMix01 = 0.3f;
                off.reverbOn = false;

                // Tutti i parametri del riverbero agli estremi: se una sola riga del suo ramo
                // girasse comunque, si vedrebbe.
                off.reverbSize01 = 1.0f;
                off.reverbDecay01 = 1.0f;
                off.reverbDamp01 = 1.0f;
                off.reverbPredelaySeconds = 0.1f;
                off.reverbMix01 = 1.0f;

                auto never = baseParams();
                never.filterOn = true;
                never.chorusOn = chorusOn;
                never.chorusRateHz = 1.6f;
                never.chorusDepth01 = 0.4f;
                never.chorusMix01 = 0.3f;
                // nessun campo del riverbero toccato: i default di EngineParams

                const auto withExtremes = renderHeldNote (off, store, 60);
                const auto withDefaults = renderHeldNote (never, store, 60);

                expectEquals ((int) withExtremes.left.size(), (int) withDefaults.left.size());

                for (size_t i = 0; i < withDefaults.left.size(); ++i)
                {
                    expectWithinAbsoluteError (withExtremes.left[i], withDefaults.left[i], 0.0f);
                    expectWithinAbsoluteError (withExtremes.right[i], withDefaults.right[i], 0.0f);
                }
            }
        }

        beginTest ("il riverbero allunga la coda della nota, e il chorus non basta a farlo");
        {
            // La prova che l'effetto fa qualcosa di misurabile **dentro il motore**: si spegne la
            // nota e si guarda cosa resta mezzo secondo dopo. Con il solo chorus quel mezzo
            // secondo e' silenzio (il suo ritardo massimo e' 16.5 ms); con il riverbero c'e'
            // ancora coda, ed e' anche la dimostrazione che il ringout non la tronca — il vecchio
            // valore di 0.5 s l'avrebbe spenta esattamente li'.
            const auto tailAfterRelease = [&store] (bool reverbOn)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = reverbDefaults();
                p.reverbOn = reverbOn;
                p.reverbMix01 = 1.0f;
                p.chorusOn = true;
                p.chorusMix01 = 0.3f;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

                for (int b = 0; b < 100; ++b)
                {
                    juce::AudioBuffer<float> buffer (2, kBlock);
                    buffer.clear();
                    synth.process (buffer, midi);
                    midi.clear();
                }

                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

                auto late = 0.0f;
                const auto blocks = (int) std::ceil (1.0 * kSampleRate / (double) kBlock);
                const auto lateFrom = (int) std::ceil (0.5 * kSampleRate / (double) kBlock);

                for (int b = 0; b < blocks; ++b)
                {
                    juce::AudioBuffer<float> buffer (2, kBlock);
                    buffer.clear();
                    synth.process (buffer, midi);
                    midi.clear();

                    if (b >= lateFrom)
                        for (int ch = 0; ch < 2; ++ch)
                            for (int i = 0; i < kBlock; ++i)
                                late = juce::jmax (late, std::abs (buffer.getSample (ch, i)));
                }

                return late;
            };

            const auto dry = tailAfterRelease (false);
            const auto wet = tailAfterRelease (true);

            logMessage ("mezzo secondo dopo il rilascio: senza riverbero " + juce::String (dry)
                        + ", con riverbero " + juce::String (wet));

            expect (dry < 1.0e-5f, "senza riverbero resta " + juce::String (dry) + ": non e' silenzio");
            expect (wet > 1.0e-3f,
                    "con il riverbero resta " + juce::String (wet)
                        + ": la coda non c'e', o il ringout l'ha troncata");
        }

        beginTest ("dopo la coda il ringout spegne davvero lo stadio");
        {
            // L'altro verso dello stesso contratto: la soglia del ringout cresce con la coda, ma
            // deve pur scattare. Ai default la coda dichiarata e' circa un secondo e mezzo: dopo
            // cinque secondi di silenzio non puo' restare niente.
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = reverbDefaults();
            p.reverbMix01 = 1.0f;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            for (int b = 0; b < 100; ++b)
            {
                juce::AudioBuffer<float> buffer (2, kBlock);
                buffer.clear();
                synth.process (buffer, midi);
                midi.clear();
            }

            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

            auto tail = 0.0f;
            const auto blocks = (int) std::ceil (8.0 * kSampleRate / (double) kBlock);
            const auto lateFrom = (int) std::ceil (5.0 * kSampleRate / (double) kBlock);

            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buffer (2, kBlock);
                buffer.clear();
                synth.process (buffer, midi);
                midi.clear();

                if (b >= lateFrom)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlock; ++i)
                            tail = juce::jmax (tail, std::abs (buffer.getSample (ch, i)));
            }

            expect (tail < 1.0e-4f, "coda " + juce::String (tail) + ": il riverbero non si spegne mai");
        }

        beginTest ("accendere e spegnere il riverbero durante una nota non produce un gradino");
        {
            // Stessa tecnica del test gemello del chorus: l'interruttore cade in sessanta punti
            // diversi dell'onda e si tiene il peggiore, perche' il gradino d'accensione **dipende
            // dal campione su cui si accende** e una fase sola lo prende o lo manca a caso.
            //
            // Per il riverbero il rimedio non e' il riempimento della linea — la sua coda ci
            // mette secondi a formarsi — ma la rampa sull'ingresso: nelle linee non entra nessun
            // gradino, quindi non ne puo' uscire nessuno. Il gradino che toglie arriva **dopo**
            // l'incrocio d'uscita, non durante: il bagnato resta zero per tutto il predelay piu'
            // il primo tap (1200 campioni ai default, contro i 576 dell'incrocio), poi comincia
            // di colpo a leggere il segnale che era all'ingresso nell'istante dell'accensione. Il
            // salto vale circa 0.065 volte l'ampiezza di quel segnale — il prodotto dei guadagni
            // immediati di passa-basso, quattro diffusori, primo allpass e kWetGain.
            //
            // Per vederlo serve una forma d'onda la cui pendenza naturale sia piu' bassa di quel
            // salto, e per questo qui la tavola e' "Basic Shapes" a un terzo di `wtpos` — il seno
            // della famiglia — e non i denti di sega del resto del file: su un dente di sega la
            // pendenza propria dell'onda e' trenta volte il gradino e lo nasconde, e il test
            // passerebbe anche con la rampa tolta.
            //
            // Misurato: **0.0139** accendendo, contro una pendenza naturale di 0.0218 (il regime
            // acceso) e 0.0126 (quello spento). Con la rampa d'ingresso disattivata, e il solo
            // incrocio d'uscita: **0.0308**, cioe' oltre il limite. Il test guarda quella riga.
            dsp::WavetableStore sine;
            sine.setActive (0); // "Basic Shapes": le quattro onde base, il seno a un terzo di corsa

            const auto worstJump = [&sine] (bool startOn, bool endOn)
            {
                auto worst = 0.0f;

                for (int settle = 200; settle < 260; ++settle)
                {
                    engine::SynthEngine synth;
                    prepareEngine (synth, sine);

                    auto p = reverbDefaults();
                    p.framePosition = 1.0f / 3.0f;
                    p.attackSeconds = 0.005f;
                    p.decaySeconds = 0.005f;
                    p.chorusOn = false;
                    p.reverbOn = startOn;
                    p.reverbMix01 = 1.0f; // il caso peggiore: fra secco e bagnato non resta niente in comune
                    synth.setParams (p);
                    synth.setMasterGainLinear (1.0f);

                    juce::AudioBuffer<float> buffer (2, kBlock);
                    juce::MidiBuffer midi;
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                    buffer.clear();
                    synth.process (buffer, midi);
                    midi.clear();

                    auto previous = 0.0f;

                    for (int b = 0; b < settle; ++b)
                    {
                        buffer.clear();
                        synth.process (buffer, midi);
                        previous = buffer.getSample (0, kBlock - 1);
                    }

                    p.reverbOn = endOn;
                    synth.setParams (p);

                    // Sessanta blocchi coprono la dissolvenza (12 ms), il predelay (20 ms) e il
                    // tempo in cui le prime riflessioni arrivano ai tap: il gradino, se c'e',
                    // sta li' dentro.
                    for (int b = 0; b < 60; ++b)
                    {
                        buffer.clear();
                        synth.process (buffer, midi);

                        worst = juce::jmax (worst, std::abs (buffer.getSample (0, 0) - previous));

                        for (int i = 1; i < kBlock; ++i)
                            worst = juce::jmax (worst,
                                                std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

                        previous = buffer.getSample (0, kBlock - 1);
                    }
                }

                return worst;
            };

            // La pendenza naturale e' il **massimo dei due regimi**, acceso e spento, e non solo
            // di quello acceso: con `rvMix` al 100 % l'uscita passa dall'onda cruda del synth al
            // bagnato del riverbero, che sono due segnali con pendenze diverse, e durante
            // l'incrocio l'uscita e' una combinazione dei due. Confrontarla con il solo regime
            // acceso misurerebbe la differenza fra i due segnali invece del gradino: e' esattamente
            // cio' che faceva fallire questo test prima, con 0.089 contro una pendenza "naturale"
            // di 0.068 che era quella del solo bagnato.
            const auto naturalOn = worstJump (true, true);
            const auto naturalOff = worstJump (false, false);
            const auto natural = juce::jmax (naturalOn, naturalOff);
            const auto onJump = worstJump (false, true);
            const auto offJump = worstJump (true, false);

            logMessage ("salto peggiore su 60 fasi: pendenza naturale " + juce::String (natural)
                        + " (acceso " + juce::String (naturalOn) + ", spento " + juce::String (naturalOff)
                        + "), accendendo " + juce::String (onJump) + ", spegnendo " + juce::String (offJump));

            const auto limit = natural * 1.25f;
            expect (onJump < limit, "accendendo, salto " + juce::String (onJump)
                                        + " contro pendenza naturale " + juce::String (natural));
            expect (offJump < limit, "spegnendo, salto " + juce::String (offJump)
                                         + " contro pendenza naturale " + juce::String (natural));
        }

        beginTest ("in mono lo stadio gira su una linea sola e non esce dal buffer");
        {
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = kSampleRate;
            spec.maximumBlockSize = kBlock;
            spec.numChannels = 1;
            synth.prepare (spec);

            dsp::WavetableStore mono;
            mono.setActive (0);
            synth.setWavetable (mono.active());

            auto p = reverbDefaults();
            p.chorusOn = true;
            p.chorusRateHz = 1.6f;
            p.chorusDepth01 = 0.6f;
            p.chorusMix01 = 1.0f;
            p.reverbMix01 = 1.0f;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            auto peak = 0.0f;

            for (int b = 0; b < 200; ++b)
            {
                juce::AudioBuffer<float> buffer (1, kBlock);
                buffer.clear();
                synth.process (buffer, midi);
                midi.clear();

                for (int i = 0; i < kBlock; ++i)
                {
                    expect (std::isfinite (buffer.getSample (0, i)), "campione non finito in mono");
                    peak = juce::jmax (peak, std::abs (buffer.getSample (0, i)));
                }
            }

            expect (peak > 0.05f, "uscita mono troppo bassa (" + juce::String (peak) + ")");
            expect (peak <= 1.0f, "campione mono fuori scala (" + juce::String (peak) + ")");
        }

        beginTest ("lo stadio FX non alloca sul thread audio");
        {
            // Il contatore sostituisce operator new globale (vedi il commento in testa al file).
            // Si guarda **solo** process(), con i due effetti accesi e i parametri in movimento:
            // e' il percorso su cui un `new` nascosto — dentro juce::dsp, dentro una linea che si
            // ridimensiona, dentro un std::vector che cresce — avrebbe un thread audio da far
            // saltare.
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = reverbDefaults();
            p.chorusOn = true;
            p.chorusMix01 = 0.5f;
            p.chorusFeedback01 = 0.5f;
            p.reverbMix01 = 0.5f;
            synth.setParams (p);
            synth.setMasterGainLinear (0.8f);

            juce::AudioBuffer<float> buffer (2, kBlock);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            // Un giro a vuoto prima di guardare: la prima chiamata puo' toccare cose che si
            // inizializzano pigramente, e non sarebbe quello il difetto da cercare.
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            gAllocations.store (0, std::memory_order_relaxed);
            gWatchingAllocations.store (true, std::memory_order_relaxed);

            for (int b = 0; b < 400; ++b)
            {
                // I parametri si muovono a ogni blocco, compresi size e predelay, che nel codice
                // originale erano proprio quelli che riallocavano.
                const auto t = (float) b / 400.0f;
                p.reverbSize01 = t;
                p.reverbDecay01 = 1.0f - t;
                p.reverbDamp01 = t;
                p.reverbPredelaySeconds = t * dsp::PlateReverb::kMaxPredelaySeconds;
                p.reverbMix01 = t;
                p.chorusOn = (b / 37) % 2 == 0;
                p.reverbOn = (b / 53) % 2 == 0;
                synth.setParams (p);

                buffer.clear();
                synth.process (buffer, midi);
            }

            gWatchingAllocations.store (false, std::memory_order_relaxed);

            const auto allocations = gAllocations.load (std::memory_order_relaxed);
            logMessage ("allocazioni durante 400 blocchi di process(): " + juce::String (allocations));
            expectEquals (allocations, 0);
        }

        beginTest ("il margine al soft clipper: ai default e agli estremi del riverbero");
        {
            // **Misura e riporta**: kVoiceHeadroomGain e kSoftClipThreshold restano dove sono, e
            // la ritaratura congiunta del gain staging e' il lavoro che viene dopo.
            //
            // Il metodo e' quello che docs/architecture.md indica sotto la tabella del gain
            // staging, non l'inversione analitica di softClip: quella e' malcondizionata vicino
            // alla saturazione (0.9983 risale a 2.36, 0.9990 a 3.40, cioe' sette decimillesimi di
            // uscita diventano 3.4 dB di ingresso stimato). Qui si abbassa il gain master finche'
            // il clipper resta spento — e si **verifica** che sia spento — e si riscala. La
            // catena prima del clipper e' esattamente lineare nel gain master, quindi il
            // riscalamento e' esatto e non una stima.
            constexpr float kDefaultVolume = 0.8f;
            constexpr float kProbeVolume = 0.05f;

            // Due numeri per configurazione, e servono tutti e due. Il **picco assoluto** lo fa
            // l'attacco: sei note che partono sullo stesso campione partono anche sulla stessa
            // fase della tavola, quindi il transitorio iniziale e' identico in tutte le
            // configurazioni — gli effetti non hanno ancora avuto tempo di entrare (12 ms di
            // dissolvenza, 16.5 di riempimento del chorus, 20 di predelay) e quel numero non
            // distingue nulla. Il **picco a regime**, misurato dopo mezzo secondo, e' quello su
            // cui si confrontano gli effetti fra loro.
            // L'RMS a regime viaggia insieme ai due picchi perche' e' l'unica grandezza su cui
            // "equal-power" dice davvero qualcosa: il mix sin3dB conserva la **potenza**, non il
            // picco, e secco e bagnato hanno fattori di cresta diversi.
            struct Peaks { double overall, steady, rms; };

            const auto peakAtClipper = [&store, this] (const engine::EngineParams& params)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = params;
                p.filterOn = true;
                p.cutoffHz = 4000.0f;
                p.resonanceQ = 2.0f;
                synth.setParams (p);
                synth.setMasterGainLinear (kProbeVolume);

                juce::MidiBuffer midi;

                for (const int note : { 48, 55, 60, 64, 67, 72 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);

                auto peak = 0.0;
                auto steady = 0.0;
                auto energy = 0.0;
                auto counted = 0;
                const auto steadyFrom = (int) std::ceil (0.5 * kSampleRate / (double) kBlock);

                for (int b = 0; b < 600; ++b)
                {
                    juce::AudioBuffer<float> buffer (2, kBlock);
                    buffer.clear();
                    synth.process (buffer, midi);
                    midi.clear();

                    for (int ch = 0; ch < 2; ++ch)
                    {
                        for (int i = 0; i < kBlock; ++i)
                        {
                            const auto sample = (double) std::abs (buffer.getSample (ch, i));
                            peak = juce::jmax (peak, sample);

                            if (b >= steadyFrom)
                            {
                                steady = juce::jmax (steady, sample);
                                energy += sample * sample;
                                ++counted;
                            }
                        }
                    }
                }

                // Se il clipper si fosse acceso anche solo per un campione, il riscalamento non
                // varrebbe piu' niente: meglio saperlo che riportare un numero inventato.
                expect (peak < 0.95, "il gain di prova non basta: il clipper si e' acceso a "
                                         + juce::String (peak, 4));

                const auto scale = (double) (kDefaultVolume / kProbeVolume);
                const auto steadyRms = counted > 0 ? std::sqrt (energy / (double) counted) : 0.0;
                return Peaks { peak * scale, steady * scale, steadyRms * scale };
            };

            auto dry = baseParams();
            dry.chorusOn = false;
            dry.reverbOn = false;

            auto defaults = reverbDefaults();
            defaults.chorusOn = true;
            defaults.chorusRateHz = 1.6f;
            defaults.chorusDepth01 = 0.4f;
            defaults.chorusMix01 = 0.3f;

            auto reverbOnly = reverbDefaults();
            reverbOnly.chorusOn = false;

            auto fullMix = reverbDefaults();
            fullMix.chorusOn = false;
            fullMix.reverbMix01 = 1.0f;

            auto extremes = reverbDefaults();
            extremes.chorusOn = true;
            extremes.chorusRateHz = 1.6f;
            extremes.chorusDepth01 = 1.0f;
            extremes.chorusMix01 = 1.0f;
            extremes.chorusFeedback01 = 1.0f;
            extremes.reverbSize01 = 1.0f;
            extremes.reverbDecay01 = 1.0f;
            extremes.reverbDamp01 = 0.0f;
            extremes.reverbPredelaySeconds = 0.0f;
            extremes.reverbMix01 = 1.0f;

            const auto peakDry = peakAtClipper (dry);
            const auto peakDefaults = peakAtClipper (defaults);
            const auto peakReverbOnly = peakAtClipper (reverbOnly);
            const auto peakFullMix = peakAtClipper (fullMix);
            const auto peakExtremes = peakAtClipper (extremes);

            const auto report = [this, peakDry] (const char* what, Peaks peak)
            {
                logMessage (juce::String (what) + ": a regime " + juce::String (peak.steady, 3) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (peak.steady), 2) + " dBFS), "
                            + juce::String (juce::Decibels::gainToDecibels (peak.steady / peakDry.steady), 2)
                            + " dB rispetto al secco; RMS "
                            + juce::String (juce::Decibels::gainToDecibels (peak.rms / peakDry.rms), 2)
                            + " dB; picco assoluto " + juce::String (peak.overall, 3));
            };

            report ("secco (nessun effetto)", peakDry);
            report ("chorus e riverbero ai default di fabbrica", peakDefaults);
            report ("solo riverbero ai default", peakReverbOnly);
            report ("solo riverbero, mix 100 %", peakFullMix);
            report ("tutto a fondo corsa", peakExtremes);

            // Ai default di fabbrica — quelli che l'utente trova aprendo il plugin — il riverbero
            // deve costare poco o niente, perche' e' acceso di default e nessuno lo ha chiesto.
            // Misurato: **+0.31 dB** da solo, e -0.22 dB insieme al chorus, che ne restituisce piu'
            // di quanti il riverbero ne prenda (il chorus ai suoi default abbassa il picco: vedi
            // Tests/ChorusTests.cpp).
            const auto defaultsDb = juce::Decibels::gainToDecibels (peakReverbOnly.steady / peakDry.steady);
            expect (defaultsDb < 1.0, "il riverbero ai default alza il picco al clipper di "
                                          + juce::String (defaultsDb, 2) + " dB");

            // A `rvMix` 100 % l'uscita **e'** il bagnato. Misurato: il picco sale di 2.36 dB e
            // l'RMS di 2.23, cioe' quasi tutto l'aumento e' potenza vera e non fattore di cresta —
            // ed e' la cosa che vale la pena sapere di questa misura.
            //
            // kWetGain e' tarato perche' il bagnato stia al livello del secco su **rumore a banda
            // larga** (il test omonimo al livello di dsp misura -0.02 dB), ma il guadagno di una
            // piastra non e' piatto in frequenza: il damping toglie l'acuto e lascia stare il
            // grave, quindi un accordo — la cui energia sta fra 130 e 520 Hz — esce dalla piastra
            // piu' forte di quanto ne esca del rumore. I due decibel sono la differenza fra le due
            // sorgenti, non uno sbilanciamento della taratura: nessun valore di kWetGain le porta
            // a zero tutte e due, sposterebbe solo quale delle due ci sta. Il riferimento a banda
            // larga e' quello che non dipende da che patch si sta suonando, e per questo e' quello
            // su cui e' tarata la costante.
            const auto mixRmsDb = juce::Decibels::gainToDecibels (peakFullMix.rms / peakDry.rms);
            const auto mixPeakDb = juce::Decibels::gainToDecibels (peakFullMix.steady / peakDry.steady);

            logMessage ("rvMix al 100 %: picco " + juce::String (mixPeakDb, 2) + " dB, RMS "
                        + juce::String (mixRmsDb, 2) + " dB");

            expect (mixRmsDb < 2.5, "rvMix al 100 % alza l'RMS di " + juce::String (mixRmsDb, 2)
                                        + " dB: piu' dei due decibel che separano le due sorgenti di "
                                          "taratura, quindi kWetGain e' da rivedere");

            // "Tutto a fondo corsa" e' i due effetti insieme con ogni knob al massimo: mix 100 % su
            // entrambi, feedback del chorus al 100 %, size e decay del riverbero a fondo corsa.
            // Misurato **+3.07 dB**, di cui 2.36 sono il riverbero a mix pieno e il resto il
            // feedback del chorus, che da solo ne vale 2.2 (vedi Tests/ChorusTests.cpp) — i due non
            // si sommano perche' il picco lo fissa comunque la coda.
            //
            // La soglia sta appena sopra la misura. Se un giorno mordera', i numeri da rivedere sono
            // kVoiceHeadroomGain e kSoftClipThreshold nella ritaratura congiunta del gain staging —
            // **non questa riga**.
            const auto worstDb = juce::Decibels::gainToDecibels (peakExtremes.steady / peakDry.steady);
            expect (worstDb < 3.5, "con tutto a fondo corsa il picco al clipper sale di "
                                       + juce::String (worstDb, 2) + " dB");
        }
    }
};

static ReverbStageTests reverbStageTests;
