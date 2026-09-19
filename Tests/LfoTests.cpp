#include "dsp/Lfo.h"

#include <juce_core/juce_core.h>

#include <cmath>

struct LfoTests final : juce::UnitTest
{
    LfoTests() : juce::UnitTest ("Lfo", "dsp") {}

    void runTest() override
    {
        beginTest ("le forme d'onda coincidono con lfoShape di WebUI/src/synth/mod.ts");
        {
            // Stessi valori attesi scritti anche in WebUI/src/synth/mod.test.ts: se una delle due
            // implementazioni cambia, uno dei due test si accorge della divergenza.
            const float phases[] = { 0.0f, 0.125f, 0.25f, 0.5f, 0.75f, 0.999f };

            for (auto ph : phases)
            {
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::sine, ph),
                                           std::sin (ph * 2.0f * juce::MathConstants<float>::pi), 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::triangle, ph),
                                           1.0f - 4.0f * std::abs (ph - 0.5f), 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::saw, ph),
                                           1.0f - 2.0f * ph, 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::square, ph),
                                           ph < 0.5f ? 1.0f : -1.0f, 1.0e-5f);
                expectWithinAbsoluteError (dsp::lfoShapeValue (dsp::Lfo::Shape::sampleHold, ph),
                                           std::sin (std::floor (ph * 8.0f) * 7.3f), 1.0e-5f);
            }
        }

        beginTest ("la fase avvolge e resta in -1..1 qualunque sia la frequenza");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::sine);

            for (float hz : { 0.05f, 1.0f, 20.0f, 1.0e6f, -5.0f })
            {
                lfo.setFrequencyHz (hz);
                lfo.retrigger (0.0f);

                for (int b = 0; b < 100; ++b)
                {
                    const auto v = lfo.advance (128);
                    expect (std::isfinite (v), "livello non finito");
                    expect (v >= -1.0001f && v <= 1.0001f, "livello fuori da -1..1");
                }
            }
        }

        beginTest ("a 1 Hz un ciclo dura esattamente un secondo");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::saw);
            lfo.setFrequencyHz (1.0f);
            lfo.retrigger (0.0f);

            // Saw parte da 1 e scende a -1: dopo mezzo secondo deve valere circa 0.
            lfo.advance (24000);
            expectWithinAbsoluteError (lfo.level(), 0.0f, 0.01f);

            lfo.advance (24000);
            expectWithinAbsoluteError (lfo.level(), 1.0f, 0.01f);
        }

        beginTest ("retrigger riparte dall'offset di fase richiesto");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::saw);
            lfo.setFrequencyHz (1.0f);

            lfo.retrigger (0.0f);
            expectWithinAbsoluteError (lfo.level(), 1.0f, 1.0e-4f);

            lfo.advance (12000);
            lfo.retrigger (0.5f); // meta' ciclo: saw vale -0
            expectWithinAbsoluteError (lfo.level(), 0.0f, 1.0e-4f);
        }

        beginTest ("la dissolvenza in entrata scala il livello dal note-on");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::square); // ampiezza costante 1: isola la sola fade
            lfo.setFrequencyHz (0.05f);             // periodo 20 s: resta nella prima meta'
            lfo.setFadeSeconds (1.0f);
            lfo.retrigger (0.0f);

            expectWithinAbsoluteError (lfo.level(), 0.0f, 1.0e-4f);

            lfo.advance (24000); // mezzo secondo
            expectWithinAbsoluteError (lfo.level(), 0.5f, 0.02f);

            lfo.advance (24000); // un secondo: fade completa
            expectWithinAbsoluteError (lfo.level(), 1.0f, 0.02f);

            lfo.advance (48000); // e non supera 1
            expectWithinAbsoluteError (lfo.level(), 1.0f, 0.02f);
        }

        beginTest ("fade a zero secondi significa nessuna dissolvenza");
        {
            dsp::Lfo lfo;
            lfo.prepare (48000.0);
            lfo.setShape (dsp::Lfo::Shape::square);
            lfo.setFrequencyHz (0.05f);
            lfo.setFadeSeconds (0.0f);
            lfo.retrigger (0.0f);

            expectWithinAbsoluteError (lfo.level(), 1.0f, 1.0e-4f);
        }

        beginTest ("sync: le divisioni sono quelle di Tabs.tsx e seguono il tempo dell'host");
        {
            // ["1/16", "1/8", "1/4", "1/2", "1", "2"], indice min(5, floor(raw * 6)).
            // A 120 BPM un quarto dura mezzo secondo -> 2 Hz.
            expectWithinAbsoluteError (dsp::syncedRateHz (2.0f / 6.0f + 0.01f, 120.0), 2.0f, 1.0e-3f);
            // 1/16 = un quarto di quarto -> 8 Hz.
            expectWithinAbsoluteError (dsp::syncedRateHz (0.0f, 120.0), 8.0f, 1.0e-3f);
            // "2" = due battute da quattro quarti = 8 quarti -> 0.25 Hz.
            expectWithinAbsoluteError (dsp::syncedRateHz (1.0f, 120.0), 0.25f, 1.0e-3f);
            // Il tempo scala tutto linearmente.
            expectWithinAbsoluteError (dsp::syncedRateHz (0.0f, 60.0), 4.0f, 1.0e-3f);
        }
    }
};

static LfoTests lfoTests;
