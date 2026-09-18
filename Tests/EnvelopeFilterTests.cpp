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

        beginTest ("l'attacco arriva a 1 entro il tempo dichiarato, non prima");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            const auto halfway = runFor (env, 2400);  // 50 ms
            expect (halfway < 0.99f, "a metà attacco non deve essere già finito");

            const auto atEnd = runFor (env, 2400);    // 100 ms in tutto
            expect (atEnd >= 0.99f, "livello a fine attacco " + juce::String (atEnd));
        }

        beginTest ("il decay scende al sustain e ci resta");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.4f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            runFor (env, 48);                          // attacco
            const auto afterDecay = runFor (env, 2400); // 50 ms
            expectWithinAbsoluteError (afterDecay, 0.4f, 0.02f);

            const auto later = runFor (env, 48000);     // un secondo di sustain
            expectWithinAbsoluteError (later, 0.4f, 0.001f);
        }

        beginTest ("il release scende sotto -80 dB e l'inviluppo si spegne");
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
            expect (env.isActive(), "durante il release la voce è ancora viva");

            const auto tail = runFor (env, 2400); // 50 ms
            expect (tail < 1.0e-4f, "coda " + juce::String (tail));
            expect (! env.isActive(), "a fine release la voce va liberata");
        }

        beginTest ("la velocity scala il picco quando envVel è attivo");
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

        beginTest ("reset azzera tutto");
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
