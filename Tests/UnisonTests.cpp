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
 * Task 10: unison e detune.
 *
 * L'unison non ha un "valore giusto" campione per campione da confrontare, quindi i test qui
 * sotto misurano proprieta': quante copie suonano davvero, dove stanno intonate, quanto forte
 * esce la somma e quanto e' larga. Il test piu' importante e' pero' quello negativo, in fondo:
 * con unison 1 lo strumento deve restare esattamente quello di prima.
 */
struct UnisonTests final : juce::UnitTest
{
    UnisonTests() : juce::UnitTest ("unison e detune", "engine") {}

    /** Accende una nota consumando un blocco: le misure successive partono a nota gia' viva. */
    static void noteOn (engine::SynthEngine& synth, int note)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, midi);
    }

    /**
     * RMS del canale sinistro su finestre da `blocksPerWindow` blocchi. La finestra e' lunga
     * apposta: su 128 campioni soli il conteggio dei periodi che ci stanno dentro non e' intero
     * e l'RMS oscilla di qualche punto percentuale anche su un segnale perfettamente periodico,
     * cioe' proprio il rumore di fondo che questo test deve distinguere dai battimenti.
     */
    static std::vector<float> rmsWindows (engine::SynthEngine& synth, int numWindows, int blocksPerWindow)
    {
        std::vector<float> series;
        series.reserve ((size_t) numWindows);
        juce::MidiBuffer none;

        for (int w = 0; w < numWindows; ++w)
        {
            double sumSquares = 0.0;
            int count = 0;

            for (int b = 0; b < blocksPerWindow; ++b)
            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, none);

                const auto* data = buffer.getReadPointer (0);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    sumSquares += (double) data[i] * (double) data[i];
                    ++count;
                }
            }

            series.push_back (count > 0 ? (float) std::sqrt (sumSquares / (double) count) : 0.0f);
        }

        return series;
    }

    /** Scarto quadratico medio diviso la media: quanto "respira" l'ampiezza. */
    static float relativeSpread (const std::vector<float>& series)
    {
        if (series.empty())
            return 0.0f;

        double mean = 0.0;
        for (const auto value : series)
            mean += (double) value;
        mean /= (double) series.size();

        if (mean <= 0.0)
            return 0.0f;

        double variance = 0.0;
        for (const auto value : series)
            variance += ((double) value - mean) * ((double) value - mean);
        variance /= (double) series.size();

        return (float) (std::sqrt (variance) / mean);
    }

    /**
     * Correlazione fra i due canali. Un segnale audio ha media nulla, quindi non si sottrae
     * nessuna media: e' il coseno fra i due vettori. 1 = i due canali sono lo stesso segnale
     * (immagine puntiforme al centro), 0 = del tutto scorrelati (immagine larghissima).
     */
    static float channelCorrelation (engine::SynthEngine& synth, int numBlocks)
    {
        double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
        juce::MidiBuffer none;

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, none);

            const auto* l = buffer.getReadPointer (0);
            const auto* r = buffer.getReadPointer (1);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                sumLL += (double) l[i] * (double) l[i];
                sumRR += (double) r[i] * (double) r[i];
                sumLR += (double) l[i] * (double) r[i];
            }
        }

        const auto denominator = std::sqrt (sumLL * sumRR);
        return denominator > 0.0 ? (float) (sumLR / denominator) : 0.0f;
    }

    /**
     * `numSamples` campioni del segnale mid (L + R), senza MIDI. Si guarda il mid e non il solo
     * canale sinistro perche' lo spread stereo da' a ogni copia un guadagno diverso in L: nel
     * mid i guadagni restano invece quasi uguali fra loro (cos + sin varia poco attorno ai 45
     * gradi), quindi un confronto di ampiezza fra copie misura l'unison e non il pan.
     */
    static std::vector<float> renderMid (engine::SynthEngine& synth, int numSamples)
    {
        std::vector<float> out;
        out.reserve ((size_t) numSamples + 128);
        juce::MidiBuffer none;

        while ((int) out.size() < numSamples)
        {
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, none);

            const auto* l = buffer.getReadPointer (0);
            const auto* r = buffer.getReadPointer (1);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                out.push_back (0.5f * (l[i] + r[i]));
        }

        out.resize ((size_t) numSamples);
        return out;
    }

    /**
     * Ampiezza del segnale a una frequenza qualunque: una singola riga di DFT, valutata dove
     * serve invece che su tutta la griglia di una FFT. La finestra di Hann non e' un dettaglio
     * — senza, la dispersione spettrale di una sinusoide che non cade esattamente su un bin
     * riempirebbe i vuoti fra una copia e l'altra, e il test non saprebbe piu' distinguere otto
     * righe da una macchia larga.
     */
    static double dftMagnitude (const std::vector<float>& x, double frequencyHz, double sampleRate)
    {
        const auto n = (double) x.size();

        if (n <= 0.0)
            return 0.0;

        const auto omega = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
        double re = 0.0, im = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / n);
            const auto phase = omega * (double) i;
            re += (double) x[i] * window * std::cos (phase);
            im -= (double) x[i] * window * std::sin (phase);
        }

        return std::sqrt (re * re + im * im) / n;
    }

    /** Un motore pronto a suonare con questi due parametri e nient'altro di diverso. */
    static engine::EngineParams unisonParams (int voices, float detuneCents)
    {
        auto p = defaultParams();
        p.filterOn = false; // niente filtro a sporcare conteggi di zero e misure di livello
        p.sustain = 1.0f;
        p.unisonVoices = voices;
        p.detuneCents = detuneCents;
        return p;
    }

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("collectEngineParams: il choice unison diventa 1/2/4/8, detune diventa cent");
        {
            const int expected[] = { 1, 2, 4, 8 };

            for (int index = 0; index < 4; ++index)
            {
                const auto rawFor = [index] (params::ParamSlot slot) noexcept
                {
                    return slot == params::ParamSlot::unison ? (float) index : 0.0f;
                };

                expectEquals (params::collectEngineParams (rawFor).unisonVoices, expected[index],
                              "indice " + juce::String (index));
            }

            for (const float real : { 0.0f, 18.0f, 100.0f })
            {
                const auto raw = real / 100.0f; // detune ha mappa lineare 0..100
                const auto rawFor = [raw] (params::ParamSlot slot) noexcept
                {
                    return slot == params::ParamSlot::detune ? raw : 0.0f;
                };

                expectWithinAbsoluteError (params::collectEngineParams (rawFor).detuneCents, real, 1.0e-3f);
            }
        }

        beginTest ("gli offset di detune sono simmetrici attorno allo zero e coprono +-detune");
        {
            // L'esempio del brief: N = 4 e detune 18 cent danno -18, -6, +6, +18.
            const float expected4[] = { -18.0f, -6.0f, 6.0f, 18.0f };

            for (int i = 0; i < 4; ++i)
                expectWithinAbsoluteError (18.0f * engine::unisonSpread (i, 4), expected4[i], 1.0e-4f,
                                           "copia " + juce::String (i));

            expectWithinAbsoluteError (18.0f * engine::unisonSpread (0, 2), -18.0f, 1.0e-4f);
            expectWithinAbsoluteError (18.0f * engine::unisonSpread (1, 2), 18.0f, 1.0e-4f);

            // N = 1: nessun offset, ed e' un'uguaglianza esatta perche' da questa dipende la
            // non-regressione (la conversione cent -> rapporto deve dare esattamente 1.0f).
            expectEquals (engine::unisonSpread (0, 1), 0.0f);

            for (const int n : { 2, 4, 8 })
            {
                double sum = 0.0;
                for (int i = 0; i < n; ++i)
                    sum += (double) engine::unisonSpread (i, n);

                expectWithinAbsoluteError ((float) sum, 0.0f, 1.0e-5f, "somma, N " + juce::String (n));
                expectWithinAbsoluteError (engine::unisonSpread (0, n), -1.0f, 1.0e-6f);
                expectWithinAbsoluteError (engine::unisonSpread (n - 1, n), 1.0f, 1.0e-6f);
            }
        }

        beginTest ("resetToPhase sposta la fase di esattamente la frazione chiesta");
        {
            // A 375 Hz con sample rate 48 kHz l'incremento di fase vale 2^-7 esatti: un quarto
            // di ciclo sono 32 campioni tondi, e il confronto puo' essere a tolleranza zero.
            // E' il mattone su cui poggia la distribuzione delle fasi dell'unison, quindi si
            // verifica da solo prima di verificarlo attraverso il motore.
            dsp::WavetableOscillator fromZero, fromQuarter;

            for (auto* oscillator : { &fromZero, &fromQuarter })
            {
                oscillator->prepare (48000.0);
                oscillator->setTable (store.active());
                oscillator->setFrequencyHz (375.0f);
                oscillator->setFramePosition (0.0f);
            }

            fromZero.reset();
            fromQuarter.resetToPhase (0.25f);

            std::vector<float> zero (160);
            for (auto& value : zero)
                value = fromZero.getSample();

            for (int i = 0; i < 128; ++i)
                expectWithinAbsoluteError (fromQuarter.getSample(), zero[(size_t) (i + 32)], 0.0f,
                                           "campione " + juce::String (i));

            // E un giro intero riporta esattamente dove si era: 1.0 deve valere 0.0.
            dsp::WavetableOscillator wrapped;
            wrapped.prepare (48000.0);
            wrapped.setTable (store.active());
            wrapped.setFrequencyHz (375.0f);
            wrapped.setFramePosition (0.0f);
            wrapped.resetToPhase (1.0f);
            expectWithinAbsoluteError (wrapped.getSample(), zero[0], 0.0f);
        }

        beginTest ("le N copie suonano a N intonazioni distinte, simmetriche attorno alla nota");
        {
            // Si guarda lo spettro attorno alla nota: con detune a fondo corsa le copie sono
            // separate di abbastanza da risolverle una per una. Il test chiede tre cose
            // insieme — che ci sia una riga su ciascuna delle N frequenze attese (quindi le
            // copie attive sono N), che fra una riga e l'altra non ci sia niente, e che fuori
            // dall'intervallo +-detune non ci sia niente (quindi la distribuzione e' proprio
            // quella simmetrica, non una a partire da zero verso l'alto).
            constexpr int noteNumber = 81; // La 880 Hz: piu' alta e' la nota, piu' larghe in Hz
            constexpr float baseHz = 880.0f;   // sono le distanze fra le copie
            constexpr float detune = 100.0f;   // fondo corsa
            constexpr double sampleRate = 48000.0;
            constexpr int windowSamples = 32768; // 0.68 s: 1.46 Hz per bin

            const auto centsToHz = [] (float cents) { return baseHz * std::exp2 (cents / 1200.0f); };

            for (const int voices : { 1, 2, 4, 8 })
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (voices, detune));
                noteOn (synth, noteNumber);

                renderPeak (synth, 40, *this);
                const auto mid = renderMid (synth, windowSamples);

                // Le righe attese, e l'ampiezza piu' bassa e piu' alta fra loro.
                double weakest = 1.0e9, strongest = 0.0;

                for (int i = 0; i < voices; ++i)
                {
                    const auto amplitude = dftMagnitude (mid, centsToHz (detune * engine::unisonSpread (i, voices)),
                                                         sampleRate);
                    weakest = juce::jmin (weakest, amplitude);
                    strongest = juce::jmax (strongest, amplitude);
                }

                expect (weakest > 0.5 * strongest,
                        "unison " + juce::String (voices) + ": una copia e' molto piu' debole "
                            + "delle altre (" + juce::String (weakest) + " contro "
                            + juce::String (strongest) + ")");

                // Fra due copie adiacenti (e appena fuori dall'intervallo) non deve esserci
                // nulla: e' cio' che distingue N righe da una macchia larga.
                double intruder = 0.0;

                for (int i = 0; i + 1 < voices; ++i)
                {
                    const auto midpoint = 0.5f * detune
                                              * (engine::unisonSpread (i, voices)
                                                 + engine::unisonSpread (i + 1, voices));
                    intruder = juce::jmax (intruder, dftMagnitude (mid, centsToHz (midpoint), sampleRate));
                }

                for (const float outside : { -1.5f * detune, 1.5f * detune })
                    intruder = juce::jmax (intruder, dftMagnitude (mid, centsToHz (outside), sampleRate));

                expect (intruder < 0.2 * weakest,
                        "unison " + juce::String (voices) + ": c'e' energia dove non dovrebbe "
                            + "essercene (" + juce::String (intruder) + " contro "
                            + juce::String (weakest) + ")");
            }
        }

        beginTest ("a detune zero le fasi distribuite impediscono alle copie di sommarsi in fase");
        {
            // Otto copie tutte alla stessa frequenza e tutte a fase zero sarebbero lo stesso
            // segnale sommato otto volte: ampiezza 8x, che la compensazione 1/sqrt(8) riduce a
            // 2.83x — quasi 9 dB regalati appena si tocca il knob. Distribuite su i/8 del ciclo
            // si sommano invece come sorgenti distinte, e la riga fondamentale resta ben sotto
            // quella di una copia sola.
            constexpr int noteNumber = 81;
            constexpr double sampleRate = 48000.0;

            const auto fundamental = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (voices, 0.0f));
                noteOn (synth, noteNumber);

                renderPeak (synth, 40, *this);
                return dftMagnitude (renderMid (synth, 16384), 880.0, sampleRate);
            };

            const auto one = fundamental (1);
            const auto eight = fundamental (8);
            const auto ratio = one > 0.0 ? eight / one : 0.0;

            expect (ratio < 0.7,
                    "le otto copie si stanno sommando in fase: fondamentale " + juce::String (ratio)
                        + " volte quella di una copia sola (a fase zero sarebbe 2.83)");

            logMessage ("detune 0, unison 8: fondamentale " + juce::String (ratio)
                        + " volte quella di unison 1");
        }

        beginTest ("detune zero con unison 8 non produce battimenti; con detune si'");
        {
            const auto breathing = [&store, this] (float detuneCents)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (8, detuneCents));
                noteOn (synth, 60);

                renderPeak (synth, 40, *this); // ~0.1 s: attacco e rampa di level
                return relativeSpread (rmsWindows (synth, 50, 8));
            };

            const auto flat = breathing (0.0f);
            const auto beating = breathing (18.0f);

            // Otto copie alla stessa identica frequenza: la somma e' periodica, l'ampiezza non
            // ha ragione di muoversi. Se le fasi non fossero distribuite il segnale sarebbe
            // ancora piatto, ma otto volte piu' forte — quello lo becca il test del livello.
            expect (flat < 0.02f,
                    "detune 0: l'ampiezza dovrebbe restare piatta, variazione relativa "
                        + juce::String (flat));

            // A 261.6 Hz, +-18 cent separano le copie estreme di 5.4 Hz: un battimento ogni
            // 185 ms, cioe' cinque o sei cicli dentro la finestra misurata.
            expect (beating > flat * 5.0f && beating > 0.05f,
                    "detune 18: i battimenti non si vedono, variazione relativa "
                        + juce::String (beating) + " contro " + juce::String (flat));
        }

        beginTest ("il livello resta costante fra unison 1 e unison 8");
        {
            const auto rms = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (voices, 18.0f));
                noteOn (synth, 60);

                renderPeak (synth, 40, *this);
                return renderRms (synth, 750); // ~2 s: una decina di cicli di battimento
            };

            const auto one = rms (1);
            const auto eight = rms (8);
            const auto deltaDb = juce::Decibels::gainToDecibels (eight / one);

            expect (std::abs (deltaDb) < 1.5f,
                    "unison 8 sta " + juce::String (deltaDb, 2) + " dB da unison 1");

            logMessage ("livello: unison 1 " + juce::String (juce::Decibels::gainToDecibels (one), 2)
                        + " dBFS RMS, unison 8 "
                        + juce::String (juce::Decibels::gainToDecibels (eight), 2) + " dBFS RMS ("
                        + juce::String (deltaDb, 2) + " dB)");
        }

        beginTest ("l'immagine stereo si allarga con unison");
        {
            const auto correlation = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                auto p = unisonParams (voices, 18.0f);
                p.pan = 0.0f;
                synth.setParams (p);
                noteOn (synth, 60);

                renderPeak (synth, 40, *this);
                return channelCorrelation (synth, 400);
            };

            const auto mono = correlation (1);
            const auto wide = correlation (8);

            // Una copia sola al centro: i due canali sono lo stesso segnale, moltiplicato per
            // lo stesso guadagno. Correlazione 1, senza margini.
            expectWithinAbsoluteError (mono, 1.0f, 1.0e-4f,
                                       "unison 1 dovrebbe restare puntiforme, correlazione "
                                           + juce::String (mono));

            expect (wide < 0.9f, "unison 8 non allarga: correlazione " + juce::String (wide));
            expect (wide > 0.0f, "correlazione negativa (" + juce::String (wide)
                                     + "): lo spread sta invertendo la fase, non allargando");

            logMessage ("correlazione L/R: unison 1 " + juce::String (mono) + ", unison 8 "
                        + juce::String (wide));
        }

        beginTest ("unison 1: detune non tocca un solo campione");
        {
            // Il criterio di accettazione che conta piu' di tutti, in forma di test permanente:
            // il knob detune di default e' a 18 cent, quindi se l'unison lo leggesse anche con
            // una copia sola lo strumento cambierebbe suono a parametri invariati.
            const auto render = [&store] (float detuneCents)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                auto p = unisonParams (1, detuneCents);
                p.filterOn = true;
                synth.setParams (p);
                synth.setMasterGainLinear (0.8f);

                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                juce::AudioBuffer<float> first (2, 128);
                first.clear();
                synth.process (first, midi);

                std::vector<float> out;
                juce::MidiBuffer none;
                for (int b = 0; b < 30; ++b)
                {
                    juce::AudioBuffer<float> buffer (2, 128);
                    buffer.clear();
                    synth.process (buffer, none);
                    const auto* data = buffer.getReadPointer (0);
                    out.insert (out.end(), data, data + buffer.getNumSamples());
                }

                return out;
            };

            const auto quiet = render (0.0f);
            const auto full = render (100.0f);

            expectEquals ((int) full.size(), (int) quiet.size());
            for (size_t i = 0; i < quiet.size(); ++i)
                expectWithinAbsoluteError (full[i], quiet[i], 0.0f);
        }

        beginTest ("unison 8 su sedici voci: niente NaN, niente campioni fuori scala");
        {
            // La stessa configurazione dura misurata anche a unison 1: il numero da guardare non
            // e' il picco in se' (il soft clipper lo tiene comunque sotto 1.0) ma di quanto
            // unison lo sposta, perche' e' quello che entra nel gain staging.
            const auto peakWith = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = unisonParams (voices, 100.0f); // detune a fondo corsa
                p.filterOn = true;
                p.cutoffHz = 4000.0f;
                p.resonanceQ = 6.0f;
                p.driveGain = 4.0f;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer midi;
                for (int i = 0; i < engine::VoiceManager::maxVoices; ++i)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 30 + i * 4, 1.0f), 0);

                juce::AudioBuffer<float> first (2, 128);
                first.clear();
                synth.process (first, midi);

                return renderPeak (synth, 200, *this); // verifica gia' che ogni campione sia finito
            };

            for (const int voices : { 1, 8 })
            {
                const auto peak = peakWith (voices);

                expect (peak <= 1.0f, "unison " + juce::String (voices) + ": campione fuori scala, picco "
                                          + juce::String (peak));
                expect (peak > 0.05f, "unison " + juce::String (voices) + ": uscita troppo bassa ("
                                          + juce::String (peak) + "), il test non verifica niente");

                logMessage ("16 voci, detune 100 ct, Q 6, drive +12 dB, unison "
                            + juce::String (voices) + ": picco " + juce::String (peak) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
            }
        }
    }
};

static UnisonTests unisonTests;
