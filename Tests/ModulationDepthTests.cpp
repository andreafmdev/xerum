#include "EngineTestHelpers.h"

#include "dsp/MipTable.h"
#include "dsp/Saturation.h"
#include "dsp/PlateReverb.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"
#include "engine/ParamCollect.h"
#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"
#include "parameters/PresetValue.h"
#include "state/StateToEngine.h"
#include "state/StateTree.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace
{
using harness::defaultParams;
using harness::measureFundamentalHz;
using harness::prepareEngine;
using harness::renderPeak;
using harness::renderRms;
using harness::denormaliseLinear;
using harness::ClipperProbe;
using harness::measureAtClipper;
using harness::rawFromNormalised;
using harness::PresetPatch;
using harness::patchFromPreset;
} // namespace

/**
 * La profondita' di modulazione che arriva davvero all'uscita, in funzione della frequenza
 * dell'LFO.
 *
 * Perche' esiste. Gli SmoothedValue della voce hanno una rampa di 20 ms, cioe' — con il
 * bersaglio riposato una volta per sotto-fetta e la rampa che ne recupera 32/960 per volta —
 * un passa-basso a un polo a circa 8 Hz. Finche' dentro quegli smoother finiva il valore *gia'
 * modulato*, quel filtro si applicava anche alla modulazione: una route lfo -> cutoff a 8 Hz
 * usciva attenuata di 3 dB, a 20 Hz di 8. Lo smoothing esiste per togliere lo zipper quando
 * l'utente o l'host muovono un parametro a scatti, non per filtrare un segnale che e' gia'
 * continuo per costruzione.
 *
 * Come si misura. Non si guarda il valore di cutoff dentro la voce — sarebbe una finestra
 * aperta nel percorso audio solo per il test — ma l'unica cosa che l'utente sente: l'uscita.
 * Si tiene una nota ferma, si calcola l'RMS su finestre consecutive di 64 campioni (un
 * inviluppo a 750 Hz a 48 kHz, quaranta punti per ciclo anche a 20 Hz), si scarta il mezzo
 * secondo iniziale — attacco e assestamento delle rampe — e si estrae dall'inviluppo la sola
 * componente alla frequenza dell'LFO, con una DFT a una riga e finestra di Hann. Il rapporto
 * fra quell'ampiezza e il valor medio dell'inviluppo e' la profondita' vista all'uscita.
 *
 * Il numero assoluto non conta e non e' confrontabile fra un bersaglio e l'altro (su `level` e'
 * l'ampiezza stessa, su `cutoff` e' il modo in cui il filtro traduce una spazzata in energia):
 * quello che conta e' che **non cambi con la frequenza dell'LFO**. Si confronta quindi ogni
 * frequenza con la piu' bassa della serie.
 */
struct ModulationDepthTests final : juce::UnitTest
{
    ModulationDepthTests() : juce::UnitTest ("profondita' di modulazione", "engine") {}

    static constexpr double kSampleRate = 48000.0;

    /**
     * Larghezza della finestra d'RMS, in campioni. E' il compromesso su cui sta in piedi la
     * misura, e va stretto fra due limiti opposti: la finestra deve contenere **piu' cicli
     * della nota** — altrimenti l'RMS insegue la forma d'onda invece dell'inviluppo, ed e'
     * rumore che si somma alla riga che si vuole leggere — e deve restare **molto piu' corta
     * del ciclo dell'LFO**, o attenua proprio cio' che misura.
     *
     * 192 campioni a 48 kHz sono 4 ms: quattro cicli della nota (MIDI 84, 1046 Hz) e un
     * inviluppo a 250 Hz, cioe' dodici punti per ciclo anche a 20 Hz. Il lobo della media
     * mobile a 20 Hz vale 0.989: un per cento di pendenza, dieci volte sotto la tolleranza.
     */
    static constexpr int kRmsWindow = 192;

    /**
     * La nota della misura. Acuta apposta: fra il ciclo della nota e quello dell'LFO deve
     * entrarci una finestra d'RMS, e a 130 Hz (MIDI 48) i due sono separati da un fattore sei,
     * troppo poco perche' una finestra esista. A 1046 Hz il fattore e' cinquanta.
     */
    static constexpr int kMidiNote = 84;

    /** Il grezzo di `lrate` che da' quella frequenza: la mappa e' Log 0.05..20 Hz, quindi il
        grezzo non e' la frequenza divisa per venti. */
    static float lfoRateRawFor (float hz)
    {
        constexpr auto* spec = params::find ("lrate");
        static_assert (spec != nullptr, "lrate non e' in ParameterTable.h");
        return params::normalise (*spec, hz);
    }

