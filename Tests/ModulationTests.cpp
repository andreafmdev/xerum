#include "EngineTestHelpers.h"

#include "dsp/MipTable.h"
#include "dsp/Saturation.h"
#include "dsp/PlateReverb.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"
#include "engine/ParamCollect.h"
#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"
#include "parameters/PresetValue.h"
#include "state/StateToEngine.h"
#include "state/StateTree.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace
{
using harness::defaultParams;
using harness::measureFundamentalHz;
using harness::prepareEngine;
using harness::renderPeak;
using harness::renderRms;
using harness::denormaliseLinear;
using harness::ClipperProbe;
using harness::measureAtClipper;
using harness::rawFromNormalised;
using harness::PresetPatch;
using harness::patchFromPreset;
} // namespace

/**
 * La modulazione dentro la voce: e' qui che la somma `base + Σ depth × livello sorgente`
 * diventa suono. I quattro test coprono, nell'ordine, che una sorgente per voce muova davvero
 * il parametro, che il segno del depth conti, che il clamp a 0..1 sia rispettato e — il piu'
 * importante — che senza assegnazioni non cambi un solo campione.
 */
struct ModulationVoiceTests final : juce::UnitTest
{
    ModulationVoiceTests() : juce::UnitTest ("modulazione nella voce", "engine") {}

    void runTest() override
    {
        beginTest ("env -> cutoff e' l'inviluppo di filtro: il timbro si apre durante l'attacco");
        {
            dsp::WavetableStore store; store.setActive (1);

            // Energia nei primi 107 ms di attacco e nei 107 successivi, con la stessa route su
            // cutoff a due profondita' diverse.
            struct Run { float early; float late; };

            const auto run = [&store] (float depth) -> Run
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                engine::ModSnapshot mods;
                mods.count = 1;
                mods.routes[0] = { engine::ModSource::env,
                                   engine::modTargetIndexFor (params::ParamSlot::cutoff), depth };

                auto p = defaultParams();
                p.filterOn = true;
                p.cutoffHz = 200.0f;                 // il valore denormalizzato non conta piu' da solo:
                p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::cutoff)] = 0.15f; // conta questo
                p.attackSeconds = 0.5f;              // attacco lento: c'e' tempo per misurare due punti
                p.sustain = 1.0f;
                p.mods = &mods;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
                juce::AudioBuffer<float> first (2, 128);
                first.clear();
                synth.process (first, m);

