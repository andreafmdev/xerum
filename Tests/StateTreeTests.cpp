#include "state/StateTree.h"

#include <juce_core/juce_core.h>

/**
 * Il formato dello stato non parametrico (MODS e ARP) e il payload verso la WebUI. Header-only e
 * senza dipendenze GUI, quindi testabile qui: prima non lo era, e l'unico controllo del
 * round-trip setMods -> toVar era aprire il plugin.
 */
struct StateTreeTests final : public juce::UnitTest
{
    StateTreeTests() : juce::UnitTest ("StateTree", "state") {}

    static juce::var modVar (const char* src, const char* target, double depth)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("src", src);
        o->setProperty ("target", target);
        o->setProperty ("depth", depth);
        return juce::var (o);
    }

    void runTest() override
    {
        beginTest ("ensureChildren crea MODS e ARP una volta sola, con il pattern di default");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);
            expect (root.getChildWithName (state::ids::MODS).isValid());
            expect (root.getChildWithName (state::ids::ARP).isValid());
            expectEquals ((int) root.getChildWithName (state::ids::MODS)[state::ids::version], state::kModsPayloadVersion);

            state::ensureChildren (root);
            expectEquals (root.getNumChildren(), 2);
        }

        beginTest ("schemaVersionOf: assente vale 1, presente si legge");
        {
            juce::ValueTree root { "PARAMS" };
            expectEquals (state::schemaVersionOf (root), 1);
            state::stampSchemaVersion (root);
            expectEquals (state::schemaVersionOf (root), state::kStateVersion);
        }

        beginTest ("setMods -> toVar e' un round-trip");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);

            juce::Array<juce::var> mods;
            mods.add (modVar ("env", "cutoff", 0.75));
            mods.add (modVar ("lfo", "wtpos", -0.5));
            state::setMods (root, juce::var (mods), nullptr);

            const auto out = state::toVar (root, "me");
            expectEquals (out["origin"].toString(), juce::String ("me"));
            expectEquals ((int) out["version"], state::kModsPayloadVersion);

            const auto* list = out["mods"].getArray();
            expect (list != nullptr && list->size() == 2);
            expectEquals ((*list)[0]["src"].toString(), juce::String ("env"));
            expectEquals ((*list)[1]["target"].toString(), juce::String ("wtpos"));
            expectWithinAbsoluteError ((double) (*list)[1]["depth"], -0.5, 1.0e-9);
        }

        beginTest ("setMods con un payload non conforme lascia lo stato com'e'");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);
            juce::Array<juce::var> mods;
            mods.add (modVar ("env", "cutoff", 0.5));
            state::setMods (root, juce::var (mods), nullptr);

            state::setMods (root, juce::var ("rotto"), nullptr);
            expectEquals (root.getChildWithName (state::ids::MODS).getNumChildren(), 1);
        }

        beginTest ("setArpSteps tronca a kArpSteps e toVar completa a kArpSteps");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);

            juce::Array<juce::var> tooMany;
            for (int i = 0; i < state::kArpSteps + 4; ++i)
                tooMany.add (0.5);
            state::setArpSteps (root, juce::var (tooMany), nullptr);

            const auto csv = root.getChildWithName (state::ids::ARP)[state::ids::steps].toString();
            expectEquals (juce::StringArray::fromTokens (csv, ",", "").size(), state::kArpSteps);

            juce::Array<juce::var> few;
            few.add (0.8);
            few.add (0.0);
            state::setArpSteps (root, juce::var (few), nullptr);

            // `out` resta vivo: getArray() punta dentro il var, non a una copia.
            const auto out = state::toVar (root, "x");
            const auto* steps = out["arpSteps"].getArray();
            expect (steps != nullptr && steps->size() == state::kArpSteps);
            expectWithinAbsoluteError ((double) (*steps)[0], 0.8, 1.0e-9);
            expectWithinAbsoluteError ((double) (*steps)[state::kArpSteps - 1], 0.0, 1.0e-9);
        }
    }
};

static StateTreeTests stateTreeTests;
