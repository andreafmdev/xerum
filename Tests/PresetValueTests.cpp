#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"
#include "parameters/PresetValue.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <iterator>

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
        // I tre casi qui sotto usano preset costruiti a mano invece di pescare dalla tabella
        // generata: la regola da esercitare e' presetValue(), non il contenuto di presets.json.
        // Legandoli a un preset reale (com'era prima) bastava ritarare un preset per far
        // fallire test che con quel preset non c'entrano niente.
        beginTest ("un parametro elencato nel preset riceve il suo valore, non il default");
        {
            const auto* cutoffSpec = params::find ("cutoff");
            expect (cutoffSpec != nullptr, "spec di cutoff non trovata");
            expect (std::abs (cutoffSpec->def - 0.35f) > 1.0e-6f,
                    "il test e' significativo solo se 0.35 non e' gia' il default di cutoff");

            static constexpr params::PresetValue values[] = { { "cutoff", 0.35f } };
            const params::Preset preset { "test", "test", values, (int) std::size (values) };

            expectWithinAbsoluteError (params::presetValue (preset, *cutoffSpec), 0.35f, 1.0e-6f);
        }

        beginTest ("un parametro impostato a 0 dal preset resta 0, non cade sul default");
        {
            // Se il fallback-al-default scattasse per errore (es. un controllo "value != 0"
            // invece che "id assente"), att tornerebbe al suo default di spec, non a zero.
            const auto* attSpec = params::find ("att");
            expect (attSpec != nullptr, "spec di att non trovata");
            expect (attSpec->def != 0.0f, "att deve avere un default diverso da zero perche' il test sia significativo");

            static constexpr params::PresetValue values[] = { { "att", 0.0f } };
            const params::Preset preset { "test", "test", values, (int) std::size (values) };

            expectWithinAbsoluteError (params::presetValue (preset, *attSpec), 0.0f, 1.0e-6f);
        }

        beginTest ("un parametro Int assente dal preset torna al default normalizzato, non al grezzo");
        {
            // Questo e' esattamente il caso che ha rotto ogni preset: "oct" e "semi" sono
            // Kind::Int con spec.def grezzo (0), non normalizzato. Il vecchio codice passava
            // 0.0f a setValueNotifyingHost() come se fosse gia' normalizzato: per oct
            // (range -3..3) 0.0f normalizzato e' il minimo, cioe' oct=-3, non oct=0.
            const auto* octSpec = params::find ("oct");
            const auto* semiSpec = params::find ("semi");
            expect (octSpec != nullptr && semiSpec != nullptr, "spec di oct/semi non trovate");
            expectEquals (octSpec->def, 0.0f, "il default grezzo di oct e' 0 in parameters.json");

            // Un preset che non elenca ne' oct ne' semi.
            static constexpr params::PresetValue values[] = { { "cutoff", 0.5f } };
            const params::Preset preset { "test", "test", values, (int) std::size (values) };

            // oct: min=-3, max=3, default grezzo 0 -> normalizzato atteso 0.5 (centro range).
            expectWithinAbsoluteError (params::presetValue (preset, *octSpec), 0.5f, 1.0e-6f);
            // semi: min=-12, max=12, default grezzo 0 -> normalizzato atteso 0.5.
            expectWithinAbsoluteError (params::presetValue (preset, *semiSpec), 0.5f, 1.0e-6f);
        }

        beginTest ("ogni preset di presets.json produce valori normalizzati validi");
        {
            // Il controllo che prima esisteva solo per "Init": ogni preset, per ogni
            // parametro, deve dare un valore in 0..1 — un preset con un valore fuori range
            // (o con un id che nessuno spec riconosce, quindi silenziosamente ignorato)
            // arriverebbe fino all'APVTS.
            for (int i = 0; i < params::kNumPresets; ++i)
            {
                const auto& preset = params::kPresetTable[i];

                for (const auto& spec : params::kTable)
                {
                    const auto value = params::presetValue (preset, spec);
                    expect (value >= 0.0f && value <= 1.0f,
                            juce::String (preset.name) + " / " + juce::String (spec.id)
                                + ": valore normalizzato fuori da 0..1, e' " + juce::String (value));
                }
            }
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

        beginTest ("nessun preset di fabbrica elenca env2: i quattro cadono sul loro default");
        {
            // I dodici preset sono stati scritti prima che il secondo inviluppo esistesse.
            // Devono restare cosi': se uno di loro cominciasse a elencare att2/dec2/sus2/rel2
            // cambierebbe suono rispetto a com'e' oggi, e questo test lo direbbe. La verifica e'
            // doppia — che l'id non compaia, e che presetValue() restituisca esattamente il
            // default normalizzato dello spec — perche' la prima da sola non prova che il
            // fallback funzioni e la seconda da sola passerebbe anche con un preset che elenca
            // per caso proprio il default.
            for (const char* id : { "att2", "dec2", "sus2", "rel2" })
            {
                const auto* spec = params::find (id);
                expect (spec != nullptr, juce::String (id) + ": spec non trovata");

                for (int i = 0; i < params::kNumPresets; ++i)
                {
                    const auto& preset = params::kPresetTable[i];

                    for (int v = 0; v < preset.numValues; ++v)
                        expect (juce::String (preset.values[v].id) != juce::String (id),
                                juce::String (preset.name) + " elenca " + id + ": non dovrebbe");

                    expectWithinAbsoluteError (params::presetValue (preset, *spec),
                                               params::normalisedDefault (*spec), 0.0f,
                                               juce::String (preset.name) + " / " + id
                                                   + ": non e' caduto sul default");
                }
            }
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