                const auto early = renderRms (synth, 40);   // subito dopo il note-on
                const auto late  = renderRms (synth, 40);   // piu' avanti nell'attacco
                return { early, late };
            };

            const auto modulated = run (1.0f);
            const auto flat = run (0.0f);

            // Cutoff piu' alto = piu' armoniche passano = piu' energia.
            expect (modulated.late > modulated.early * 1.2f, "il filtro non si e' aperto con l'inviluppo");

            // Il contrappeso, senza il quale il test non proverebbe niente: anche a depth 0
            // l'energia cresce, perche' cresce l'inviluppo d'ampiezza. Il confronto e' quindi
            // con la stessa nota, stesso cutoff di partenza, sola profondita' diversa.
            // Misurato: 0.074 contro 0.0019, un fattore 38. La soglia sta molto sotto, ma non
            // a 1: con la modulazione disattivata il rapporto sarebbe esattamente 1.
            logMessage ("env -> cutoff: late con depth 1 " + juce::String (modulated.late)
                            + ", con depth 0 " + juce::String (flat.late));
            expect (modulated.late > flat.late * 5.0f,
                    "depth 1 e depth 0 producono la stessa energia: la route non sta modulando");
        }

        beginTest ("depth negativo modula nel verso opposto");
        {
            dsp::WavetableStore store; store.setActive (1);

            const auto rmsWithDepth = [&store] (float depth)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                engine::ModSnapshot mods;
                mods.count = 1;
                mods.routes[0] = { engine::ModSource::vel,
                                   engine::modTargetIndexFor (params::ParamSlot::level), depth };

                auto p = defaultParams();
                p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.5f;
                p.mods = &mods;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                juce::AudioBuffer<float> b (2, 128);
                b.clear();
                synth.process (b, m);

                return renderRms (synth, 20);
            };

            const auto base = rmsWithDepth (0.0f);
            expect (rmsWithDepth (0.4f) > base * 1.1f, "depth positivo non ha alzato il livello");
            expect (rmsWithDepth (-0.4f) < base * 0.9f, "depth negativo non ha abbassato il livello");
        }

        beginTest ("il clamp a 0..1 impedisce di superare il massimo del parametro");
        {
            dsp::WavetableStore store; store.setActive (1);

            const auto rmsWith = [&store] (float base, float depth)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                engine::ModSnapshot mods;
                mods.count = 1;
                mods.routes[0] = { engine::ModSource::vel,
                                   engine::modTargetIndexFor (params::ParamSlot::level), depth };

                auto p = defaultParams();
                p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = base;
                p.mods = &mods;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                juce::AudioBuffer<float> b (2, 128);
                b.clear();
                synth.process (b, m);

                return renderRms (synth, 20);
            };

            // 0.9 + 1.0 × 1.0 = 1.9, clampato a 1.0: identico a partire gia' da 1.0 senza modulazione.
            expectWithinAbsoluteError (rmsWith (0.9f, 1.0f), rmsWith (1.0f, 0.0f), 1.0e-4f);

            // L'uguaglianza da sola non basta: sarebbe verificata anche da un motore che
            // ignorasse del tutto la modulazione, perche' allora entrambe le rese userebbero il
            // livello non modulato. Questa seconda asserzione chiude il buco: con base 0.9 il
            // depth deve *aver alzato* il livello — e il clamp deve averlo fermato a 1.0, che e'
            // esattamente cio' che rende vera la prima.
            expect (rmsWith (0.9f, 1.0f) > rmsWith (0.9f, 0.0f) * 1.05f,
                    "la route non ha alzato il livello: il clamp non e' l'unica cosa in gioco");
        }

        beginTest ("nessuna assegnazione: l'uscita e' identica campione per campione a prima");
        {
            dsp::WavetableStore store; store.setActive (1);

            const auto render = [&store] (const engine::ModSnapshot* mods)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);
                auto p = defaultParams();
                p.filterOn = true;
                p.mods = mods;
                synth.setParams (p);
                synth.setMasterGainLinear (0.8f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                juce::AudioBuffer<float> b (2, 128);
                b.clear();
                synth.process (b, m);

                std::vector<float> out;
                juce::MidiBuffer none;
                for (int i = 0; i < 30; ++i)
                {
                    juce::AudioBuffer<float> buf (2, 128);
                    buf.clear();
                    synth.process (buf, none);
                    const auto* d = buf.getReadPointer (0);
                    out.insert (out.end(), d, d + 128);
                }
                return out;
            };

            engine::ModSnapshot empty; // count = 0
            const auto withNull = render (nullptr);
            const auto withEmpty = render (&empty);

            expectEquals ((int) withNull.size(), (int) withEmpty.size());
            for (size_t i = 0; i < withNull.size(); ++i)
                expectWithinAbsoluteError (withEmpty[i], withNull[i], 0.0f);
        }

        beginTest ("env2 e' indipendente da env: stessa route, tempi diversi, traiettorie diverse");
        {
            // Il buco che questo test chiude. Finche' la sorgente `env` era l'inviluppo
            // d'ampiezza, una route env -> cutoff era costretta alla forma del volume: con
            // `sus` a 1 (nota tenuta a livello costante) il cutoff *non poteva* muoversi.
            // `env2` ha i propri quattro tempi, quindi puo' chiudere il filtro mentre la nota
            // tiene — ed e' esattamente cio' che si misura qui.
            dsp::WavetableStore store; store.setActive (1);

            struct Run { float early; float late; };

            // Tutto identico fra le due rese tranne la sorgente della singola route: stesso
            // depth, stessa base di cutoff, stesso inviluppo d'ampiezza. Se `env2` fosse un
            // alias di `env` i due risultati coinciderebbero campione per campione.
            const auto run = [&store] (engine::ModSource src) -> Run
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                engine::ModSnapshot mods;
                mods.count = 1;
                mods.routes[0] = { src, engine::modTargetIndexFor (params::ParamSlot::cutoff), 1.0f };

                auto p = defaultParams();
                p.filterOn = true;
                p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::cutoff)] = 0.15f;

                // Inviluppo d'ampiezza: sale in un millisecondo e poi tiene. Il volume e'
                // quindi costante per tutta la misura, e `env` vale ~1 fisso: una route env ->
                // cutoff lascia il filtro spalancato e immobile.
                p.attackSeconds = 0.001f;
                p.decaySeconds = 0.01f;
                p.sustain = 1.0f;

                // Il secondo inviluppo, con tempi suoi: scatta e poi scende a zero in 150 ms,
                // mentre la nota continua a suonare al massimo. E' la forma che con il solo
                // `env` non era esprimibile.
                p.attack2Seconds = 0.001f;
                p.decay2Seconds = 0.15f;
                p.sustain2 = 0.0f;
                p.release2Seconds = 0.05f;

                p.mods = &mods;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                juce::AudioBuffer<float> first (2, 128);
                first.clear();
                synth.process (first, m);

                const auto early = renderRms (synth, 20);   // ~53 ms: env2 e' ancora alto
                const auto late  = renderRms (synth, 60);   // ~160 ms dopo: env2 e' a zero
                return { early, late };
            };

            const auto viaEnv = run (engine::ModSource::env);
            const auto viaEnv2 = run (engine::ModSource::env2);

            logMessage ("env -> cutoff: early " + juce::String (viaEnv.early) + ", late " + juce::String (viaEnv.late));
            logMessage ("env2 -> cutoff: early " + juce::String (viaEnv2.early) + ", late " + juce::String (viaEnv2.late));

            // Con `env` il filtro resta aperto: la coda non e' piu' scura dell'inizio.
            expect (viaEnv.late > viaEnv.early * 0.8f,
                    "con env (sustain 1) il cutoff doveva restare fermo, invece si e' chiuso");

            // Con `env2` il filtro si richiude mentre la nota tiene.
            expect (viaEnv2.late < viaEnv2.early * 0.5f,
                    "env2 non ha chiuso il filtro: il suo decay non sta arrivando al cutoff");

            // E le due traiettorie sono davvero diverse, non due misure della stessa cosa.
            expect (viaEnv.late > viaEnv2.late * 3.0f,
                    "le due route producono la stessa coda: env2 non e' un inviluppo a se'");
        }

        beginTest ("env2 non gata la voce: release cortissimo, la nota continua a suonare");
        {
            // La trappola e' in stop(): `active_ = envelope_.isActive()`. Farlo dipendere anche
            // da envelope2_ (o dai due in and) troncherebbe la coda di ogni nota non appena il
            // modulatore finisce — un modulatore che spegne cio' che modula.
            dsp::WavetableStore store; store.setActive (1);

            // Una route env2 -> level, non env2 -> cutoff: serve un bersaglio che renda
            // *osservabile* la fine del secondo inviluppo senza portare l'uscita a zero, o il
            // test non distinguerebbe "voce uccisa" da "filtro chiuso".
            struct Run { float sounding; float afterNoteOff; };

            const auto run = [&store] (float release2Seconds) -> Run
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                engine::ModSnapshot mods;
                mods.count = 1;
                mods.routes[0] = { engine::ModSource::env2,
                                   engine::modTargetIndexFor (params::ParamSlot::level), 0.5f };

                auto p = defaultParams();
                p.level = 0.5f;
                p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.5f;

                // Rilascio d'ampiezza lungo un secondo: la nota ha una coda vera da difendere.
                p.attackSeconds = 0.001f;
                p.decaySeconds = 0.01f;
                p.sustain = 1.0f;
                p.releaseSeconds = 1.0f;

                p.attack2Seconds = 0.001f;
                p.decay2Seconds = 0.01f;
                p.sustain2 = 1.0f;
                p.release2Seconds = release2Seconds;

                p.mods = &mods;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer on;
                on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                juce::AudioBuffer<float> b (2, 128);
                b.clear();
                synth.process (b, on);

                const auto sounding = renderRms (synth, 20);

                juce::MidiBuffer off;
                off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                juce::AudioBuffer<float> b2 (2, 128);
                b2.clear();
                synth.process (b2, off);

                // 26 ms di scarto: il release di env2 (1 ms nel caso corto) e' finito da un
                // pezzo e la rampa di 20 ms sul livello si e' assestata.
                renderRms (synth, 10);
                return { sounding, renderRms (synth, 20) };
            };

            const auto shortRelease = run (0.001f);
            const auto longRelease = run (1.0f);

            logMessage ("env2 rel 1 ms: durante " + juce::String (shortRelease.sounding)
                            + ", dopo il note-off " + juce::String (shortRelease.afterNoteOff));
            logMessage ("env2 rel 1 s: dopo il note-off " + juce::String (longRelease.afterNoteOff));

            expect (shortRelease.sounding > 1.0e-3f, "la nota non ha mai suonato: il test non prova niente");

            // Il punto: la coda c'e' ancora, molto dopo che il secondo inviluppo si e' spento.
            // Se env2 gatasse la voce qui ci sarebbe silenzio esatto.
            expect (shortRelease.afterNoteOff > shortRelease.sounding * 0.15f,
                    "la nota si e' spenta con env2 invece che con l'inviluppo d'ampiezza");

            // E il contrappeso: con un release lungo su env2 la coda e' piu' forte, perche' la
            // route sul livello sta ancora spingendo. Senza questo, il test passerebbe anche se
            // rel2 non fosse letto da nessuno.
            expect (longRelease.afterNoteOff > shortRelease.afterNoteOff * 1.3f,
                    "il release di env2 non cambia niente: il parametro non arriva all'inviluppo");
        }
    }
};

