#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace bridge
{
/** Un ingresso MIDI del sistema, come lo vede la UI. */
struct MidiInputDevice
{
    juce::String id;
    juce::String name;
    bool enabled { false };
};

/**
 * Il payload di getMidiInputs() e dell'evento "midiInputsChanged": `{ host, devices: [{ id, name,
 * enabled }] }`. `host` vero significa "il MIDI arriva dall'host" (VST3/AU): la lista e' vuota e la
 * UI mostra la scritta, non un selettore. Funzione libera e header-only perche' sia testabile
 * senza WebView (Tests/MidiDevicesTests.cpp).
 */
inline juce::var midiInputsToVar (bool host, const std::vector<MidiInputDevice>& devices)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("host", host);

    juce::Array<juce::var> list;

    for (const auto& d : devices)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", d.id);
        o->setProperty ("name", d.name);
        o->setProperty ("enabled", d.enabled);
        list.add (juce::var (o));
    }

    obj->setProperty ("devices", list);
    return juce::var (obj);
}
} // namespace bridge
