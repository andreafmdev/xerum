#include "dsp/ADSREnvelope.h"
#include "dsp/StateVariableFilter.h"

#include <juce_core/juce_core.h>
#include <cmath>

struct ADSRTests final : juce::UnitTest
{
    ADSRTests() : juce::UnitTest ("ADSREnvelope", "dsp") {}

    static float runFor (dsp::ADSREnvelope& env, int samples)
    {
        float last = 0.0f;
        for (int i = 0; i < samples; ++i)
            last = env.getNextSample();
        return last;
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("attack reaches peak within stated time, not before");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            const auto halfway = runFor (env, 2400);  // 50 ms
            expect (halfway < 0.99f, "at midpoint attack not yet complete");

            const auto atEnd = runFor (env, 2400);    // 100 ms total
            expect (atEnd >= 0.99f, "level at attack end " + juce::String (atEnd));
        }

        beginTest ("decay reaches sustain level and holds");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.4f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            runFor (env, 48);                          // attack
            const auto afterDecay = runFor (env, 2400); // 50 ms
            expectWithinAbsoluteError (afterDecay, 0.4f, 0.02f);

            const auto later = runFor (env, 48000);     // one second of sustain
            expectWithinAbsoluteError (later, 0.4f, 0.001f);
        }

        beginTest ("decay timing correct with high sustain level");
        {
            // This test pins the decay distance fix: sustain close to peak
            // should not cause immediate transition to sustain stage.
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.995f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            runFor (env, 48);                          // attack
            const auto halfway = runFor (env, 1200);   // 25 ms into decay
            expect (halfway > 0.995f, "partway through decay level above sustain");

            const auto atEnd = runFor (env, 1200);     // 50 ms total decay
            expectWithinAbsoluteError (atEnd, 0.995f, 0.005f);
        }

        beginTest ("release falls below -80 dB and envelope goes inactive");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.001f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.05f);
            env.noteOn (1.0f);
            runFor (env, 480);

            env.noteOff();
            expect (env.isActive(), "during release voice is still active");

            const auto tail = runFor (env, 2400); // 50 ms
            expect (tail < 1.0e-4f, "tail level " + juce::String (tail));
            expect (! env.isActive(), "after release voice is freed");
        }

        beginTest ("velocity parameter scales the peak");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (0.5f);

            const auto peak = runFor (env, 480);
            expectWithinAbsoluteError (peak, 0.5f, 0.02f);
        }

        beginTest ("reset clears all state");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (0.1f);
            env.setSustainLevel (0.5f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);
            runFor (env, 480);
            env.reset();
            expect (! env.isActive());
            expectWithinAbsoluteError (env.getNextSample(), 0.0f, 0.0f);
        }
    }
};

static ADSRTests adsrTests;

namespace
{
/** Ampiezza in uscita dal filtro a una data frequenza, misurata a regime. */
float filterGainAt (dsp::StateVariableFilter& filter, float frequencyHz, double sampleRate)
{
    filter.reset();

    const auto increment = 2.0 * juce::MathConstants<double>::pi * (double) frequencyHz / sampleRate;
    const int settle = (int) (sampleRate * 0.2);
    const int measure = (int) (sampleRate * 0.1);

    double phase = 0.0;
    for (int i = 0; i < settle; ++i, phase += increment)
        filter.processSample ((float) std::sin (phase));

    double sumSquares = 0.0;
    for (int i = 0; i < measure; ++i, phase += increment)
    {
        const auto out = filter.processSample ((float) std::sin (phase));
        sumSquares += (double) out * (double) out;
    }

    // RMS in uscita diviso l'RMS di un seno di ampiezza 1 (cioe 1/sqrt(2)).
    return (float) (std::sqrt (sumSquares / measure) * std::sqrt (2.0));
}
} // namespace

struct StateVariableFilterTests final : juce::UnitTest
{
    StateVariableFilterTests() : juce::UnitTest ("StateVariableFilter", "dsp") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("passa-basso Butterworth: -3 dB al cutoff");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (1);
            filter.setResonance (juce::MathConstants<float>::sqrt2 / 2.0f); // Q = 0.707
            filter.setCutoffHz (1000.0f);