static ModulationVoiceTests modulationVoiceTests;

/**
 * La modulazione vista dal motore: la pubblicazione lock-free dello snapshot, il mod wheel che
 * arriva da CC 1 e l'LFO libero che avanza a ogni blocco anche quando non suona niente.
 */
struct ModulationEngineTests final : juce::UnitTest
{
    ModulationEngineTests() : juce::UnitTest ("modulazione nel motore", "engine") {}

    void runTest() override
    {
        beginTest ("setMods pubblica le assegnazioni e process() le applica");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            auto p = defaultParams();
            // Base a meta' corsa, cosi' la somma del depth ha spazio per salire: con il level
            // di defaultParams() (1.0) il clamp mangerebbe tutta la modulazione.
            p.level = 0.5f;
            p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.5f;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            juce::AudioBuffer<float> b (2, 128);
            b.clear();
            synth.process (b, m);

            const auto before = renderRms (synth, 20);

            engine::ModSnapshot mods;
            mods.count = 1;
            mods.routes[0] = { engine::ModSource::vel,
                               engine::modTargetIndexFor (params::ParamSlot::level), 0.5f };
            synth.setMods (mods);

            const auto after = renderRms (synth, 20);
            expect (after > before * 1.1f, "la nuova assegnazione non e' arrivata alle voci");
        }

