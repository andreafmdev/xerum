/**
 * La giuntura fra i parametri veri dell'APVTS e il motore, e quella fra lo stato salvato e
 * l'APVTS.
 *
 * Tutti gli altri test di parametri esercitano params::collectEngineParams con un accessore
 * finto che restituisce valori normalizzati 0..1. E' comodo — non serve linkare l'APVTS — ma
 * lascia scoperta proprio la domanda che conta: *cosa restituisce davvero* getRawParameterValue
 * per ognuno dei quattro Kind. Un bug che vive in quella conversione e' invisibile a ogni altro
 * test della suite, ed e' esattamente li' che se n'e' annidato uno per mesi: `oct` e `semi` sono
 * AudioParameterInt, che vivono nel loro range naturale (-3..3 e -12..12), mentre
 * collectEngineParams li denormalizzava come se fossero 0..1 — quindi ai valori di default
 * (0 e 0) il motore trasponeva ogni nota di -3 ottave e -12 semitoni, quattro ottave sotto il
 * tasto premuto.
 *
 * Questo file costruisce il layout vero, legge i puntatori veri e verifica la conversione. Non
 * su otto parametri scelti a mano ma su tutti e 56, ciclando su params::kTable: i controlli
 * generali stanno nei tre cicli in fondo ("ogni parametro...", "andata e ritorno...",
 * "l'APVTS vero restituisce..."), quelli scritti a mano sopra restano perche' nominano il
 * sintomo — un test che dice "lo strumento non traspone" si legge, uno che dice
 * "kTable[3] != 0" no.
 */
#include "parameters/ParamCollect.h"
#include "parameters/ParameterLayout.h"
#include "parameters/ParameterTable.h"
#include "parameters/PresetValue.h"
#include "parameters/StateTree.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <vector>

namespace
{
/** Un AudioProcessor minimo: serve solo a possedere un APVTS costruito col layout vero. */
struct DummyProcessor final : juce::AudioProcessor
{
    DummyProcessor() : apvts (*this, nullptr, "PARAMS", params::createParameterLayout()) {}

    const juce::String getName() const override { return "Dummy"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

/**
 * Lo stesso accessore di PluginProcessor::collectParams, sui parametri veri — e risolto nello
 * stesso modo, ciclando su params::kSlotIds.
 *
 * Prima qui c'erano 28 righe di `slot[...] = byId[...]`: la terza copia a mano della mappatura
 * id -> slot, dopo ParamSlot.h e PluginProcessor.cpp. Un errore in quelle righe avrebbe prodotto
 * un test verde su un cablaggio sbagliato, perche' il test verificava il proprio cablaggio invece
 * di quello che spedisce. Ora la mappatura e' una sola, generata da parameters.json.
 */
struct RealAccessor
{
    explicit RealAccessor (DummyProcessor& p)
    {
        for (int i = 0; i < params::kNumSlots; ++i)
            slot[(size_t) i] = p.apvts.getRawParameterValue (params::kSlotIds[i]);
    }

    float operator() (params::ParamSlot s) const noexcept
    {
        const auto* p = slot[(size_t) s];
        return p != nullptr ? p->load (std::memory_order_relaxed) : 0.0f;
    }

    std::array<std::atomic<float>*, (size_t) params::ParamSlot::count> slot {};
};

/** Scrive un parametro nelle sue unita' naturali, come farebbe l'utente girando il knob. */
void setNatural (DummyProcessor& p, const char* id, float natural)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (natural));
}

/** "#12 lrate": prefisso di ogni messaggio dei cicli, perche' un fallimento dica *quale*. */
juce::String where (int index, const params::Spec& s)
{
    return "#" + juce::String (index) + " " + juce::String (s.id);
}

/**
 * I valori naturali con cui vale la pena sollecitare un parametro: *tutti* i valori discreti per
 * gli Int, i Choice e i Bool (7 per `oct`, 25 per `semi`), e cinque posizioni lungo la corsa per
 * i Float. Per gli Int e' la differenza fra un test che passa e uno che serve: il difetto delle
 * quattro ottave lasciava `oct` capace di produrre *solo* i due estremi, e provarne due soli non
 * lo vedrebbe.
 */
std::vector<float> naturalProbes (const params::Spec& s)
{
    std::vector<float> out;

    switch (s.kind)
    {
        case params::Kind::Int:
            for (int n = (int) s.min; n <= (int) s.max; ++n) out.push_back ((float) n);
            break;
        case params::Kind::Choice:
            for (int i = 0; i < s.numOptions; ++i) out.push_back ((float) i);
            break;
        case params::Kind::Bool:
            out = { 0.0f, 1.0f };
            break;
        case params::Kind::Float:
            for (const float raw : { 0.0f, 0.13f, 0.5f, 0.87f, 1.0f }) out.push_back (params::denormalise (s, raw));
            break;
    }

    return out;
}
} // namespace

