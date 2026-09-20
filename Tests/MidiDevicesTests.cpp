#include "bridge/MidiDevices.h"

#include <juce_core/juce_core.h>

/** Il contratto JSON del selettore MIDI della barra (WebUI/src/juce/backend.ts, MidiInputs). */
struct MidiDevicesTests final : public juce::UnitTest
{
    MidiDevicesTests() : juce::UnitTest ("bridge: midi inputs payload", "bridge") {}

    void runTest() override
    {
        beginTest ("nel plugin: host vero e nessun device");
        {
            const auto v = bridge::midiInputsToVar (true, {});
            expect ((bool) v["host"]);
            expect (v["devices"].isArray() && v["devices"].getArray()->isEmpty());
        }

        beginTest ("nello Standalone: un elemento per device con id, nome e stato");
        {
            const auto v = bridge::midiInputsToVar (false, { { "id-a", "Keystation", true }, { "id-b", "Launchkey", false } });
            expect (! (bool) v["host"]);
            const auto* list = v["devices"].getArray();
            expect (list != nullptr && list->size() == 2);
            expectEquals ((*list)[0]["id"].toString(), juce::String ("id-a"));
            expectEquals ((*list)[0]["name"].toString(), juce::String ("Keystation"));
            expect ((bool) (*list)[0]["enabled"]);
            expect (! (bool) (*list)[1]["enabled"]);
        }
    }
};

static MidiDevicesTests midiDevicesTests;
