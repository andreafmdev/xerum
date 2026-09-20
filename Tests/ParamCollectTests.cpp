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

// --- Task 8, Ruling C: la stessa mappatura di PluginProcessor::collectParams, ma esercitata
// direttamente su params::collectEngineParams (ParamCollect.h) con un accessor finto. A
// differenza dei due test di tuning sopra (che ricostruiscono l'aritmetica di Map::Linear a
// mano, perche' non potevano linkare juce_audio_processors), questo chiama il vero codice di
// produzione: se il cast troncante tornasse, questo test lo becca, gli altri due no.
struct ParamCollectTests final : juce::UnitTest
{
    ParamCollectTests() : juce::UnitTest ("collectEngineParams", "engine") {}

    /** Accessor finto: id -> valore normalizzato 0..1. Non e' codice del thread audio (e'
        codice di test), quindi std::map va benissimo qui. */
    struct FakeRaw
    {
        // Chiave params::ParamSlot, non stringa: e' esattamente cio' che collectEngineParams
        // passa all'accessore ora (vedi ParamCollect.h), quindi il finto qui rispecchia il vero
        // PluginProcessor::paramSlots_[(size_t) slot].
        std::map<params::ParamSlot, float> values;

        float operator() (params::ParamSlot slot) const
        {
            const auto it = values.find (slot);
            return it != values.end() ? it->second : 0.0f;
        }
    };

    /** Normalizzato che, passato a Map::Linear, ridà `real`. */
    static float rawForLinear (const char* id, float real)
    {
        const auto* spec = params::find (id);
        return (real - spec->min) / (spec->max - spec->min);
    }

    /** Normalizzato che, passato a Map::MsSquared, ridà `realMs`. */
    static float rawForMsSquared (const char* id, float realMs)
    {
        const auto* spec = params::find (id);
        return std::sqrt ((realMs - spec->min) / (spec->max - spec->min));
    }

    void runTest() override
    {
        beginTest ("oct/semi arrivano in unita' naturali, non normalizzate");
        {
            // Questo test alimentava l'accessore finto con valori *normalizzati*, ed e' quella
            // assunzione ad aver nascosto per mesi il difetto vero: oct e semi sono gli unici
            // Kind::Int, creati da ParameterMapping.h come juce::AudioParameterInt nel loro
            // range naturale, quindi getRawParameterValue restituisce -3..3 e -12..12, non
            // 0..1. Denormalizzarli dava -3 ottave e -12 semitoni al default, cioe' ogni nota
            // quattro ottave sotto il tasto premuto. La verifica contro l'APVTS vero sta in
            // Tests/ParameterSeamTests.cpp; qui si blocca l'aritmetica.
            const int semiCases[] = { -12, -4, -1, 0, 2, 5, 8, 12 };
            for (auto expected : semiCases)
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::semi] = (float) expected;
                const auto p = params::collectEngineParams (raw);
                expectEquals (p.semitones, expected, "semi " + juce::String (expected));
            }