        beginTest ("il mod wheel arriva da CC 1 e vale 0..1");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            engine::ModSnapshot mods;
            mods.count = 1;
            mods.routes[0] = { engine::ModSource::mw,
                               engine::modTargetIndexFor (params::ParamSlot::level), 0.5f };
            synth.setMods (mods);

            auto p = defaultParams();
            p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::level)] = 0.4f;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            juce::AudioBuffer<float> b (2, 128);
            b.clear();
            synth.process (b, m);

            const auto closed = renderRms (synth, 30);

            juce::MidiBuffer cc;
            cc.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
            juce::AudioBuffer<float> b2 (2, 128);
            b2.clear();
            synth.process (b2, cc);

            const auto open = renderRms (synth, 30);
            expect (open > closed * 1.1f, "il mod wheel non ha modulato niente");
        }

        beginTest ("l'LFO libero avanza anche senza note e finisce nel meter");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            auto p = defaultParams();
            p.lfoRetrig = false;
            p.lfoShapeIndex = 2;   // saw: parte da 1 e scende
            p.lfoRateRaw = 1.0f;   // il massimo della mappa: 20 Hz
            p.lfoSync = false;
            synth.setParams (p);

            const auto first = synth.getLfoLevel();

            juce::MidiBuffer none;
            for (int i = 0; i < 10; ++i)
            {
                juce::AudioBuffer<float> b (2, 128);
                b.clear();
                synth.process (b, none);
            }

            expect (! juce::approximatelyEqual (synth.getLfoLevel(), first), "l'LFO libero non e' avanzato");
        }
    }
};

static ModulationEngineTests modulationEngineTests;