struct ParameterSeamTests final : juce::UnitTest
{
    ParameterSeamTests() : juce::UnitTest ("giuntura APVTS -> motore", "params") {}

    void runTest() override
    {
        DummyProcessor processor;
        RealAccessor raw { processor };

        beginTest ("ai default di fabbrica lo strumento non traspone");
        {
            const auto p = params::collectEngineParams (raw);

            // Il difetto che questo test blocca: oct e semi sono AudioParameterInt, quindi
            // getRawParameterValue restituisce -3..3 e -12..12, non 0..1. Denormalizzarli
            // come float dava -3 ottave e -12 semitoni al valore di default (0 e 0): ogni
            // nota suonava quattro ottave sotto il tasto premuto.
            expectEquals (p.octave, 0, "ai default l'ottava deve essere 0");
            expectEquals (p.semitones, 0, "ai default i semitoni devono essere 0");
            expectWithinAbsoluteError (p.fineCents, 0.0f, 0.5f);
        }

        beginTest ("oct e semi coprono tutto il loro range, non solo gli estremi");
        {
            for (int oct = -3; oct <= 3; ++oct)
            {
                setNatural (processor, "oct", (float) oct);
                expectEquals (params::collectEngineParams (raw).octave, oct,
                              "oct naturale " + juce::String (oct));
            }
            setNatural (processor, "oct", 0.0f);

            for (int semi : { -12, -7, -1, 0, 1, 7, 12 })
            {
                setNatural (processor, "semi", (float) semi);
                expectEquals (params::collectEngineParams (raw).semitones, semi,
                              "semi naturale " + juce::String (semi));
            }
            setNatural (processor, "semi", 0.0f);
        }

        beginTest ("il range del pitch bend arriva in semitoni, non normalizzato");
        {
            // Stessa forma di oct e semi: AudioParameterInt, quindi getRawParameterValue
            // restituisce 0..24 e non 0..1. Denormalizzarlo come float darebbe 0 semitoni al
            // default, cioe' una rotella inerte, e 24 a fondo corsa qualunque cosa dica l'utente.
            expectEquals (params::collectEngineParams (raw).pitchBendRangeSemitones, 2,
                          "al default il range deve essere 2 semitoni");

            for (int semi : { 0, 1, 2, 7, 12, 24 })
            {
                setNatural (processor, "pbRange", (float) semi);
                expectEquals (params::collectEngineParams (raw).pitchBendRangeSemitones, semi,
                              "pbRange naturale " + juce::String (semi));
            }
            setNatural (processor, "pbRange", 2.0f);
        }

        beginTest ("i float restano normalizzati e si denormalizzano come sempre");
        {
            // I Kind::Float sono creati con NormalisableRange {0, 1}: naturale e normalizzato
            // coincidono, ed e' il motivo per cui il difetto di oct/semi era l'unico.
            setNatural (processor, "cutoff", 0.62f);
            expectWithinAbsoluteError (params::collectEngineParams (raw).cutoffHz,
                                       params::cutoffHzFromRaw (0.62f), 1.0f);

            setNatural (processor, "res", 0.3f);
            expectWithinAbsoluteError (params::collectEngineParams (raw).resonanceQ,
                                       params::resonanceQFromRaw (0.3f), 1.0e-3f);
        }

        beginTest ("i choice arrivano come indice");
        {
            setNatural (processor, "slope", 0.0f);
            expectEquals (params::collectEngineParams (raw).filterStages, 1);
            setNatural (processor, "slope", 1.0f);
            expectEquals (params::collectEngineParams (raw).filterStages, 2);

            setNatural (processor, "unison", 3.0f); // quarta opzione: "8"
            expectEquals (params::collectEngineParams (raw).unisonVoices, 8);
            setNatural (processor, "unison", 0.0f);
            expectEquals (params::collectEngineParams (raw).unisonVoices, 1);

            setNatural (processor, "ftype", 1.0f);
            expect (params::collectEngineParams (raw).filterType
                        == dsp::StateVariableFilter::Type::highPass);
            setNatural (processor, "ftype", 0.0f);
        }

        beginTest ("i bool arrivano come 0/1");
        {
            setNatural (processor, "filtOn", 0.0f);
            expect (! params::collectEngineParams (raw).filterOn);
            setNatural (processor, "filtOn", 1.0f);
            expect (params::collectEngineParams (raw).filterOn);
        }

        // --- da qui in giu': cicli su tutti e 56 i parametri, non su otto scelti a mano -----

        beginTest ("ogni slot e' cablato sul parametro giusto");
        {
            // La mappatura id -> slot e' generata, ma nessuno aveva ancora verificato che il
            // puntatore risolto per uno slot sia quello del parametro che lo slot nomina.
            for (int i = 0; i < params::kNumSlots; ++i)
            {
                const auto& spec = params::specForSlot ((params::ParamSlot) i);
                expect (raw.slot[(size_t) i] != nullptr,
                        where (i, spec) + ": slot non risolto (l'id non esiste nel layout?)");
                expect (juce::String (spec.id) == juce::String (params::kSlotIds[i]),
                        "slot #" + juce::String (i) + ": kSlotIds dice \"" + params::kSlotIds[i]
                            + "\" ma specForSlot dice \"" + spec.id + "\"");
            }
        }

        beginTest ("ogni parametro, appena costruito, vale il default dichiarato in parameters.json");
        {
            // Il ciclo che avrebbe reso impossibile scrivere un default nell'unita' sbagliata:
            // confronta cio' che JUCE mette davvero nell'atomico con cio' che la tabella
            // dichiara, per tutti e quattro i Kind. `spec.def` e' gia' nell'unita' in cui vive
            // il parametro — normalizzata per Float e Bool, naturale per Int, indice per Choice
            // — quindi il confronto e' diretto, ed e' esattamente questa coincidenza che il
            // test pretende e che ParameterMapping.h deve continuare a produrre.
            DummyProcessor fresh;

            for (int i = 0; i < params::kNumParams; ++i)
            {
                const auto& spec = params::kTable[i];
                const auto* ptr = fresh.apvts.getRawParameterValue (spec.id);
                expect (ptr != nullptr, where (i, spec) + ": nessun parametro con questo id nel layout");

                if (ptr == nullptr)
                    continue;

                const auto got = ptr->load();
                expectWithinAbsoluteError (got, spec.def, 1.0e-5f,
                                           where (i, spec) + ": atteso " + juce::String (spec.def)
                                               + ", ottenuto " + juce::String (got));
            }
        }

        beginTest ("andata e ritorno naturale -> normalizzato -> naturale, per ogni kind");
        {
            // normalise() e denormalise() sono gemelle: la prima disfa esattamente cio' che fa
            // la seconda. Per i Float entro tolleranza, per i valori discreti esattamente —
            // ogni ottava, ogni semitono, ogni opzione di ogni choice.
            for (int i = 0; i < params::kNumParams; ++i)
            {
                const auto& spec = params::kTable[i];

                if (spec.kind == params::Kind::Float)
                {
                    for (const float x : { 0.0f, 0.05f, 0.25f, 0.5f, 0.75f, 0.95f, 1.0f })
                    {
                        const auto natural = params::denormalise (spec, x);
                        const auto back = params::normalise (spec, natural);
                        expectWithinAbsoluteError (back, x, 1.0e-4f,
                                                   where (i, spec) + ": normalizzato " + juce::String (x)
                                                       + " -> naturale " + juce::String (natural)
                                                       + " -> ottenuto " + juce::String (back));
                    }
                }
                else if (spec.kind == params::Kind::Int)
                {
                    for (int n = (int) spec.min; n <= (int) spec.max; ++n)
                    {
                        const auto back = juce::roundToInt (params::denormalise (spec, params::normalise (spec, (float) n)));
                        expectEquals (back, n, where (i, spec) + ": naturale " + juce::String (n)
                                                   + ", ottenuto " + juce::String (back));
                    }
                }
                else if (spec.kind == params::Kind::Choice)
                {
                    for (int k = 0; k < spec.numOptions; ++k)
                    {
                        // La stessa formula di params::normalisedDefault e di formatValue: un
                        // choice non passa da una mappa, e' l'indice spalmato su 0..1.
                        const auto v = spec.numOptions > 1 ? (float) k / (float) (spec.numOptions - 1) : 0.0f;
                        const auto back = juce::roundToInt (params::clamp01 (v) * (float) (spec.numOptions - 1));
                        expectEquals (back, k, where (i, spec) + ": indice " + juce::String (k)
                                                   + ", ottenuto " + juce::String (back));
                    }
                }
            }
        }

        beginTest ("l'APVTS vero restituisce il valore naturale che gli abbiamo scritto, per ogni parametro");
        {
            // Il ciclo che si accorge del bug delle quattro ottave, e di ogni suo parente: si
            // scrive un valore naturale nel parametro vero e lo si rilegge attraverso
            // params::naturalFromRaw, la stessa funzione che collectEngineParams usa per oct e
            // semi. Denormalizzare un Kind::Int qui dentro fa fallire 32 asserzioni (i sette
            // valori di `oct`, i venticinque di `semi`) prima ancora di arrivare al suono.
            DummyProcessor fresh;

            for (int i = 0; i < params::kNumParams; ++i)
            {
                const auto& spec = params::kTable[i];
                auto* param = fresh.apvts.getParameter (spec.id);
                const auto* ptr = fresh.apvts.getRawParameterValue (spec.id);

                if (param == nullptr || ptr == nullptr)
                {
                    expect (false, where (i, spec) + ": parametro assente dal layout");
                    continue;
                }

                for (const auto natural : naturalProbes (spec))
                {
                    const auto rawWanted = params::rawFromNatural (spec, natural);
                    param->setValueNotifyingHost (param->convertTo0to1 (rawWanted));

                    const auto rawGot = ptr->load();
                    const auto naturalGot = params::naturalFromRaw (spec, rawGot);

                    const auto detail = where (i, spec) + ": scritto naturale " + juce::String (natural)
                                        + " (grezzo " + juce::String (rawWanted) + "), riletto naturale "
                                        + juce::String (naturalGot) + " (grezzo " + juce::String (rawGot) + ")";

                    if (spec.kind == params::Kind::Float)
                        expectWithinAbsoluteError (rawGot, rawWanted, 1.0e-4f, detail); // il naturale di un Float puo' essere -inf (mappa Db): il confronto vive nel grezzo
                    else
                        expectEquals (naturalGot, natural, detail);
                }
            }
        }
    }
};