            const int octCases[] = { -3, -1, 0, 2, 3 };
            for (auto expected : octCases)
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::oct] = (float) expected;
                const auto p = params::collectEngineParams (raw);
                expectEquals (p.octave, expected, "oct " + juce::String (expected));
            }

            // Il float che arriva puo' cadere appena sotto l'intero per arrotondamento: e' il
            // caso che rende roundToInt necessario al posto di un cast troncante.
            FakeRaw nearlyEight;
            nearlyEight.values[params::ParamSlot::semi] = 7.999998f;
            expectEquals (params::collectEngineParams (nearlyEight).semitones, 8);
        }

        beginTest ("att/dec/rel: da ms denormalizzati a secondi per il motore");
        {
            FakeRaw raw;
            raw.values[params::ParamSlot::att] = rawForMsSquared ("att", 500.0f);
            raw.values[params::ParamSlot::dec] = rawForMsSquared ("dec", 1000.0f);
            raw.values[params::ParamSlot::rel] = rawForMsSquared ("rel", 2000.0f);
            const auto p = params::collectEngineParams (raw);

            expectWithinAbsoluteError (p.attackSeconds, 0.5f, 1.0e-4f);
            expectWithinAbsoluteError (p.decaySeconds, 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (p.releaseSeconds, 2.0f, 1.0e-4f);
        }

        beginTest ("sus/res/keytrk/envVel: dalla percentuale alla frazione 0..1");
        {
            FakeRaw raw;
            raw.values[params::ParamSlot::sus] = rawForLinear ("sus", 70.0f);
            raw.values[params::ParamSlot::res] = rawForLinear ("res", 40.0f);
            raw.values[params::ParamSlot::keytrk] = rawForLinear ("keytrk", 25.0f);
            raw.values[params::ParamSlot::envVel] = rawForLinear ("envVel", 60.0f);
            const auto p = params::collectEngineParams (raw);

            expectWithinAbsoluteError (p.sustain, 0.7f, 1.0e-5f);
            // res passa anche per la mappa esponenziale 0.707..12 dopo la denormalizzazione:
            // si verifica la formula intera, non solo denormalise(). Esponenziale e non
            // lineare: con la vecchia jmap, res 40 % dava gia' Q 8.4 (+18 dB di picco).
            const auto expectedQ = dsp::StateVariableFilter::kButterworthQ
                                       * std::pow (12.0f / dsp::StateVariableFilter::kButterworthQ, 0.4f);
            expectWithinAbsoluteError (p.resonanceQ, expectedQ, 1.0e-4f);
            expect (expectedQ < 2.5f, "a res 40 % la risonanza deve essere ancora moderata, Q "
                                          + juce::String (expectedQ));
            expectWithinAbsoluteError (p.keyTrack, 0.25f, 1.0e-5f);
            expectWithinAbsoluteError (p.velocityAmount, 0.6f, 1.0e-5f);
        }

        beginTest ("att2/dec2/sus2/rel2: il secondo inviluppo arriva in secondi e frazione");
        {
            FakeRaw raw;
            raw.values[params::ParamSlot::att2] = rawForMsSquared ("att2", 250.0f);
            raw.values[params::ParamSlot::dec2] = rawForMsSquared ("dec2", 750.0f);
            raw.values[params::ParamSlot::sus2] = rawForLinear ("sus2", 30.0f);
            raw.values[params::ParamSlot::rel2] = rawForMsSquared ("rel2", 1500.0f);
            const auto p = params::collectEngineParams (raw);

            expectWithinAbsoluteError (p.attack2Seconds, 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (p.decay2Seconds, 0.75f, 1.0e-4f);
            expectWithinAbsoluteError (p.sustain2, 0.3f, 1.0e-5f);
            expectWithinAbsoluteError (p.release2Seconds, 1.5f, 1.0e-4f);
        }

        beginTest ("env2 riusa mappe, range e default dei quattro di env");
        {
            // Non e' pignoleria: e' la promessa fatta a chi carica un patch vecchio. I quattro
            // parametri nuovi non compaiono in nessuno stato salvato ne' in nessun preset di
            // fabbrica, quindi cadono sul default — e un default diverso da quello dell'env
            // d'ampiezza renderebbe il secondo inviluppo una sorpresa invece che un punto di
            // partenza prevedibile.
            const std::pair<const char*, const char*> pairs[] = {
                { "att", "att2" }, { "dec", "dec2" }, { "sus", "sus2" }, { "rel", "rel2" }
            };

            for (const auto& [firstId, secondId] : pairs)
            {
                const auto* a = params::find (firstId);
                const auto* b = params::find (secondId);
                expect (a != nullptr && b != nullptr, juce::String (firstId) + "/" + secondId + ": spec mancante");

                expect (a->kind == b->kind, juce::String (secondId) + ": kind diverso da " + firstId);
                expect (a->map == b->map, juce::String (secondId) + ": mappa diversa da " + firstId);
                expectWithinAbsoluteError (b->min, a->min, 0.0f, juce::String (secondId) + ": min diverso");
                expectWithinAbsoluteError (b->max, a->max, 0.0f, juce::String (secondId) + ": max diverso");
                expectWithinAbsoluteError (b->def, a->def, 0.0f, juce::String (secondId) + ": default diverso");
            }
        }

        beginTest ("pan: da -50..+50 a -1..+1");
        {
            for (float target : { -50.0f, 0.0f, 25.0f, 50.0f })
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::pan] = rawForLinear ("pan", target);
                const auto p = params::collectEngineParams (raw);
                expectWithinAbsoluteError (p.pan, target * 0.02f, 1.0e-5f);
            }
        }

        beginTest ("level: il valore grezzo della mappa Db e' gia' il guadagno lineare");
        {
            // Se collectEngineParams chiamasse per errore denormalise() su level (che ha
            // Map::Db), un x=0.37 diventerebbe circa -8.6 dB invece di restare 0.37: questo test
            // lo becca. (volume ha la stessa proprieta' ma non passa da qui: il guadagno master
            // si calcola altrove, in PluginProcessor::processBlock, non toccato da questo refactor.)
            for (float target : { 0.0f, 0.37f, 1.0f })
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::level] = target;
                const auto p = params::collectEngineParams (raw);
                expectWithinAbsoluteError (p.level, target, 1.0e-6f);
            }
        }
    }
};