    /** La frequenza vera che ne esce: e' quella su cui si sintonizza la DFT, non quella chiesta. */
    static float lfoRateHzFor (float raw)
    {
        constexpr auto* spec = params::find ("lrate");
        static_assert (spec != nullptr, "lrate non e' in ParameterTable.h");
        return params::denormalise (*spec, raw);
    }

    /** Una nota tenuta con una sola route `lfo -> targetSlot`, tutto il resto fermo. */
    static engine::EngineParams heldNoteParams (params::ParamSlot targetSlot, float base, float rateHz)
    {
        auto p = defaultParams();
        p.attackSeconds = 0.001f;
        p.decaySeconds = 0.001f;
        p.sustain = 1.0f;        // l'inviluppo d'ampiezza e' piatto: ogni oscillazione e' dell'LFO
        p.releaseSeconds = 0.05f;
        p.modBase[(size_t) engine::modTargetIndexFor (targetSlot)] = base;

        p.lfoShapeIndex = 0;     // sine
        p.lfoRateRaw = lfoRateRawFor (rateHz);
        p.lfoSync = false;
        p.lfoRetrig = true;
        p.lfoFadeSeconds = 0.0f;
        p.lfoPhaseOffset01 = 0.0f;
        return p;
    }

    /** Inviluppo d'ampiezza dell'uscita: RMS su finestre consecutive di kRmsWindow campioni. */
    static std::vector<float> renderRmsEnvelope (dsp::WavetableStore& store,
                                                 const engine::EngineParams& params,
                                                 const engine::ModSnapshot& mods,
                                                 int totalSamples)
    {
        engine::SynthEngine synth;
        engine::EngineSpec spec;
        spec.sampleRate = kSampleRate;
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());

        auto p = params;
        p.mods = &mods;
        synth.setParams (p);
        synth.setMasterGainLinear (1.0f);