static ParameterSeamTests parameterSeamTests;

/**
 * L'altra giuntura: fra il file di progetto e l'APVTS.
 *
 * Non esercita SerumStyleSynthAudioProcessor::setStateInformation — XerumTests non compila
 * Source/plugin — ma i due pezzi che quel metodo mette in fila, params::resetToDefaults e
 * state::schemaVersionOf, nello stesso ordine.
 */
struct StateSeamTests final : juce::UnitTest
{
    StateSeamTests() : juce::UnitTest ("giuntura stato salvato -> APVTS", "params") {}

    void runTest() override
    {
        beginTest ("un parametro assente dallo stato salvato torna al default, non resta dov'e'");
        {
            DummyProcessor p;

            constexpr auto* specRes = params::find ("res");
            constexpr auto* specCutoff = params::find ("cutoff");
            static_assert (specRes != nullptr && specCutoff != nullptr, "res/cutoff non sono in ParameterTable.h");

            auto* cutoff = p.apvts.getParameter ("cutoff");
            auto* res = p.apvts.getParameter ("res");

            cutoff->setValueNotifyingHost (0.9f);
            res->setValueNotifyingHost (0.9f);

            // Lo stato che finisce nel progetto dell'utente, ma senza il nodo di `res`: e'
            // esattamente la forma che ha un progetto salvato *prima* che quel parametro
            // esistesse, ed e' il caso che al primo parametro aggiunto smette di essere teorico.
            auto saved = p.apvts.copyState();
            saved.removeChild (saved.getChildWithProperty (juce::Identifier ("id"), "res"), nullptr);

            // Poi l'utente continua a suonare: i valori correnti non c'entrano piu' col file.
            cutoff->setValueNotifyingHost (0.1f);
            res->setValueNotifyingHost (0.1f);

            // La sequenza di setStateInformation: prima il reset sullo schema, poi il file.
            params::resetToDefaults (p.apvts);
            p.apvts.replaceState (saved);

            expectWithinAbsoluteError (p.apvts.getRawParameterValue ("cutoff")->load(), 0.9f, 1.0e-4f,
                                       "cutoff era nel file: deve valere quello del file");
            expectWithinAbsoluteError (p.apvts.getRawParameterValue ("res")->load(),
                                       params::normalisedDefault (*specRes), 1.0e-4f,
                                       "res non era nel file: deve tornare al suo default, non restare a 0.1");
        }

        beginTest ("resetToDefaults riporta tutti e 56 i parametri al default dichiarato");
        {
            DummyProcessor p;

            for (const auto& spec : params::kTable)
                p.apvts.getParameter (spec.id)->setValueNotifyingHost (0.77f);

            params::resetToDefaults (p.apvts);

            for (int i = 0; i < params::kNumParams; ++i)
            {
                const auto& spec = params::kTable[i];
                const auto got = p.apvts.getRawParameterValue (spec.id)->load();
                expectWithinAbsoluteError (got, spec.def, 1.0e-5f,
                                           where (i, spec) + ": atteso " + juce::String (spec.def)
                                               + ", ottenuto " + juce::String (got));
            }
        }

        beginTest ("uno stato senza versione di schema e' un formato 1, non uno stato da rifiutare");
        {
            juce::ValueTree oldState { "PARAMS" };
            expectEquals (state::schemaVersionOf (oldState), 1,
                          "tutto cio' che e' stato salvato finora non ha l'attributo: vale 1");

            juce::ValueTree fresh { "PARAMS" };
            state::stampSchemaVersion (fresh);
            expectEquals (state::schemaVersionOf (fresh), state::kStateVersion);

            juce::ValueTree future { "PARAMS" };
            future.setProperty (state::ids::schemaVersion, 7, nullptr);
            expectEquals (state::schemaVersionOf (future), 7, "la versione letta e' quella scritta, non un default");
        }

        beginTest ("la versione di schema sopravvive al giro XML che l'host fa davvero");
        {
            DummyProcessor p;
            auto state = p.apvts.copyState();
            state::stampSchemaVersion (state);

            const auto xml = state.createXml();
            expect (xml != nullptr);
            expectEquals (state::schemaVersionOf (juce::ValueTree::fromXml (*xml)), state::kStateVersion);
        }
    }
};

static StateSeamTests stateSeamTests;
