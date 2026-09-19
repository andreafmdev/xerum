/**
 * La giuntura fra i parametri veri dell'APVTS e il motore.
 *
 * Tutti gli altri test di parametri esercitano params::collectEngineParams con un accessore
 * finto che restituisce valori normalizzati 0..1. E' comodo — non serve linkare l'APVTS — ma
 * lascia scoperta proprio la domanda che conta: *cosa restituisce davvero* getRawParameterValue
 * per ognuno dei quattro Kind. Un bug che vive in quella conversione e' invisibile a ogni altro
 * test della suite, ed e' esattamente li' che se n'e' annidato uno per mesi: `oct` e `semi` sono
 * AudioParameterInt, che vivono nel loro range naturale (-3..3 e -12..12), mentre
 * collectEngineParams li denormalizzava come se fossero 0..1 — quindi ai valori di default
 * (0 e 0) il motore trasponeva ogni nota di -3 ottave e -12 semitoni, quattro ottave sotto il
 * tasto premuto.
 *
 * Questo file costruisce il layout vero, legge i puntatori veri e verifica la conversione.
 */
#include "parameters/ParamCollect.h"
#include "parameters/ParameterLayout.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace
{
/** Un AudioProcessor minimo: serve solo a possedere un APVTS costruito col layout vero. */
struct DummyProcessor final : juce::AudioProcessor
{
    DummyProcessor() : apvts (*this, nullptr, "PARAMS", params::createParameterLayout()) {}

    const juce::String getName() const override { return "Dummy"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

/** Lo stesso accessore di PluginProcessor::collectParams, sui parametri veri. */
struct RealAccessor
{
    explicit RealAccessor (DummyProcessor& p)
    {
        for (const auto& spec : params::kTable)
            byId.set (spec.id, p.apvts.getRawParameterValue (spec.id));

        slot[(size_t) params::ParamSlot::oscOn]   = byId["oscOn"];
        slot[(size_t) params::ParamSlot::wtpos]   = byId["wtpos"];
        slot[(size_t) params::ParamSlot::oct]     = byId["oct"];
        slot[(size_t) params::ParamSlot::semi]    = byId["semi"];
        slot[(size_t) params::ParamSlot::fine]    = byId["fine"];
        slot[(size_t) params::ParamSlot::level]   = byId["level"];
        slot[(size_t) params::ParamSlot::filtOn]  = byId["filtOn"];
        slot[(size_t) params::ParamSlot::ftype]   = byId["ftype"];
        slot[(size_t) params::ParamSlot::slope]   = byId["slope"];
        slot[(size_t) params::ParamSlot::cutoff]  = byId["cutoff"];
        slot[(size_t) params::ParamSlot::res]     = byId["res"];
        slot[(size_t) params::ParamSlot::drive]   = byId["drive"];
        slot[(size_t) params::ParamSlot::keytrk]  = byId["keytrk"];
        slot[(size_t) params::ParamSlot::att]     = byId["att"];
        slot[(size_t) params::ParamSlot::dec]     = byId["dec"];
        slot[(size_t) params::ParamSlot::sus]     = byId["sus"];
        slot[(size_t) params::ParamSlot::rel]     = byId["rel"];
        slot[(size_t) params::ParamSlot::envVel]  = byId["envVel"];
        slot[(size_t) params::ParamSlot::pan]     = byId["pan"];
        slot[(size_t) params::ParamSlot::bypass]  = byId["bypass"];
        slot[(size_t) params::ParamSlot::lshape]  = byId["lshape"];
        slot[(size_t) params::ParamSlot::lrate]   = byId["lrate"];
        slot[(size_t) params::ParamSlot::lsync]   = byId["lsync"];
        slot[(size_t) params::ParamSlot::lphase]  = byId["lphase"];
        slot[(size_t) params::ParamSlot::lfade]   = byId["lfade"];
        slot[(size_t) params::ParamSlot::lretrig] = byId["lretrig"];
        slot[(size_t) params::ParamSlot::unison]  = byId["unison"];
        slot[(size_t) params::ParamSlot::detune]  = byId["detune"];
    }

    float operator() (params::ParamSlot s) const noexcept
    {
        const auto* p = slot[(size_t) s];
        return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f;
    }

    juce::HashMap<juce::String, std::atomic<float>*> byId;
    std::array<std::atomic<float>*, (size_t) params::ParamSlot::count> slot {};
};

/** Scrive un parametro nelle sue unita' naturali, come farebbe l'utente girando il knob. */
void setNatural (DummyProcessor& p, const char* id, float natural)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (natural));
}
} // namespace

