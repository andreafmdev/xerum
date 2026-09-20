#include "EngineHarness.h"

#include "dsp/StereoDelay.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

/** Il rack: tre effetti nell'ordine scelto, con il delay che a riposo non cambia un bit. */
struct FxRackTests final : public juce::UnitTest
{
    FxRackTests() : juce::UnitTest ("FX rack", "engine") {}

    static engine::EngineParams fxParams (bool chorus, bool delay, bool reverb, engine::FxOrder order)
    {
        auto p = harness::baseParams();
        p.filterOn = true;
        p.chorusOn = chorus; p.chorusRateHz = 1.6f; p.chorusDepth01 = 0.4f; p.chorusMix01 = 0.3f;
        p.reverbOn = reverb; p.reverbSize01 = 0.6f; p.reverbDecay01 = 0.5f; p.reverbDamp01 = 0.4f; p.reverbPredelaySeconds = 0.02f; p.reverbMix01 = 0.25f;
        p.delayOn = delay; p.delayTimeRaw = 0.5f; p.delaySync = false; p.delayFeedback01 = 0.5f; p.delayDamp01 = 0.5f; p.delayMix01 = 0.5f;
        p.fxOrder = order;
        return p;
    }

    static float rmsDiff (const harness::Rendered& a, const harness::Rendered& b)
    {
        double s = 0.0;
        for (size_t i = 0; i < a.left.size(); ++i) { const auto d = (double) a.left[i] - b.left[i]; s += d * d; }
        return (float) std::sqrt (s / (double) a.left.size());
    }

    void runTest() override
    {
        dsp::WavetableStore store; store.setActive (1);

        beginTest ("con il delay spento l'ordine non cambia un bit: cdr e crd sono identici");
        {
            for (const bool reverb : { false, true })
            {
                auto cdr = fxParams (true, false, reverb, engine::FxOrder::chorusDelayReverb);
                auto crd = fxParams (true, false, reverb, engine::FxOrder::chorusReverbDelay);
                // Tutti i parametri del delay agli estremi: se una riga del suo ramo girasse, si vedrebbe.
                for (auto* p : { &cdr, &crd }) { p->delayFeedback01 = 1.0f; p->delayMix01 = 1.0f; p->delayTimeRaw = 1.0f; }
                const auto a = harness::renderHeldNote (cdr, store, 60);
                const auto b = harness::renderHeldNote (crd, store, 60);
                int mismatches = 0;
                for (size_t i = 0; i < a.left.size(); ++i) mismatches += (a.left[i] != b.left[i] || a.right[i] != b.right[i]) ? 1 : 0;
                expectEquals (mismatches, 0, "reverb " + juce::String ((int) reverb));
            }
        }

        beginTest ("il delay acceso cambia il suono, e dcr suona diverso da cdr");
        {
            const auto off = harness::renderHeldNote (fxParams (true, false, true, engine::FxOrder::chorusDelayReverb), store, 60);
            const auto cdr = harness::renderHeldNote (fxParams (true, true, true, engine::FxOrder::chorusDelayReverb), store, 60);
            const auto dcr = harness::renderHeldNote (fxParams (true, true, true, engine::FxOrder::delayChorusReverb), store, 60);
            expect (rmsDiff (off, cdr) > 1.0e-3f, "il delay non si sente");
            expect (rmsDiff (cdr, dcr) > 1.0e-4f, "l'ordine non cambia niente");
        }

        beginTest ("con il solo delay acceso lo stadio si spegne dopo la coda");
        {
            engine::SynthEngine synth; harness::prepareEngine (synth, store);
            auto p = fxParams (false, true, false, engine::FxOrder::chorusDelayReverb);
            p.delayMix01 = 1.0f; p.delayFeedback01 = 0.3f;   // 27 %: coda ~ 7 ripetizioni di 44 ms
            synth.setParams (p); synth.setMasterGainLinear (1.0f);
            juce::MidiBuffer midi; midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            for (int b = 0; b < 100; ++b) { juce::AudioBuffer<float> buf (2, harness::kBlock); buf.clear(); synth.process (buf, midi); midi.clear(); }
            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            float late = 0.0f;
            const auto blocks = (int) std::ceil (4.0 * harness::kSampleRate / harness::kBlock);
            const auto lateFrom = (int) std::ceil (3.0 * harness::kSampleRate / harness::kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buf (2, harness::kBlock); buf.clear(); synth.process (buf, midi); midi.clear();
                if (b >= lateFrom) for (int i = 0; i < harness::kBlock; ++i) late = juce::jmax (late, std::abs (buf.getSample (0, i)));
            }
            expect (late < 1.0e-4f, "coda " + juce::String (late) + ": il delay non si spegne mai");
        }

        beginTest ("il tempo sincronizzato segue il bpm del blocco");
        {
            // 1/4 a 120 bpm = 0.5 s: il primo eco di una nota staccata arriva dopo mezzo secondo.
            engine::SynthEngine synth; harness::prepareEngine (synth, store);
            auto p = fxParams (false, true, false, engine::FxOrder::chorusDelayReverb);
            p.delaySync = true; p.delayTimeRaw = 0.4f; p.bpm = 120.0f; p.delayMix01 = 1.0f; p.delayFeedback01 = 0.0f;
            p.attackSeconds = 0.001f; p.releaseSeconds = 0.01f;
            synth.setParams (p); synth.setMasterGainLinear (1.0f);
            juce::MidiBuffer midi; midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0); midi.addEvent (juce::MidiMessage::noteOff (1, 60), 64);
            std::vector<float> out;
            for (int b = 0; b < 400; ++b) { juce::AudioBuffer<float> buf (2, harness::kBlock); buf.clear(); synth.process (buf, midi); midi.clear(); out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + harness::kBlock); }
            // Silenzio fra la nota (finita entro ~0.05 s) e l'eco a 0.5 s; poi qualcosa.
            float quiet = 0.0f, echo = 0.0f;
            for (int i = 12000; i < 23000; ++i) quiet = juce::jmax (quiet, std::abs (out[(size_t) i]));
            for (int i = 24000; i < 30000; ++i) echo = juce::jmax (echo, std::abs (out[(size_t) i]));
            expect (quiet < 1.0e-3f, "qualcosa suona prima dell'eco: " + juce::String (quiet));
            expect (echo > 0.01f, "nessun eco a mezzo secondo");
        }
    }
};

static FxRackTests fxRackTests;
