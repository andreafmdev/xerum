#include "EngineHarness.h"

#include "dsp/WavetableStore.h"
#include "engine/SynthEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace
{
/** La frequenza attesa per una nota MIDI spostata di `semitones`. */
float hzFor (int midiNote, float semitones) noexcept
{
    return 440.0f * std::pow (2.0f, ((float) midiNote + semitones - 69.0f) / 12.0f);
}

/** Valore grezzo di pitch wheel per una posizione -1..1. 8192 e' il centro. */
int wheelFor (float position) noexcept
{
    return juce::jlimit (0, 16383, 8192 + juce::roundToInt (position * 8192.0f));
}
} // namespace

struct PitchBendTests final : juce::UnitTest
{
    PitchBendTests() : juce::UnitTest ("pitch bend", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (0); // altrimenti active() resta nullptr e il motore rende silenzio

        beginTest ("senza bend la nota suona alla sua altezza");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            synth.setParams (harness::defaultParams());

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            const auto hz = harness::measureFundamentalHz (synth, 48000.0, 4800, 24000);
            expectWithinAbsoluteError (hz, 440.0f, 3.0f);
        }

        beginTest ("bend a fondo corsa sposta di pbRange semitoni");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            auto p = harness::defaultParams();
            p.pitchBendRangeSemitones = 2;
            synth.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheelFor (1.0f)), 1);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            // setParams() riscrive params_ a ogni blocco: il bend deve sopravvivere, come modWheel.
            synth.setParams (p);
            const auto hz = harness::measureFundamentalHz (synth, 48000.0, 4800, 24000);
            expectWithinAbsoluteError (hz, hzFor (69, 2.0f), 4.0f);
        }

        beginTest ("bend verso il basso e ritorno al centro");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            auto p = harness::defaultParams();
            p.pitchBendRangeSemitones = 12;
            synth.setParams (p);

            juce::MidiBuffer down;
            down.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            down.addEvent (juce::MidiMessage::pitchWheel (1, wheelFor (-1.0f)), 1);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, down);
            synth.setParams (p);
            expectWithinAbsoluteError (harness::measureFundamentalHz (synth, 48000.0, 4800, 24000),
                                       hzFor (69, -12.0f), 3.0f);

            juce::MidiBuffer centre;
            centre.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
            buffer.clear();
            synth.process (buffer, centre);
            synth.setParams (p);
            expectWithinAbsoluteError (harness::measureFundamentalHz (synth, 48000.0, 4800, 24000),
                                       440.0f, 3.0f);
        }

        beginTest ("con pbRange a zero la rotella non fa niente");
        {
            engine::SynthEngine synth;
            harness::prepareEngine (synth, store);
            auto p = harness::defaultParams();
            p.pitchBendRangeSemitones = 0;
            synth.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, wheelFor (1.0f)), 1);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);
            synth.setParams (p);

            expectWithinAbsoluteError (harness::measureFundamentalHz (synth, 48000.0, 4800, 24000),
                                       440.0f, 3.0f);
        }
    }
};

static PitchBendTests pitchBendTests;