        std::vector<float> envelope;
        envelope.reserve ((size_t) (totalSamples / kRmsWindow + 1));

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, kMidiNote, 1.0f), 0);

        juce::AudioBuffer<float> buffer (2, 128);
        double sumSquares = 0.0;
        int count = 0;

        for (int start = 0; start < totalSamples; start += 128)
        {
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            const auto* d = buffer.getReadPointer (0);

            for (int i = 0; i < 128; ++i)
            {
                sumSquares += (double) d[i] * (double) d[i];

                if (++count == kRmsWindow)
                {
                    envelope.push_back ((float) std::sqrt (sumSquares / (double) kRmsWindow));
                    sumSquares = 0.0;
                    count = 0;
                }
            }
        }

        return envelope;
    }

    /** Ampiezza della componente a `rateHz` dell'inviluppo, in frazione del suo valor medio. */
    static float depthOf (const std::vector<float>& envelope, float rateHz, float discardSeconds)
    {
        const auto envelopeRate = kSampleRate / (double) kRmsWindow;
        const auto first = (size_t) (discardSeconds * (float) envelopeRate);

        if (first + 16 >= envelope.size())
            return 0.0f;

        const auto n = envelope.size() - first;

        double mean = 0.0;
        for (size_t k = 0; k < n; ++k)
            mean += (double) envelope[first + k];
        mean /= (double) n;

        const auto twoPi = 2.0 * juce::MathConstants<double>::pi;
        const auto omega = twoPi * (double) rateHz / envelopeRate;

        double re = 0.0;
        double im = 0.0;
        double windowSum = 0.0;

        for (size_t k = 0; k < n; ++k)
        {
            // Hann: la finestra d'analisi non contiene un numero intero di cicli, e senza
            // finestratura la dispersione spettrale del residuo continuo falserebbe la riga.
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) k / (double) (n - 1));
            const auto v = (double) envelope[first + k] - mean;

            re += w * v * std::cos (omega * (double) k);
            im -= w * v * std::sin (omega * (double) k);
            windowSum += w;
        }

        // Fattore 2 e divisione per la somma della finestra: e' la normalizzazione che rende
        // l'ampiezza letta uguale a quella della sinusoide, guadagno coerente della Hann incluso.
        const auto amplitude = 2.0 * std::sqrt (re * re + im * im) / windowSum;

        return mean > 0.0 ? (float) (amplitude / mean) : 0.0f;
    }

    /** La profondita' vista all'uscita per una route su `targetSlot` a quella frequenza. */
    static float measureDepth (dsp::WavetableStore& store, params::ParamSlot targetSlot,
                               float base, float depth, float rateHz,
                               const engine::EngineParams& extra)
    {
        auto p = heldNoteParams (targetSlot, base, rateHz);
        p.cutoffHz = extra.cutoffHz;
        p.resonanceQ = extra.resonanceQ;

        engine::ModSnapshot mods;
        mods.count = 1;
        mods.routes[0] = { engine::ModSource::lfo, engine::modTargetIndexFor (targetSlot), depth };

        constexpr float settleSeconds = 0.5f;
        constexpr float periods = 8.0f;

        const auto trueHz = lfoRateHzFor (p.lfoRateRaw);
        const auto seconds = settleSeconds + periods / trueHz;
        const auto totalSamples = ((int) (seconds * (float) kSampleRate) / 128 + 1) * 128;

        const auto envelope = renderRmsEnvelope (store, p, mods, totalSamples);
        return depthOf (envelope, trueHz, settleSeconds);
    }

    void runTest() override
    {
        // Da 2 a 20 Hz: 2 Hz sta comodamente dentro la banda passante dello smoother, 20 Hz e'
        // il massimo che `lrate` sa produrre. Se lo smoothing tocca la modulazione, la serie
        // scende; se non la tocca, resta piatta.
        const float rates[] = { 2.0f, 4.0f, 8.0f, 16.0f, 20.0f };

        // Piatta vuol dire piatta: il solo scostamento ammesso e' quello della misura, non una
        // pendenza. La finestra d'RMS a 20 Hz attenua dello 0.1 %, il mantenimento a tasso di
        // controllo di un altro 0.01 %.
        constexpr float kFlat = 0.90f;

        beginTest ("lfo -> level: la profondita' non dipende dalla frequenza dell'LFO");
        {
            dsp::WavetableStore store; store.setActive (1);

            auto extra = defaultParams();

            std::vector<float> depths;
            for (const auto rate : rates)
                depths.push_back (measureDepth (store, params::ParamSlot::level, 0.5f, 0.4f, rate, extra));

            for (size_t i = 0; i < depths.size(); ++i)
                logMessage ("level: LFO " + juce::String (rates[i], 1) + " Hz -> profondita' "
                            + juce::String (depths[i], 4) + " ("
                            + juce::String (depths[0] > 0.0f ? depths[i] / depths[0] : 0.0f, 3) + " della prima)");

            expect (depths[0] > 0.05f, "a 2 Hz non si misura modulazione: il test non proverebbe niente");

            for (size_t i = 1; i < depths.size(); ++i)
                expect (depths[i] > kFlat * depths[0],
                        "a " + juce::String (rates[i], 1) + " Hz la profondita' e' scesa a "
                            + juce::String (depths[i] / depths[0], 3) + " di quella a 2 Hz");
        }

        beginTest ("lfo -> cutoff: la profondita' non dipende dalla frequenza dell'LFO");
        {
            dsp::WavetableStore store; store.setActive (1);

            // Il passa-basso a 24 dB/ottava fa da rivelatore: la spazzata porta il cutoff da
            // 390 a 2700 Hz attorno alla fondamentale (1046 Hz), quindi l'energia in uscita la
            // segue con un'escursione larga. Q resta a Butterworth apposta: con la risonanza il
            // picco passerebbe *sopra* la fondamentale due volte per ciclo, e l'inviluppo
            // guadagnerebbe una componente al doppio della frequenza dell'LFO invece che alla
            // sua — la riga che si legge non sarebbe piu' quella della modulazione.
            auto extra = defaultParams();

            std::vector<float> depths;
            for (const auto rate : rates)
                depths.push_back (measureDepth (store, params::ParamSlot::cutoff, 0.57f, 0.14f, rate, extra));

            for (size_t i = 0; i < depths.size(); ++i)
                logMessage ("cutoff: LFO " + juce::String (rates[i], 1) + " Hz -> profondita' "
                            + juce::String (depths[i], 4) + " ("
                            + juce::String (depths[0] > 0.0f ? depths[i] / depths[0] : 0.0f, 3) + " della prima)");

            expect (depths[0] > 0.05f, "a 2 Hz non si misura modulazione: il test non proverebbe niente");

            for (size_t i = 1; i < depths.size(); ++i)
                expect (depths[i] > kFlat * depths[0],
                        "a " + juce::String (rates[i], 1) + " Hz la profondita' e' scesa a "
                            + juce::String (depths[i] / depths[0], 3) + " di quella a 2 Hz");
        }
    }
};

static ModulationDepthTests modulationDepthTests;
