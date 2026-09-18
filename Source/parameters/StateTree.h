#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace state
{
namespace ids
{
    inline const juce::Identifier MODS { "MODS" }, MOD { "MOD" }, src { "src" }, target { "target" }, depth { "depth" };
    inline const juce::Identifier ARP { "ARP" }, steps { "steps" }, version { "version" };
}

inline constexpr int kVersion = 1;
inline constexpr int kArpSteps = 16;

/** Crea MODS e ARP se mancano (stato nuovo o preset vecchio). */
inline void ensureChildren (juce::ValueTree& root)
{
    if (! root.getChildWithName (ids::MODS).isValid())
    {
        juce::ValueTree mods { ids::MODS };
        mods.setProperty (ids::version, kVersion, nullptr);
        root.appendChild (mods, nullptr);
    }

    if (! root.getChildWithName (ids::ARP).isValid())
    {
        juce::ValueTree arp { ids::ARP };
        arp.setProperty (ids::steps, "0.8,0,0.6,0.9,0,0.7,0,0.5,0.8,0,0.6,0,0.9,0.4,0,0.7", nullptr);
        root.appendChild (arp, nullptr);
    }
}

/** Serializza lo stato non parametrico nel payload atteso dalla WebUI. */
inline juce::var toVar (const juce::ValueTree& root, const juce::String& origin)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("version", kVersion);
    obj->setProperty ("origin", origin);

    juce::Array<juce::var> mods;

    for (const auto& m : root.getChildWithName (ids::MODS))
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("src", m[ids::src]);
        o->setProperty ("target", m[ids::target]);
        o->setProperty ("depth", (double) m[ids::depth]);
        mods.add (juce::var (o));
    }

    obj->setProperty ("mods", mods);

    juce::Array<juce::var> steps;

    // Il contratto con la UI è esattamente kArpSteps valori: tronchiamo l'eccesso e completiamo il resto.
    for (const auto& s : juce::StringArray::fromTokens (root.getChildWithName (ids::ARP)[ids::steps].toString(), ",", ""))
    {
        if (steps.size() >= kArpSteps)
            break;

        steps.add (s.getDoubleValue());
    }

    while (steps.size() < kArpSteps)
        steps.add (0.0);

    obj->setProperty ("arpSteps", steps);

    return juce::var (obj);
}

/** Sostituisce l'intera lista di mod: nessun diff, la UI manda sempre lo stato completo. */
inline void setMods (juce::ValueTree& root, const juce::var& mods, juce::UndoManager* undo)
{
    auto tree = root.getChildWithName (ids::MODS);
    tree.removeAllChildren (undo);

    if (auto* arr = mods.getArray())
    {
        for (const auto& m : *arr)
        {
            juce::ValueTree node { ids::MOD };
            node.setProperty (ids::src, m["src"], undo);
            node.setProperty (ids::target, m["target"], undo);
            node.setProperty (ids::depth, (double) m["depth"], undo);
            tree.appendChild (node, undo);
        }
    }
}

/** Sostituisce l'intera sequenza dell'arp: i valori sono salvati come stringa CSV. */
inline void setArpSteps (juce::ValueTree& root, const juce::var& steps, juce::UndoManager* undo)
{
    juce::StringArray tokens;

    // Scartiamo l'eccesso già in scrittura: nello stato non finiscono mai più di kArpSteps valori.
    if (auto* arr = steps.getArray())
        for (const auto& s : *arr)
        {
            if (tokens.size() >= kArpSteps)
                break;

            tokens.add (juce::String ((double) s, 3));
        }

    root.getChildWithName (ids::ARP).setProperty (ids::steps, tokens.joinIntoString (","), undo);
}
} // namespace state
