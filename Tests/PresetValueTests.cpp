#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"
#include "parameters/PresetValue.h"

#include <juce_core/juce_core.h>

// Task finale, item 1: applyPreset() passava spec.def (grezzo) a setValueNotifyingHost()
// (che vuole un valore normalizzato 0..1), quindi ogni parametro Kind::Int assente da un
// preset (oct, semi: nessun preset li elenca) cadeva sul default di spec letto come se fosse
// gia' normalizzato — spec.def=0.0f per entrambi, che come normalizzato e' il minimo del
// range (oct=-3, semi=-12), non lo zero che spec.def rappresenta davvero. Questo file
// esercita params::presetValue (Source/parameters/PresetValue.h), la regola estratta da
// StateChannel::applyPreset perche' XerumTests non compila Source/bridge/*.
struct PresetValueTests final : juce::UnitTest
{
    PresetValueTests() : juce::UnitTest ("presetValue", "parameters") {}

    void runTest() override
    {
        beginTest ("un parametro elencato nel preset riceve il suo valore, non il default");
        {
            const auto* cutoffSpec = params::find ("cutoff");
            expect (cutoffSpec != nullptr, "spec di cutoff non trovata");

            // Preset 1 ("Sub Pulse") elenca esplicitamente cutoff a 0.35.
            const auto& preset = params::kPresetTable[1];
            expectWithinAbsoluteError (params::presetValue (preset, *cutoffSpec), 0.35f, 1.0e-6f);
        }

        beginTest ("un parametro impostato a 0 dal preset resta 0, non cade sul default");
        {
            // Preset 1 elenca "att" a 0.0f: se il fallback-al-default scattasse per errore
            // (es. un controllo "value != 0" invece che "id assente"), att tornerebbe al suo
            // default di spec (0.12), non a zero.
            const auto* attSpec = params::find ("att");
            expect (attSpec != nullptr, "spec di att non trovata");
            expect (attSpec->def != 0.0f, "att deve avere un default diverso da zero perche' il test sia significativo");

            const auto& preset = params::kPresetTable[1];
            expectWithinAbsoluteError (params::presetValue (preset, *attSpec), 0.0f, 1.0e-6f);
        }

        beginTest ("un parametro Int assente dal preset torna al default normalizzato, non al grezzo");
        {
            // Questo e' esattamente il caso che ha rotto ogni preset: nessun preset elenca
            // "oct" o "semi", ed entrambi sono Kind::Int con spec.def grezzo (0), non
            // normalizzato. Il vecchio codice passava 0.0f a setValueNotifyingHost() come se
            // fosse gia' normalizzato: per oct (range -3..3) 0.0f normalizzato e' il minimo,
            // cioe' oct=-3, non oct=0. Con questo helper deve tornare il centro del range.
            const auto* octSpec = params::find ("oct");
            expect (octSpec != nullptr, "spec di oct non trovata");
            expectEquals (octSpec->def, 0.0f, "il default grezzo di oct e' 0 in parameters.json");

            // Nessun preset elenca "oct": qualunque preset con valori va bene per il fallback.
            const auto& preset = params::kPresetTable[1];
            const auto value = params::presetValue (preset, *octSpec);

            // oct: min=-3, max=3, default grezzo 0 -> normalizzato atteso 0.5 (centro range).
            expectWithinAbsoluteError (value, 0.5f, 1.0e-6f);

            const auto* semiSpec = params::find ("semi");
            expect (semiSpec != nullptr, "spec di semi non trovata");
            const auto semiValue = params::presetValue (preset, *semiSpec);
            // semi: min=-12, max=12, default grezzo 0 -> normalizzato atteso 0.5.
            expectWithinAbsoluteError (semiValue, 0.5f, 1.0e-6f);
        }

        beginTest ("Init (preset senza valori): ogni parametro torna al suo default normalizzato");
        {
            const auto& initPreset = params::kPresetTable[0];
            expectEquals (initPreset.numValues, 0, "\"Init\" non deve elencare alcun valore");

            for (const auto& spec : params::kTable)
            {
                const auto value = params::presetValue (initPreset, spec);
                expect (value >= 0.0f && value <= 1.0f,
                        juce::String (spec.id) + ": il valore normalizzato deve stare in 0..1, e' " + juce::String (value));
            }

            // oct e semi in particolare devono finire al centro del range (default grezzo 0),
            // non al minimo: e' la regressione che questo file guarda piu' da vicino.
            const auto* octSpec = params::find ("oct");
            const auto* semiSpec = params::find ("semi");
            expectWithinAbsoluteError (params::presetValue (initPreset, *octSpec), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (params::presetValue (initPreset, *semiSpec), 0.5f, 1.0e-6f);
        }

        beginTest ("normalisedDefault: Choice e Bool coerenti con AudioParameter*::getDefaultValue()");
        {
            // slope: Choice, 2 opzioni, default grezzo indice 1 -> normalizzato 1.0.
            const auto* slopeSpec = params::find ("slope");
            expect (slopeSpec != nullptr);
            expectWithinAbsoluteError (params::normalisedDefault (*slopeSpec), 1.0f, 1.0e-6f);

            // wtIndex: Choice, 6 opzioni, default grezzo indice 0 -> normalizzato 0.0.
            const auto* wtIndexSpec = params::find ("wtIndex");
            expect (wtIndexSpec != nullptr);
            expectWithinAbsoluteError (params::normalisedDefault (*wtIndexSpec), 0.0f, 1.0e-6f);

            // oscOn: Bool, default true -> normalizzato 1.0.
            const auto* oscOnSpec = params::find ("oscOn");
            expect (oscOnSpec != nullptr);
            expectWithinAbsoluteError (params::normalisedDefault (*oscOnSpec), 1.0f, 1.0e-6f);

            // level: Float, gia' normalizzato in tabella (Map::Db su 0..1) -> invariato.
            const auto* levelSpec = params::find ("level");
            expect (levelSpec != nullptr);
            expectWithinAbsoluteError (params::normalisedDefault (*levelSpec), levelSpec->def, 1.0e-6f);
        }
    }
};

static PresetValueTests presetValueTests;
