#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace state
{
namespace ids
{
    inline const juce::Identifier MODS { "MODS" }, MOD { "MOD" }, src { "src" }, target { "target" }, depth { "depth" };
    inline const juce::Identifier ARP { "ARP" }, steps { "steps" }, version { "version" };

    /** Sulla radice dello stato salvato, non dentro un figlio: vedi kStateVersion. */
    inline const juce::Identifier schemaVersion { "schemaVersion" };
}

/** La versione del payload MODS/ARP scambiato con la WebUI (proprieta' `version` del figlio MODS
    e campo `version` di toVar). Non e' kStateVersion: vedi sotto. */
inline constexpr int kModsPayloadVersion = 1;
inline constexpr int kArpSteps = 16;

/** Il pattern di fabbrica del nodo ARP: gli stessi 16 valori con cui ensureChildren() crea ARP la
    prima volta (stato nuovo o preset vecchio) e con cui Source/bridge/StateChannel.cpp,
    resetNonParametricState(), riporta l'arp al default per il comando "Reset to default state".
    Un'unica definizione: prima erano due letterali C++ separati che il compilatore non teneva
    allineati, solo un commento lo chiedeva. */
inline constexpr double kDefaultArpSteps[kArpSteps] = { 0.8, 0.0, 0.6, 0.9, 0.0, 0.7, 0.0, 0.5, 0.8, 0.0, 0.6, 0.0, 0.9, 0.4, 0.0, 0.7 };

/**
 * La versione del *formato* dello stato salvato, scritta sulla radice da
 * XerumAudioProcessor::getStateInformation e riletta da setStateInformation.
 *
 * Non e' la versione del plugin e non e' kModsPayloadVersion, che descrive il solo figlio MODS: e'
 * il numero da cui una migrazione futura puo' ramificare, e l'unico posto dove puo' stare e' la
 * radice — dentro un figlio non serve a niente, perche' per leggerlo bisogna gia' sapere che
 * quel figlio esiste e come si chiama. Uno stato salvato *senza* questo attributo (cioe' tutto
 * quello che esiste oggi, scritto prima che questa costante esistesse) vale 1: vedi
 * schemaVersionOf().
 *
 * CHANGELOG DEL FORMATO — una riga per revisione, in ordine. Serve a non dover ricostruire la
 * storia dal git log alla terza migrazione, ed e' l'unica parte di questo lavoro che
 * retroattivamente non si puo' fare.
 *
 *   1 — formato iniziale. Radice PARAMS con un figlio PARAM per parametro (APVTS), piu' i figli
 *       non parametrici MODS (con la propria proprieta' `version`) e ARP. Nessun attributo
 *       `schemaVersion` sulla radice: gli stati scritti prima di questa costante ricadono qui.
 *   2 — (libero) la prossima revisione va descritta qui, con cosa cambia e cosa deve fare la
 *       migrazione da 1.
 */
inline constexpr int kStateVersion = 1;

/**
 * La versione di formato di uno stato letto da disco. Assente = 1: uno stato scritto prima che
 * l'attributo esistesse e' per definizione un formato 1, non uno stato da rifiutare.
 */
inline int schemaVersionOf (const juce::ValueTree& root)
{
    return root.hasProperty (ids::schemaVersion) ? (int) root[ids::schemaVersion] : 1;
}

/** Marca lo stato col formato corrente, subito prima di serializzarlo. */
inline void stampSchemaVersion (juce::ValueTree& root)
{
    root.setProperty (ids::schemaVersion, kStateVersion, nullptr);
}

/** Crea MODS e ARP se mancano (stato nuovo o preset vecchio). */
inline void ensureChildren (juce::ValueTree& root)
{
    if (! root.getChildWithName (ids::MODS).isValid())
    {
        juce::ValueTree mods { ids::MODS };
        mods.setProperty (ids::version, kModsPayloadVersion, nullptr);
        root.appendChild (mods, nullptr);
    }

    if (! root.getChildWithName (ids::ARP).isValid())
    {
        juce::ValueTree arp { ids::ARP };
        juce::StringArray defaultTokens;
        for (double v : kDefaultArpSteps)
            defaultTokens.add (juce::String (v));
        arp.setProperty (ids::steps, defaultTokens.joinIntoString (","), nullptr);
        root.appendChild (arp, nullptr);
    }
}

/** Serializza lo stato non parametrico nel payload atteso dalla WebUI. */
inline juce::var toVar (const juce::ValueTree& root, const juce::String& origin)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("version", kModsPayloadVersion);
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
    // Payload non conforme (JSON rotto, UI più vecchia): lo stato resta com'è invece di essere azzerato.
    if (! mods.isArray())
        return;

    auto tree = root.getChildWithName (ids::MODS);
    tree.removeAllChildren (undo);

    for (const auto& m : *mods.getArray())
    {
        juce::ValueTree node { ids::MOD };
        node.setProperty (ids::src, m["src"], undo);
        node.setProperty (ids::target, m["target"], undo);
        node.setProperty (ids::depth, (double) m["depth"], undo);
        tree.appendChild (node, undo);
    }
}

/** Sostituisce l'intera sequenza dell'arp: i valori sono salvati come stringa CSV. */
inline void setArpSteps (juce::ValueTree& root, const juce::var& steps, juce::UndoManager* undo)
{
    // Payload non conforme: meglio lasciare la sequenza com'è che sostituirla con una stringa vuota.
    if (! steps.isArray())
        return;

    juce::StringArray tokens;

    // Scartiamo l'eccesso già in scrittura: nello stato non finiscono mai più di kArpSteps valori.
    for (const auto& s : *steps.getArray())
    {
        if (tokens.size() >= kArpSteps)
            break;

        tokens.add (juce::String ((double) s, 3));
    }

    root.getChildWithName (ids::ARP).setProperty (ids::steps, tokens.joinIntoString (","), undo);
}
} // namespace state