/**
 * I livelli che il motore pubblica per gli anelli di modulazione della UI.
 *
 * L'anello disegnato attorno a un knob modulato mostra `base + depth x livello sorgente`
 * (liveValue() in WebUI/src/synth/mod.ts): se i livelli non arrivano dal motore l'anello ha la
 * profondita' giusta e il movimento sbagliato — una route env -> cutoff resta ferma mentre il
 * filtro si apre. Qui si verifica che i cinque livelli siano quelli della voce e non costanti.
 *
 * Nota quale numero si osserva: getEnvLevel() e getEnv2Level() sono il **picco della sotto-fetta
 * peggiore del blocco appena reso**, non il valore di fine blocco (vedi il commento in
 * SynthEngine.h). Su un attacco monotono le due cose crescono insieme, quindi il test regge in
 * entrambi i casi; a distinguerle e' l'ultimo beginTest.
 */
struct MeterSourceLevelTests final : juce::UnitTest
{
    MeterSourceLevelTests() : juce::UnitTest ("livelli delle sorgenti per il meter", "engine") {}

    /** Un blocco da 128 campioni con il MIDI passato, poi i livelli pubblicati. */
    static void runBlock (engine::SynthEngine& synth, juce::MidiBuffer& midi)
    {
        juce::AudioBuffer<float> b (2, 128);
        b.clear();
        synth.process (b, midi);
    }

    static void runBlocks (engine::SynthEngine& synth, int n)
    {
        juce::MidiBuffer none;
        for (int i = 0; i < n; ++i)
            runBlock (synth, none);
    }

    void runTest() override
    {
        beginTest ("senza note i livelli per voce sono zero");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);
            synth.setParams (defaultParams());

            runBlocks (synth, 4);

