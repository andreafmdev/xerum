#include "dsp/ADSREnvelope.h"

#include <juce_core/juce_core.h>

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
