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
 * Il tasso di modulazione non deve dipendere dalla dimensione del buffer che l'host consegna.
 *
 * Prima delle sotto-fette a lunghezza fissa lo decideva lui: il ciclo di SynthEngine::process
 * spezzava il render solo sugli eventi MIDI, quindi con un blocco da 128 la modulazione si
 * rivalutava 375 volte al secondo e con uno da 1024 solo 47. A 47 Hz un LFO a 15 Hz ha tre punti
 * per ciclo: non e' piu' un LFO, e' una scala, e la stessa patch suonava diversa a seconda di
 * come era configurata la scheda audio.
 *
 * Il test e' la definizione stessa della correzione: stessa sequenza di note, stessa modulazione,
 * due dimensioni di buffer che differiscono di un fattore otto, e le due uscite devono coincidere
 * **campione per campione**. Non "assomigliarsi": con le sotto-fette a lunghezza fissa i confini
 * di controllo cadono negli stessi punti assoluti nelle due rese, quindi l'uguaglianza e' esatta e
 * la tolleranza puo' essere zero.
 *
 * Gli eventi MIDI stanno a posizioni multiple di 128, cioe' allineate alla griglia di controllo
 * qualunque sia la sua lunghezza fra 32 e 128. Un evento *non* allineato spezzerebbe una
 * sotto-fetta in due punti diversi nelle due rese e introdurrebbe una differenza piccola ma vera:
 * e' il caso coperto dall'ultimo test qui sotto, con la sua tolleranza e la sua ragione.
 */
struct ControlRateTests final : juce::UnitTest
{
    ControlRateTests() : juce::UnitTest ("tasso di modulazione", "engine") {}

    /** Renderizza `totalSamples` a 48 kHz consegnando blocchi da `hostBlock`, con la stessa
        sequenza di note alle stesse posizioni *assolute*: e' l'unica variabile del confronto. */
    static std::vector<float> renderWithHostBlock (dsp::WavetableStore& store,
                                                   const engine::EngineParams& params,
                                                   const engine::ModSnapshot* mods,
                                                   int hostBlock,
                                                   int totalSamples,
                                                   const std::vector<std::pair<int, juce::MidiMessage>>& events)
    {
        engine::SynthEngine synth;
        engine::EngineSpec spec;
        spec.sampleRate = 48000.0;
        spec.maximumBlockSize = hostBlock;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());

        auto p = params;
        p.mods = mods;
        synth.setParams (p);
        synth.setMasterGainLinear (1.0f);

        std::vector<float> out;
        out.reserve ((size_t) totalSamples);

        juce::AudioBuffer<float> buffer (2, hostBlock);

        for (int start = 0; start < totalSamples; start += hostBlock)
        {
            juce::MidiBuffer midi;

            for (const auto& event : events)
                if (event.first >= start && event.first < start + hostBlock)
                    midi.addEvent (event.second, event.first - start);

            buffer.clear();
            synth.process (buffer, midi);

            const auto* d = buffer.getReadPointer (0);
            out.insert (out.end(), d, d + hostBlock);
        }