            expectEquals (synth.getEnvLevel(), 0.0f, "env senza note non e' zero");
            expectEquals (synth.getEnv2Level(), 0.0f, "env2 senza note non e' zero");
            expectEquals (synth.getVelocityLevel(), 0.0f, "vel senza note non e' zero");
        }

        beginTest ("env cresce blocco dopo blocco durante un attacco lento");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            auto p = defaultParams();
            p.attackSeconds = 0.5f;    // 24000 campioni: 187 blocchi da 128, nessuno lo salta
            p.decaySeconds = 1.0f;
            p.sustain = 1.0f;
            synth.setParams (p);

            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            runBlock (synth, on);

            auto previous = synth.getEnvLevel();
            expect (previous > 0.0f, "il primo blocco non ha pubblicato nessun livello");

            for (int i = 0; i < 20; ++i)
            {
                runBlocks (synth, 1);
                const auto now = synth.getEnvLevel();
                expect (now > previous, "env non e' cresciuto fra due blocchi dell'attacco");
                previous = now;
            }

            expect (previous < 1.0f, "un attacco da mezzo secondo non puo' essere finito in 21 blocchi");
        }

        beginTest ("env2 ha il suo inviluppo, indipendente da quello d'ampiezza");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            auto p = defaultParams();
            p.attackSeconds = 0.001f;   // l'ampiezza e' gia' in cima
            p.decaySeconds = 1.0f;
            p.sustain = 1.0f;
            p.attack2Seconds = 2.0f;    // il secondo inviluppo e' appena partito
            p.decay2Seconds = 1.0f;
            p.sustain2 = 1.0f;
            synth.setParams (p);

            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            runBlock (synth, on);
            runBlocks (synth, 10);

            expect (synth.getEnvLevel() > 0.9f, "env doveva essere in cima dopo un millisecondo di attacco");
            expect (synth.getEnv2Level() > 0.0f, "env2 non si e' mosso");
            expect (synth.getEnv2Level() < 0.2f, "env2 con due secondi di attacco non puo' essere gia' lassu'");
        }

        beginTest ("vel e' la velocity della nota e torna a zero quando la voce si spegne");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            auto p = defaultParams();
            p.releaseSeconds = 0.01f;
            synth.setParams (p);

            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, 0.5f), 0);
            runBlock (synth, on);
            runBlocks (synth, 2);

            expectWithinAbsoluteError (synth.getVelocityLevel(), 0.5f, 0.01f,
                                       "vel non e' la velocity della nota che suona");

            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            runBlock (synth, off);
            runBlocks (synth, 40);      // il release e' 10 ms: 4 blocchi bastano, 40 abbondano

            expectEquals (synth.getVelocityLevel(), 0.0f, "vel non e' tornata a zero a voce spenta");
        }

        beginTest ("mw segue il CC 1 anche senza note");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);
            synth.setParams (defaultParams());

            runBlocks (synth, 1);
            expectEquals (synth.getModWheelLevel(), 0.0f, "il mod wheel parte da zero");

            juce::MidiBuffer cc;
            cc.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
            runBlock (synth, cc);

            expectWithinAbsoluteError (synth.getModWheelLevel(), 1.0f, 0.001f,
                                       "il mod wheel non e' arrivato al meter");
        }

        beginTest ("env pubblica il picco del blocco, non il valore di fine blocco");
        {
            dsp::WavetableStore store; store.setActive (1);
            engine::SynthEngine synth; prepareEngine (synth, store);

            // Attacco e decay piu' corti di un blocco da 128 campioni (2.67 ms a 48 kHz) ma piu'
            // lunghi di una sotto-fetta di controllo (32 campioni, 0.67 ms): a fine blocco
            // l'inviluppo e' gia' sceso al sustain, mentre dentro il blocco e' passato per l'uno.
            // E' il caso che il valore di fine blocco non saprebbe mostrare, ed e' la ragione per
            // cui si pubblica il massimo invece dell'istantaneo.
            //
            // La risoluzione ha un fondo, ed e' quello del tasso di controllo: un attacco piu'
            // corto di kControlBlockSamples passa fra due campionamenti e nessuno lo vede. Per
            // andare piu' in basso bisognerebbe guardare l'inviluppo campione per campione dentro
            // SynthVoice::render — costo sul percorso audio per un'indicazione sullo schermo.
            auto p = defaultParams();
            p.attackSeconds = 0.002f;    // ~96 campioni: tre sotto-fette
            p.decaySeconds = 0.0005f;
            p.sustain = 0.1f;
            synth.setParams (p);

            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            runBlock (synth, on);

            expect (synth.getEnvLevel() > 0.8f,
                    "il picco dell'attacco e' passato dentro il blocco e il meter non l'ha visto");

            // Il blocco dopo: l'inviluppo e' al sustain, e il meter lo segue giu'. Se il picco
            // restasse appeso, l'anello non tornerebbe mai indietro.
            runBlocks (synth, 1);
            expect (synth.getEnvLevel() < 0.2f, "il picco non e' stato azzerato all'inizio del blocco");
        }
    }
};

static MeterSourceLevelTests meterSourceLevelTests;


/**
 * Il percorso che PluginProcessor::rebuildModSnapshot() fa sul message thread, meno
 * l'AudioProcessor: XerumTests non linka juce_audio_processors, quindi il processore non è
 * istanziabile qui, ma il pezzo che conta — dal JSON che la WebUI manda a setMods fino allo
 * snapshot che il motore riceve — sì.
 *
 * Tiene insieme tre cose scritte in file diversi: gli id di state::ids, la forma del nodo che
 * state::setMods produce e quella che state::buildModSnapshot legge. ModMatrixTests costruisce
 * il nodo MODS a mano, quindi una divergenza fra i due modi di scriverlo gli sfuggirebbe.
 */
struct ModStateWiringTests final : juce::UnitTest
{
    ModStateWiringTests() : juce::UnitTest ("cablaggio stato -> mod matrix", "engine") {}

    void runTest() override
    {
        beginTest ("il JSON che la UI manda a setMods arriva intero nello snapshot");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);

            state::setMods (root,
                            juce::JSON::parse (R"([{"src":"lfo","target":"cutoff","depth":0.5},)"
                                               R"( {"src":"vel","target":"level","depth":-0.25}])"),
                            nullptr);

            engine::ModSnapshot snapshot;
            state::buildModSnapshot (root.getChildWithName (state::ids::MODS), snapshot);

            expectEquals (snapshot.count, 2);