struct ParameterSeamTests final : juce::UnitTest
{
    ParameterSeamTests() : juce::UnitTest ("giuntura APVTS -> motore", "params") {}

    void runTest() override
    {
        DummyProcessor processor;
        RealAccessor raw { processor };

        beginTest ("ai default di fabbrica lo strumento non traspone");
        {
            const auto p = params::collectEngineParams (raw);

            // Il difetto che questo test blocca: oct e semi sono AudioParameterInt, quindi
            // getRawParameterValue restituisce -3..3 e -12..12, non 0..1. Denormalizzarli
            // come float dava -3 ottave e -12 semitoni al valore di default (0 e 0): ogni
            // nota suonava quattro ottave sotto il tasto premuto.
            expectEquals (p.octave, 0, "ai default l'ottava deve essere 0");
            expectEquals (p.semitones, 0, "ai default i semitoni devono essere 0");
            expectWithinAbsoluteError (p.fineCents, 0.0f, 0.5f);
        }

        beginTest ("oct e semi coprono tutto il loro range, non solo gli estremi");
        {
            for (int oct = -3; oct <= 3; ++oct)
            {
                setNatural (processor, "oct", (float) oct);
                expectEquals (params::collectEngineParams (raw).octave, oct,
                              "oct naturale " + juce::String (oct));
            }
            setNatural (processor, "oct", 0.0f);

            for (int semi : { -12, -7, -1, 0, 1, 7, 12 })
            {
                setNatural (processor, "semi", (float) semi);
                expectEquals (params::collectEngineParams (raw).semitones, semi,
                              "semi naturale " + juce::String (semi));
            }
            setNatural (processor, "semi", 0.0f);
        }

        beginTest ("i float restano normalizzati e si denormalizzano come sempre");
        {
            // I Kind::Float sono creati con NormalisableRange {0, 1}: naturale e normalizzato
            // coincidono, ed e' il motivo per cui il difetto di oct/semi era l'unico.
            setNatural (processor, "cutoff", 0.62f);
            expectWithinAbsoluteError (params::collectEngineParams (raw).cutoffHz,
                                       params::cutoffHzFromRaw (0.62f), 1.0f);

            setNatural (processor, "res", 0.3f);
            expectWithinAbsoluteError (params::collectEngineParams (raw).resonanceQ,
                                       params::resonanceQFromRaw (0.3f), 1.0e-3f);
        }

        beginTest ("i choice arrivano come indice");
        {
            setNatural (processor, "slope", 0.0f);
            expectEquals (params::collectEngineParams (raw).filterStages, 1);
            setNatural (processor, "slope", 1.0f);
            expectEquals (params::collectEngineParams (raw).filterStages, 2);

            setNatural (processor, "unison", 3.0f); // quarta opzione: "8"
            expectEquals (params::collectEngineParams (raw).unisonVoices, 8);
            setNatural (processor, "unison", 0.0f);
            expectEquals (params::collectEngineParams (raw).unisonVoices, 1);

            setNatural (processor, "ftype", 1.0f);
            expect (params::collectEngineParams (raw).filterType
                        == dsp::StateVariableFilter::Type::highPass);
            setNatural (processor, "ftype", 0.0f);
        }

        beginTest ("i bool arrivano come 0/1");
        {
            setNatural (processor, "filtOn", 0.0f);
            expect (! params::collectEngineParams (raw).filterOn);
            setNatural (processor, "filtOn", 1.0f);
            expect (params::collectEngineParams (raw).filterOn);
        }
    }
};

static ParameterSeamTests parameterSeamTests;