        return out;
    }

    /** Il piu' grande scarto assoluto fra due rese, e il picco della prima: il rapporto fra i due
        e' cio' che questo test misura davvero. */
    static std::pair<float, float> compare (const std::vector<float>& a, const std::vector<float>& b)
    {
        float worst = 0.0f;
        float peak = 0.0f;

        for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        {
            worst = juce::jmax (worst, std::abs (a[i] - b[i]));
            peak = juce::jmax (peak, std::abs (a[i]));
        }

        return { worst, peak };
    }

    static std::vector<std::pair<int, juce::MidiMessage>> noteSequence()
    {
        return { { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) },
                 { 1536, juce::MidiMessage::noteOn (1, 67, 0.7f) },
                 { 4096, juce::MidiMessage::noteOff (1, 60) },
                 { 5120, juce::MidiMessage::noteOff (1, 67) } };
    }

    /** Una route profonda dall'LFO al cutoff: e' il bersaglio dove il tasso di controllo si
        sente di piu', perche' il filtro traduce ogni gradino della modulazione in un gradino di
        timbro. 15 Hz non e' un numero a caso: ai 47 Hz di tasso che davano i blocchi da 1024 ci
        stanno tre punti per ciclo, ai 375 Hz dei blocchi da 128 venticinque. Fra le due rese
        c'e' tutta la differenza che il difetto sa produrre. */
    static engine::EngineParams lfoToCutoffParams (bool retrigger)
    {
        auto p = defaultParams();
        p.cutoffHz = 1200.0f;
        p.resonanceQ = 4.0f;
        p.releaseSeconds = 0.2f;
        p.modBase[(size_t) engine::modTargetIndexFor (params::ParamSlot::cutoff)] = 0.5f;

        p.lfoShapeIndex = 0;                     // sine
        p.lfoRateRaw = 15.0f / 20.0f;            // la mappa di `lrate` arriva a 20 Hz: 15 Hz
        p.lfoSync = false;
        p.lfoRetrig = retrigger;
        p.lfoFadeSeconds = 0.0f;
        p.lfoPhaseOffset01 = 0.0f;
        return p;
    }

    static engine::ModSnapshot lfoToCutoff()
    {
        engine::ModSnapshot mods;
        mods.count = 1;
        mods.routes[0] = { engine::ModSource::lfo,
                           engine::modTargetIndexFor (params::ParamSlot::cutoff), 0.9f };
        return mods;
    }

    void runTest() override
    {
        beginTest ("lfo -> cutoff: buffer da 128 e da 1024 danno la stessa identica uscita");
        {
            dsp::WavetableStore store; store.setActive (1);
            const auto mods = lfoToCutoff();
            const auto p = lfoToCutoffParams (true);

            const auto small = renderWithHostBlock (store, p, &mods, 128, 8192, noteSequence());
            const auto large = renderWithHostBlock (store, p, &mods, 1024, 8192, noteSequence());

            const auto [worst, peak] = compare (small, large);
            expect (peak > 0.01f, "la resa e' silenziosa: il confronto non proverebbe niente");
            expectWithinAbsoluteError (worst, 0.0f, 0.0f);
        }

        beginTest ("lo stesso vale per l'LFO libero, che gira anche fuori dalle voci");
        {
            dsp::WavetableStore store; store.setActive (1);
            const auto mods = lfoToCutoff();
            const auto p = lfoToCutoffParams (false);

            const auto small = renderWithHostBlock (store, p, &mods, 128, 8192, noteSequence());
            const auto large = renderWithHostBlock (store, p, &mods, 1024, 8192, noteSequence());

            const auto [worst, peak] = compare (small, large);
            expect (peak > 0.01f, "la resa e' silenziosa: il confronto non proverebbe niente");
            expectWithinAbsoluteError (worst, 0.0f, 0.0f);
        }

        beginTest ("con i rampanti in movimento e nessuna route, l'invarianza vale lo stesso");
        {
            // Il caso che il test qui sotto, a parametri fermi, non riesce a toccare: se nessuno
            // muove niente i quattro SmoothedValue stanno gia' sul bersaglio e `skip(n)`
            // restituisce lo stesso numero comunque si spezzi il blocco — l'invarianza li' e'
            // vera ma gratis. Qui invece il cutoff salta a meta' resa, quindi la rampa di 20 ms e'
            // davvero in corsa, e `skip(n)` la fa avanzare una volta per sotto-fetta: e' il
            // secondo modo con cui la lunghezza del blocco entrava nel segnale, indipendente
            // dalla mod matrix. Il salto arriva a 3072, multiplo di entrambe le dimensioni di
            // buffer: la *consegna* del parametro resta a tasso di blocco anche dopo la
            // correzione, e confrontarla a posizioni diverse misurerebbe quello, non questo.
            dsp::WavetableStore store; store.setActive (1);

            const auto render = [&store] (int hostBlock)
            {
                engine::SynthEngine synth;
                engine::EngineSpec spec;
                spec.sampleRate = 48000.0;
                spec.maximumBlockSize = hostBlock;
                spec.numChannels = 2;
                synth.prepare (spec);
                synth.setWavetable (store.active());

                auto p = defaultParams();
                p.cutoffHz = 800.0f;
                p.releaseSeconds = 0.2f;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                std::vector<float> out;
                juce::AudioBuffer<float> buffer (2, hostBlock);

                for (int start = 0; start < 8192; start += hostBlock)
                {
                    if (start == 3072)
                    {
                        p.cutoffHz = 6000.0f;
                        synth.setParams (p);
                    }

                    juce::MidiBuffer midi;

                    for (const auto& event : noteSequence())
                        if (event.first >= start && event.first < start + hostBlock)
                            midi.addEvent (event.second, event.first - start);

                    buffer.clear();
                    synth.process (buffer, midi);

                    const auto* d = buffer.getReadPointer (0);
                    out.insert (out.end(), d, d + hostBlock);
                }

                return out;
            };

            const auto [worst, peak] = compare (render (128), render (1024));
            expect (peak > 0.01f, "la resa e' silenziosa: il confronto non proverebbe niente");
            expectWithinAbsoluteError (worst, 0.0f, 0.0f);
        }

        beginTest ("senza nessuna route l'invarianza vale comunque, perche' i rampanti la seguono");
        {
            dsp::WavetableStore store; store.setActive (1);
            auto p = defaultParams();
            p.cutoffHz = 1200.0f;
            p.releaseSeconds = 0.2f;

            const auto small = renderWithHostBlock (store, p, nullptr, 128, 8192, noteSequence());
            const auto large = renderWithHostBlock (store, p, nullptr, 1024, 8192, noteSequence());

            const auto [worst, peak] = compare (small, large);
            expect (peak > 0.01f, "la resa e' silenziosa: il confronto non proverebbe niente");
            expectWithinAbsoluteError (worst, 0.0f, 0.0f);
        }

        beginTest ("un evento fuori dalla griglia di controllo lascia solo una differenza residua");
        {
            // Il limite onesto della correzione. Un note-on a un campione che non e' multiplo di
            // kControlBlockSamples tronca la sotto-fetta che lo contiene, e dove cada quel
            // troncamento dipende ancora da dove comincia il blocco dell'host: con buffer diversi
            // i due render hanno una manciata di confini di controllo diversi, e da li' in poi
            // l'inviluppo e i rampanti sono sfasati di pochi campioni.
            //
            // Non e' il difetto di prima che torna: quello era la *frequenza* dei confini a
            // cambiare di un fattore otto, questo e' la loro *fase* a cambiare una volta per
            // evento. Il numero lo dice: scarto massimo 0.0078 su un picco di 0.767, cioe' l'1.0 %,
            // contro il 65 % che la stessa sequenza dava prima della correzione. La soglia sta al
            // 2 % e non all'1.1 % perche' questo test non deve diventare un rilevatore di
            // cambiamenti nel filtro: quello che afferma e' un ordine di grandezza.
            //
            // Aggiornamento, da quando la modulazione non passa piu' dagli SmoothedValue. Quel
            // residuo era piccolo anche perche' la rampa di 20 ms filtrava le differenze insieme
            // alla modulazione: tolto il filtro, la stessa sfasatura di fase costava il 2.9 %,
            // sopra la soglia. La risposta non e' stata alzare la soglia ma togliere la causa —
            // renderControlSlices ancora adesso la griglia all'inizio del buffer invece che
            // all'ultimo evento (vedi SynthEngine.h), e la prima sotto-fetta dopo un note-on si
            // accorcia per ritrovarla. Lo scarto misurato adesso e' **esattamente zero**: anche
            // un evento fuori griglia da' la stessa identica uscita con i due buffer. La soglia
            // resta dov'era, come tetto onesto: non e' lei a dire cosa sta succedendo, e' la
            // riga di log.
            dsp::WavetableStore store; store.setActive (1);
            const auto mods = lfoToCutoff();
            const auto p = lfoToCutoffParams (true);

            const std::vector<std::pair<int, juce::MidiMessage>> offGrid {
                { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) },
                { 1511, juce::MidiMessage::noteOn (1, 67, 0.7f) },   // 1511 = 47 x 32 + 7
                { 4096, juce::MidiMessage::noteOff (1, 60) },
                { 5120, juce::MidiMessage::noteOff (1, 67) } };

            const auto small = renderWithHostBlock (store, p, &mods, 128, 8192, offGrid);
            const auto large = renderWithHostBlock (store, p, &mods, 1024, 8192, offGrid);

            const auto [worst, peak] = compare (small, large);
            expect (peak > 0.01f, "la resa e' silenziosa: il confronto non proverebbe niente");
            logMessage ("evento fuori griglia: scarto massimo " + juce::String (worst, 6)
                        + " su un picco di " + juce::String (peak, 6));
            expect (worst < 0.02f * peak, "un evento disallineato non deve costare piu' di questo");
        }
    }
};

static ControlRateTests controlRateTests;