            expect (snapshot.routes[0].src == engine::ModSource::lfo);
            expectEquals (snapshot.routes[0].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::cutoff));
            expectWithinAbsoluteError (snapshot.routes[0].depth, 0.5f, 1.0e-6f);

            expect (snapshot.routes[1].src == engine::ModSource::vel);
            expectEquals (snapshot.routes[1].targetIndex,
                          engine::modTargetIndexFor (params::ParamSlot::level));
            expectWithinAbsoluteError (snapshot.routes[1].depth, -0.25f, 1.0e-6f);
        }

        beginTest ("svuotare la lista dalla UI spegne davvero la modulazione");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);

            state::setMods (root, juce::JSON::parse (R"([{"src":"mw","target":"pan","depth":1}])"), nullptr);
            state::setMods (root, juce::JSON::parse ("[]"), nullptr);

            engine::ModSnapshot snapshot;
            state::buildModSnapshot (root.getChildWithName (state::ids::MODS), snapshot);

            expectEquals (snapshot.count, 0);
        }

        beginTest ("uno stato senza nodo MODS non lascia in piedi lo snapshot precedente");
        {
            // Quello che arriva da un progetto salvato prima che MODS esistesse: getChildWithName
            // ritorna un albero non valido, e rebuildModSnapshot lo passa comunque a
            // buildModSnapshot. Deve uscirne uno snapshot vuoto, non il precedente rimasto lì.
            juce::ValueTree root { "PARAMS" };

            engine::ModSnapshot snapshot;
            snapshot.count = 3;
            state::buildModSnapshot (root.getChildWithName (state::ids::MODS), snapshot);

            expectEquals (snapshot.count, 0);
        }
    }
};

static ModStateWiringTests modStateWiringTests;

/** warp come ottavo target: una route sul warp deve suonare come il knob. */
struct WarpModulationTests final : public juce::UnitTest
{
    WarpModulationTests() : juce::UnitTest ("mod matrix: warp come target", "engine") {}

    void runTest() override
    {
        beginTest ("mw -> warp a fondo corsa suona come il knob warp a 1, passato il transitorio delle rampe");
        {
            dsp::WavetableStore store; store.setActive (1);
            const auto renderTail = [&] (bool viaMatrix)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);
                engine::ModSnapshot mods;
                if (viaMatrix)
                {
                    mods.count = 1;
                    mods.routes[0] = { engine::ModSource::mw, engine::modTargetIndexFor (params::ParamSlot::warp), 1.0f };
                    synth.setMods (mods);
                }
                auto p = defaultParams();
                p.filterOn = false;   // il filtro ha memoria: confronto solo l'oscillatore
                p.warp = viaMatrix ? 0.0f : 1.0f;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::controllerEvent (1, 1, 127), 0);
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                std::vector<float> tail;
                juce::MidiBuffer none;
                for (int b = 0; b < 60; ++b)   // 160 ms; le rampe durano 20
                {
                    juce::AudioBuffer<float> buf (2, 128);
                    buf.clear();
                    synth.process (buf, b == 0 ? m : none);
                    if (b >= 30)
                        tail.insert (tail.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 128);
                }
                return tail;
            };

            const auto knob = renderTail (false);
            const auto matrix = renderTail (true);
            float maxDiff = 0.0f, peak = 0.0f;
            for (size_t i = 0; i < knob.size(); ++i)
            {
                maxDiff = std::max (maxDiff, std::abs (knob[i] - matrix[i]));
                peak = std::max (peak, std::abs (knob[i]));
            }
            expect (peak > 0.01f, "il motore deve suonare");
            expect (maxDiff < 1.0e-4f, "route e knob divergono di " + juce::String (maxDiff));
        }

        beginTest ("il knob warp cambia davvero il suono");
        {
            dsp::WavetableStore store; store.setActive (1);
            const auto rmsAt = [&] (float warp)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);
                auto p = defaultParams();
                p.filterOn = true; p.cutoffHz = 2000.0f;   // un passa-basso rende udibile lo spostamento di brillantezza
                p.warp = warp;
                synth.setParams (p);
                juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
                juce::AudioBuffer<float> b (2, 128); b.clear(); synth.process (b, m);
                renderRms (synth, 20);
                return renderRms (synth, 40);
            };
            const auto plain = rmsAt (0.0f), warped = rmsAt (1.0f);
            expect (std::abs (plain - warped) > 0.02f * plain, "warp 0 e warp 1 suonano uguali");
        }
    }
};

static WarpModulationTests warpModulationTests;
