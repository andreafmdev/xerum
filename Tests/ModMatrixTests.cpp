#include "engine/ModMatrix.h"
#include "state/StateToEngine.h"
#include "state/StateTree.h"

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <tuple>
#include <vector>

namespace
{
/** Costruisce un nodo MODS con le assegnazioni date, come farebbe state::setMods. */
juce::ValueTree makeMods (const std::vector<std::tuple<juce::String, juce::String, double>>& routes)
{
    juce::ValueTree mods { state::ids::MODS };

    for (const auto& [src, target, depth] : routes)
    {
        juce::ValueTree node { state::ids::MOD };
        node.setProperty (state::ids::src, src, nullptr);
        node.setProperty (state::ids::target, target, nullptr);
        node.setProperty (state::ids::depth, depth, nullptr);
        mods.appendChild (node, nullptr);
    }

    return mods;
}
} // namespace

struct ModMatrixTests final : juce::UnitTest
{
    ModMatrixTests() : juce::UnitTest ("ModMatrix", "engine") {}

    void runTest() override
    {
        beginTest ("una route valida finisce nello snapshot con il target risolto");
        {
            engine::ModSnapshot snap;
            state::buildModSnapshot (makeMods ({ { "env", "cutoff", 0.75 } }), snap);

            expectEquals (snap.count, 1);
            expect (snap.routes[0].src == engine::ModSource::env);
            expectEquals (snap.routes[0].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::cutoff));
            expectWithinAbsoluteError (snap.routes[0].depth, 0.75f, 1.0e-6f);
        }

        beginTest ("un target che il motore non sa modulare viene scartato");
        {
            engine::ModSnapshot snap;
            // `att` e' un parametro vero ma non e' fra i sette target modulabili.
            state::buildModSnapshot (makeMods ({ { "lfo", "att", 1.0 },
                                                  { "lfo", "wtpos", 0.5 } }), snap);

            expectEquals (snap.count, 1);
            expectEquals (snap.routes[0].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::wtpos));
        }

        beginTest ("env2 e' una sorgente distinta da env, non un prefisso");
        {
            engine::ModSnapshot snap;
            state::buildModSnapshot (makeMods ({ { "env", "cutoff", 0.5 },
                                                  { "env2", "res", 0.25 } }), snap);

            expectEquals (snap.count, 2);
            expect (snap.routes[0].src == engine::ModSource::env);
            expect (snap.routes[1].src == engine::ModSource::env2, "\"env2\" non e' stata riconosciuta");
            expectEquals (snap.routes[1].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::res));
        }

        beginTest ("una sorgente sconosciuta viene scartata");
        {
            engine::ModSnapshot snap;
            state::buildModSnapshot (makeMods ({ { "aftertouch", "cutoff", 1.0 } }), snap);
            expectEquals (snap.count, 0);
        }

        beginTest ("oltre kMaxRoutes le assegnazioni in eccesso si scartano senza crescere");
        {
            std::vector<std::tuple<juce::String, juce::String, double>> many;
            for (int i = 0; i < engine::ModSnapshot::kMaxRoutes + 5; ++i)
                many.emplace_back ("lfo", "cutoff", 0.1);

            engine::ModSnapshot snap;
            state::buildModSnapshot (makeMods (many), snap);
            expectEquals (snap.count, engine::ModSnapshot::kMaxRoutes);
        }

        beginTest ("depth fuori scala viene limitato a -1..1");
        {
            engine::ModSnapshot snap;
            state::buildModSnapshot (makeMods ({ { "vel", "level", 4.2 },
                                                  { "vel", "pan", -9.0 } }), snap);

            expectEquals (snap.count, 2);
            expectWithinAbsoluteError (snap.routes[0].depth, 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (snap.routes[1].depth, -1.0f, 1.0e-6f);
        }

        beginTest ("un nodo MODS vuoto produce uno snapshot vuoto");
        {
            engine::ModSnapshot snap;
            snap.count = 7; // sporco di proposito: buildModSnapshot deve azzerarlo
            state::buildModSnapshot (makeMods ({}), snap);
            expectEquals (snap.count, 0);
        }
    }
};

static ModMatrixTests modMatrixTests;