static ParamCollectTests paramCollectTests;

/**
 * Le conversioni da valore normalizzato a valore reale sono state estratte da
 * collectEngineParams in funzioni proprie (params::cutoffHzFromRaw e compagne) perche' il
 * percorso modulato deve usare le stesse: questo test e' il guardiano che le tiene allineate.
 * Se qualcuno cambia una formula in un posto solo, qui si spacca.
 */
struct ParamConversionTests final : juce::UnitTest
{
    ParamConversionTests() : juce::UnitTest ("conversioni dei parametri", "params") {}

    void runTest() override
    {
        beginTest ("collectEngineParams riempie le basi normalizzate dei target modulabili");
        {
            // Ogni slot ritorna un valore diverso, cosi' uno scambio fra due target si vede.
            const auto rawFor = [] (params::ParamSlot slot) noexcept
            {
                switch (slot)
                {
                    case params::ParamSlot::cutoff: return 0.11f;
                    case params::ParamSlot::res:    return 0.22f;
                    case params::ParamSlot::wtpos:  return 0.33f;
                    case params::ParamSlot::level:  return 0.44f;
                    case params::ParamSlot::pan:    return 0.55f;
                    case params::ParamSlot::fine:   return 0.66f;
                    case params::ParamSlot::drive:  return 0.77f;
                    default:                        return 0.5f;
                }
            };

            const auto p = params::collectEngineParams (rawFor);

            const auto base = [&p] (params::ParamSlot slot)
            {
                return p.modBase[(size_t) engine::modTargetIndexFor (slot)];
            };

            expectWithinAbsoluteError (base (params::ParamSlot::cutoff), 0.11f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::res), 0.22f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::wtpos), 0.33f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::level), 0.44f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::pan), 0.55f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::fine), 0.66f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::drive), 0.77f, 1.0e-6f);

            // I campi denormalizzati restano quelli di sempre: le basi si aggiungono, non sostituiscono.
            expectWithinAbsoluteError (p.cutoffHz, params::cutoffHzFromRaw (0.11f), 1.0e-2f);
        }

        beginTest ("i parametri dell'LFO arrivano grezzi in EngineParams");
        {
            const auto rawFor = [] (params::ParamSlot slot) noexcept
            {
                switch (slot)
                {
                    case params::ParamSlot::lshape:  return 2.0f;  // choice: il grezzo e' gia' l'indice
                    case params::ParamSlot::lrate:   return 0.45f;
                    case params::ParamSlot::lsync:   return 1.0f;
                    case params::ParamSlot::lphase:  return 0.25f;
                    case params::ParamSlot::lfade:   return 0.5f;
                    case params::ParamSlot::lretrig: return 0.0f;
                    default:                         return 0.0f;
                }
            };

            const auto p = params::collectEngineParams (rawFor);

            expectEquals (p.lfoShapeIndex, 2);
            expectWithinAbsoluteError (p.lfoRateRaw, 0.45f, 1.0e-6f);
            expect (p.lfoSync);
            // lphase e' 0..360 gradi nella tabella: in EngineParams diventa 0..1.
            expectWithinAbsoluteError (p.lfoPhaseOffset01, 0.25f, 1.0e-6f);
            // lfade e' 0..4000 ms: in EngineParams diventa secondi.
            expectWithinAbsoluteError (p.lfoFadeSeconds, 2.0f, 1.0e-3f);
            expect (! p.lfoRetrig);
        }

        beginTest ("le conversioni estratte coincidono con quelle di collectEngineParams");
        {
            // Griglia fitta: una divergenza anche solo agli estremi della corsa si vede.
            for (int i = 0; i <= 20; ++i)
            {
                const auto raw = (float) i / 20.0f;

                const auto p = params::collectEngineParams (
                    [raw] (params::ParamSlot) noexcept { return raw; });

                expectWithinAbsoluteError (params::cutoffHzFromRaw (raw), p.cutoffHz, 1.0e-2f);
                expectWithinAbsoluteError (params::resonanceQFromRaw (raw), p.resonanceQ, 1.0e-4f);
                expectWithinAbsoluteError (params::framePositionFromRaw (raw), p.framePosition, 1.0e-6f);
                expectWithinAbsoluteError (params::levelGainFromRaw (raw), p.level, 1.0e-6f);
                expectWithinAbsoluteError (params::panFromRaw (raw), p.pan, 1.0e-6f);
                expectWithinAbsoluteError (params::fineCentsFromRaw (raw), p.fineCents, 1.0e-3f);
                expectWithinAbsoluteError (params::driveGainFromRaw (raw), p.driveGain, 1.0e-4f);
            }
        }
    }
};

static ParamConversionTests paramConversionTests;