            expectWithinAbsoluteError (filterGainAt (filter, 1000.0f, sampleRate), 0.707f, 0.03f);
            expect (filterGainAt (filter, 100.0f, sampleRate) > 0.95f, "in banda passante deve passare");
            expect (filterGainAt (filter, 8000.0f, sampleRate) < 0.1f, "tre ottave sopra deve essere spento");
        }

        beginTest ("passa-alto: specchio del passa-basso");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::highPass);
            filter.setNumStages (1);
            filter.setResonance (juce::MathConstants<float>::sqrt2 / 2.0f);
            filter.setCutoffHz (1000.0f);

            expect (filterGainAt (filter, 100.0f, sampleRate) < 0.1f);
            expect (filterGainAt (filter, 8000.0f, sampleRate) > 0.95f);
        }

        beginTest ("24 dB taglia piu ripido di 12 dB");
        {
            dsp::StateVariableFilter gentle, steep;
            for (auto* f : { &gentle, &steep })
            {
                f->prepare (sampleRate);
                f->setType (dsp::StateVariableFilter::Type::lowPass);
                f->setResonance (juce::MathConstants<float>::sqrt2 / 2.0f);
                f->setCutoffHz (1000.0f);
            }
            gentle.setNumStages (1);
            steep.setNumStages (2);

            expect (filterGainAt (steep, 4000.0f, sampleRate) < filterGainAt (gentle, 4000.0f, sampleRate) * 0.5f);
        }

        beginTest ("risonanza alta su tutto il range: nessun NaN, nessuna esplosione");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (2);
            filter.setResonance (20.0f);

            for (float cutoff : { 20.0f, 200.0f, 2000.0f, 19000.0f, 40000.0f })
            {
                filter.setCutoffHz (cutoff);
                filter.reset();

                float peak = 0.0f;
                for (int i = 0; i < 48000; ++i)
                {
                    const auto out = filter.processSample (i == 0 ? 1.0f : 0.0f);
                    expect (std::isfinite (out), "uscita non finita a cutoff " + juce::String (cutoff));
                    peak = juce::jmax (peak, std::abs (out));
                }

                expect (peak < 100.0f, "picco " + juce::String (peak) + " a cutoff " + juce::String (cutoff));
            }
        }

        beginTest ("switching stages back on does not resurrect old state");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setResonance (12.0f); // alta risonanza: lo stadio 2 accumula parecchia energia
            filter.setCutoffHz (300.0f);
            filter.setNumStages (2);

            // Fa girare un segnale attraverso due stadi cosi' lo stadio 2 accumula stato.
            double phase = 0.0;
            const auto increment = 2.0 * juce::MathConstants<double>::pi * 300.0 / sampleRate;
            for (int i = 0; i < 4800; ++i, phase += increment)
                filter.processSample ((float) std::sin (phase));

            // Passa a un solo stadio: lo stadio 2 smette di essere processato ma il suo stato resta.
            filter.setNumStages (1);

            // Silenzio con un solo stadio attivo, abbastanza a lungo da lasciare che anche lo
            // stadio 1 (che a questa risonanza continua a squillare) si spenga completamente:
            // quello che sopravvive dopo e' solo lo stato congelato dello stadio 2, non ancora
            // riattivato.
            for (int i = 0; i < 20000; ++i)
                filter.processSample (0.0f);

            // Riaccende lo stadio 2: se non e' stato azzerato, la vecchia energia rientra nel segnale.
            filter.setNumStages (2);

            float peak = 0.0f;
            for (int i = 0; i < 480; ++i)
                peak = juce::jmax (peak, std::abs (filter.processSample (0.0f)));

            expect (peak < 1.0e-4f, "picco dopo il riavvio dello stadio 2 " + juce::String (peak));
        }
    }
};

static StateVariableFilterTests stateVariableFilterTests;
