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
 * Punto 10 della ricerca (docs/research/2026-09-19-confronto-synth-open-source.md): il furto
 * di voce.
 *
 * Due proprieta' distinte, e vale la pena tenerle separate perche' si rompono per ragioni
 * diverse: *quale* voce si ruba (il criterio) e *come* la si toglie di mezzo (la transizione).
 * Un criterio perfetto con un kill() in fondo continua a fare clic; una dissolvenza impeccabile
 * su una voce appena attaccata continua a togliere la nota sbagliata.
 */
struct VoiceStealingTests final : juce::UnitTest
{
    VoiceStealingTests() : juce::UnitTest ("voice stealing", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (0);

        beginTest ("rubare una voce non produce un gradino");
        {
            // Stessa tecnica di "ribattere una nota che suona gia' non produce un gradino": si
            // misura il salto massimo fra campioni adiacenti e lo si confronta con la pendenza
            // naturale dell'onda, misurata sullo stesso segnale poco prima. Il confronto e'
            // con una pendenza *misurata* e non con una costante scritta a mano perche' qui
            // suonano sedici voci insieme: quanto ripida sia la loro somma dipende da come le
            // fasi si dispongono, e una costante sarebbe o troppo larga o fragile.
            //
            // La configurazione e' scelta per rendere il gradino visibile e non per renderlo
            // grande. Due mosse, entrambe sul rapporto fra le due quantita' e non sul gradino:
            //
            //  - `framePosition` a 1.0, che nella tavola "basic" e' il seno. Al frame 0 c'e' un
            //    dente di sega, e li' la pendenza naturale *e' gia'* l'ampiezza della voce —
            //    misurato: picco 0.430 contro salto 0.427, cioe' un rapporto di 1.005. Su quella
            //    forma d'onda nessuna misura del gradino puo' discriminare, perche' il gradino
            //    che si cerca e la pendenza legittima valgono lo stesso numero. Sul seno il
            //    rapporto e' 166.
            //  - le note che rubano (MIDI 43..50) stanno **sotto** quelle tenute (55..70), cosi'
            //    che la pendenza del segnale nella finestra del furto non sia piu' ripida di
            //    quella della finestra in cui si e' misurata la pendenza naturale. Il verso
            //    conta: con le note nuove piu' acute, il confronto punirebbe il furto per
            //    dell'onda in piu' che il furto non ha prodotto.
            //
            // Il registro invece **non** e' libero, e la ragione e' la dissolvenza. Il suo primo
            // campione scende del 2.4 % dell'ampiezza della voce (e^(ln(1e-4)/384)), mentre un
            // seno a frequenza f si muove del 2*pi*f/fs: le due quantita' si pareggiano a
            // 183 Hz, cioe' attorno al RE2. Sotto quella nota la dissolvenza sarebbe piu'
            // ripida dell'onda che sta dissolvendo, e il test misurerebbe la propria scelta di
            // registro invece del difetto. Da MIDI 55 (196 Hz) in su il margine c'e'.
            //
            // `level` a 0.1 tiene la somma delle sedici voci lontana dal soft clipper (0.95):
            // sotto soglia il clipper e' bit-trasparente, quindi il salto che si misura e'
            // quello che le voci hanno prodotto davvero e non quello che il clipper ha lasciato
            // passare. Il test lo verifica invece di assumerlo.
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = defaultParams();
            p.attackSeconds = 0.005f;
            p.decaySeconds = 0.005f;
            p.sustain = 1.0f;
            p.releaseSeconds = 0.3f;
            p.filterOn = false; // il filtro suonerebbe di suo sul gradino e ne allargherebbe la coda
            p.framePosition = 1.0f; // il seno della tavola "basic"; vedi sopra
            p.level = 0.1f;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            juce::MidiBuffer noMidi;

            float previous = 0.0f;
            float peak = 0.0f;

            // Salto massimo fra campioni adiacenti nel blocco appena reso, cucitura con il
            // blocco precedente compresa: e' proprio li' che un furto a inizio blocco cade.
            auto scan = [&]
            {
                float maxJump = 0.0f;
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto s = buffer.getSample (0, i);
                    maxJump = juce::jmax (maxJump, std::abs (s - previous));
                    peak = juce::jmax (peak, std::abs (s));
                    previous = s;
                }
                return maxJump;
            };

            auto advance = [&] (int blocks)
            {
                float maxJump = 0.0f;
                for (int b = 0; b < blocks; ++b)
                {
                    buffer.clear();
                    synth.process (buffer, noMidi);
                    maxJump = juce::jmax (maxJump, scan());
                }
                return maxJump;
            };

            // Polifonia piena: sedici note tenute, nessun furto ancora.
            for (int i = 0; i < engine::VoiceManager::maxVoices; ++i)
                midi.addEvent (juce::MidiMessage::noteOn (1, 55 + i, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);
            scan();

            advance (60);                        // l'attacco e' passato, gli inviluppi sono al sustain
            const auto naturalSlope = advance (60); // la pendenza dell'onda, senza nessun furto

            // Otto furti, uno ogni 64 ms: abbastanza distanti da lasciar finire la dissolvenza
            // del furto precedente, cosi' ognuno parte dalla stessa condizione. Otto e non uno
            // perche' il gradino vale l'ampiezza *istantanea* della voce rubata: con un furto
            // solo si potrebbe cadere per caso vicino a uno zero dell'onda e non misurare
            // niente.
            float stealJump = 0.0f;

            for (int k = 0; k < 8; ++k)
            {
                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOn (1, 43 + k, 1.0f), 0);
                buffer.clear();
                synth.process (buffer, midi);

                stealJump = juce::jmax (stealJump, scan());
                stealJump = juce::jmax (stealJump, advance (8)); // 21 ms: tutta la dissolvenza
                advance (16);
            }

            logMessage ("furto: salto massimo " + juce::String (stealJump)
                        + " contro pendenza naturale " + juce::String (naturalSlope)
                        + " (picco " + juce::String (peak) + ")");

            expect (peak < 0.9f, "il soft clipper non deve entrare in gioco, picco " + juce::String (peak));
            expect (naturalSlope > 0.001f,
                    "l'onda deve scorrere, altrimenti il confronto non verifica niente: pendenza "
                        + juce::String (naturalSlope));

            // Il margine di 1.5 non e' un'indulgenza: la pendenza naturale e' un massimo su 60
            // blocchi, mentre il salto del furto si misura in un istante preciso, e le due
            // finestre non vedono la stessa disposizione di fasi. Un kill() sta comunque ordini
            // di grandezza sopra.
            expect (stealJump < naturalSlope * 1.5f,
                    "salto massimo al furto " + juce::String (stealJump) + " contro pendenza naturale "
                        + juce::String (naturalSlope));
        }

        // I tre test che seguono guardano il *criterio*, e per farlo parlano direttamente a
        // VoiceManager invece che a SynthEngine: la domanda e' quale slot del pool venga
        // sacrificato, e attraverso l'audio sommato di sedici voci quella domanda non ha una
        // risposta leggibile. Lo stato che osservano (isFading, isReleasing, getMidiNote) e'
        // lo stesso che il criterio usa per decidere, non una spia aggiunta per il test.

        /** Sedici note tenute a regime, il pool pieno e nessuna dissolvenza in corso. */
        auto fillPolyphony = [] (engine::VoiceManager& voices)
        {
            for (int i = 0; i < engine::VoiceManager::maxVoices; ++i)
                voices.noteOn (60 + i, 1.0f);
        };

        auto renderBlocks = [] (engine::VoiceManager& voices, juce::AudioBuffer<float>& buffer, int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                buffer.clear();
                voices.render (buffer.getWritePointer (0), buffer.getWritePointer (1),
                               buffer.getNumSamples());
            }
        };

        /** La nota della voce in dissolvenza, e quante ce ne sono. -1 se nessuna. */
        auto fadingNote = [] (const engine::VoiceManager& voices, int& count)
        {
            int note = -1;
            count = 0;

            for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
                if (voices.getVoice (i).isFading())
                {
                    ++count;
                    note = voices.getVoice (i).getMidiNote();
                }

            return note;
        };

        beginTest ("si ruba una voce in release, e fra quelle la piu' silenziosa");
        {
            // Il criterio, nella sua forma completa: tre note rilasciate a distanza di 43 ms
            // l'una dall'altra e tredici ancora premute. La rubata deve essere una delle tre
            // rilasciate — perche' l'esecutore ne ha gia' chiesto la fine — e fra quelle la
            // prima rilasciata, che a release esponenziale e' anche la piu' silenziosa.
            //
            // Il round-robin di prima avrebbe preso la voce zero, che qui e' proprio una delle
            // rilasciate: per questo la nota scelta come "prima rilasciata" e' la 61 e non la
            // 60. Con la 60 il test sarebbe passato anche sul codice vecchio, per coincidenza.
            engine::VoiceManager voices;
            voices.prepare (48000.0);
            voices.setWavetable (store.active());

            auto p = defaultParams();
            p.attackSeconds = 0.001f;
            p.decaySeconds = 0.001f;
            p.sustain = 1.0f;
            p.releaseSeconds = 0.5f; // lungo: le tre restano in release per tutta la prova
            voices.setParams (p);

            juce::AudioBuffer<float> buffer (2, 256);

            fillPolyphony (voices);
            renderBlocks (voices, buffer, 20);
            expectEquals (voices.countActive(), engine::VoiceManager::maxVoices,
                          "sedici note devono riempire la polifonia esatta");

            voices.noteOff (61);
            renderBlocks (voices, buffer, 8);
            voices.noteOff (63);
            renderBlocks (voices, buffer, 8);
            voices.noteOff (65);
            renderBlocks (voices, buffer, 8);

            // Le tre sono davvero a livelli diversi, altrimenti "la piu' silenziosa" non
            // significherebbe niente e il test starebbe verificando l'ordine del pool.
            const auto levelOf = [&voices] (int note)
            {
                for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
                    if (voices.getVoice (i).isActive() && voices.getVoice (i).getMidiNote() == note)
                        return voices.getVoice (i).getAmplitudeLevel();

                return -1.0f;
            };

            expect (levelOf (61) < levelOf (63) && levelOf (63) < levelOf (65),
                    "le tre voci in release devono stare a livelli diversi: "
                        + juce::String (levelOf (61)) + " / " + juce::String (levelOf (63)) + " / "
                        + juce::String (levelOf (65)));

            voices.noteOn (90, 1.0f); // la diciassettesima

            int fadingCount = 0;
            const auto stolen = fadingNote (voices, fadingCount);

            expectEquals (fadingCount, 1, "un furto solo, una dissolvenza sola");
            expectEquals (stolen, 61, "rubata la nota " + juce::String (stolen)
                                          + " invece della 61, la piu' silenziosa fra quelle in release");

            // L'altra meta' del punto 10: la nota nuova non riusa lo slot appena rubato.
            expectEquals (voices.countActive(), engine::VoiceManager::maxVoices,
                          "la polifonia resta piena, con la nota nuova al posto della rubata");
            expectEquals (voices.countSounding(), engine::VoiceManager::maxVoices + 1,
                          "la coda del furto suona ancora, in piu' rispetto alla polifonia");
        }

        beginTest ("senza voci in release si ruba la piu' vecchia");
        {
            engine::VoiceManager voices;
            voices.prepare (48000.0);
            voices.setWavetable (store.active());

            auto p = defaultParams();
            p.sustain = 1.0f;
            voices.setParams (p);

            juce::AudioBuffer<float> buffer (2, 256);

            fillPolyphony (voices); // 60..75, in quest'ordine
            renderBlocks (voices, buffer, 20);

            voices.noteOn (90, 1.0f);

            int fadingCount = 0;
            expectEquals (fadingNote (voices, fadingCount), 60, "la prima premuta e' la piu' vecchia");
            expectEquals (fadingCount, 1);

            // Il secondo furto deve prendere la successiva per eta', non tornare sulla prima:
            // e' il controllo che l'ordinamento sia per eta' e non un round-robin travestito.
            voices.noteOn (91, 1.0f);

            bool sixtyOneFading = false;
            fadingCount = 0;

            for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
                if (voices.getVoice (i).isFading())
                {
                    ++fadingCount;
                    sixtyOneFading = sixtyOneFading || voices.getVoice (i).getMidiNote() == 61;
                }

            expectEquals (fadingCount, 2, "due furti, due dissolvenze");
            expect (sixtyOneFading, "il secondo furto deve prendere la seconda piu' vecchia");
        }

        beginTest ("sotto la soglia di polifonia il furto non entra mai in gioco");
        {
            // La non-regressione, espressa come proprieta' invece che come confronto numerico:
            // con sedici note nessuna voce entra in dissolvenza e i tre slot di margine restano
            // vuoti, quindi il segnale e' quello che le stesse sedici voci producevano prima
            // che questo meccanismo esistesse.
            engine::VoiceManager voices;
            voices.prepare (48000.0);
            voices.setWavetable (store.active());
            voices.setParams (defaultParams());

            juce::AudioBuffer<float> buffer (2, 256);

            fillPolyphony (voices);
            renderBlocks (voices, buffer, 20);

            int fadingCount = 0;
            fadingNote (voices, fadingCount);
            expectEquals (fadingCount, 0, "nessuna dissolvenza sotto la soglia");
            expectEquals (voices.countSounding(), engine::VoiceManager::maxVoices);

            for (int i = engine::VoiceManager::maxVoices; i < engine::VoiceManager::poolSize; ++i)
                expect (! voices.getVoice (i).isActive(),
                        "lo slot di margine " + juce::String (i) + " non deve suonare");

            // Ribattere una nota gia' assegnata continua a passare per retrigger(), non per il
            // furto: la polifonia non si muove di un posto.
            voices.noteOn (60, 1.0f);
            expectEquals (voices.countActive(), engine::VoiceManager::maxVoices);
            fadingNote (voices, fadingCount);
            expectEquals (fadingCount, 0, "una ribattuta non e' un furto");
        }

        beginTest ("una raffica oltre la polifonia non perde ne' appende voci");
        {
            // Quaranta note-on di fila, senza un campione reso in mezzo: e' il caso che esaurisce
            // anche il margine del pool e obbliga reclaimQuietestFading() a entrare in gioco.
            // Quello che deve reggere e' l'invariante, non l'eleganza: mai piu' di maxVoices voci
            // in polifonia, mai piu' di poolSize che suonano, e alla fine niente di appeso.
            //
            // Le due varianti di sustain non sono un di piu': con sustain a zero le voci muoiono
            // da sole durante il decay, senza aspettare un note-off, e tutte le voci rilasciate
            // si trovano a livello *esattamente* zero — il caso in cui "la piu' silenziosa" non
            // discrimina piu' niente e decide il criterio di pareggio.
            for (const auto sustain : { 1.0f, 0.0f })
            {
                engine::VoiceManager voices;
                voices.prepare (48000.0);
                voices.setWavetable (store.active());

                auto p = defaultParams();
                p.sustain = sustain;
                p.decaySeconds = 0.05f;
                p.releaseSeconds = 0.05f;
                voices.setParams (p);

                juce::AudioBuffer<float> buffer (2, 256);
                const juce::String what = "sustain " + juce::String (sustain, 1) + ": ";

                for (int n = 0; n < 40; ++n)
                    voices.noteOn (36 + n, 1.0f);

                expectEquals (voices.countActive(), engine::VoiceManager::maxVoices,
                              what + "la polifonia deve essere piena e non oltre");
                expect (voices.countSounding() <= engine::VoiceManager::poolSize,
                        what + "il pool non puo' suonare piu' di quanto sia grande: "
                            + juce::String (voices.countSounding()));

                // Le sedici voci in polifonia devono essere sedici note *distinte*: una voce
                // persa si vedrebbe qui come un doppione o come uno slot vuoto.
                std::vector<int> sounding;
                for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
                    if (voices.getVoice (i).isActive() && ! voices.getVoice (i).isFading())
                        sounding.push_back (voices.getVoice (i).getMidiNote());

                std::sort (sounding.begin(), sounding.end());
                expectEquals ((int) sounding.size(), engine::VoiceManager::maxVoices, what + "voci in polifonia");
                expect (std::adjacent_find (sounding.begin(), sounding.end()) == sounding.end(),
                        what + "due voci sulla stessa nota");

                renderBlocks (voices, buffer, 4); // 21 ms: le dissolvenze sono finite
                int fadingCount = 0;
                fadingNote (voices, fadingCount);
                expectEquals (fadingCount, 0, what + "nessuna dissolvenza deve sopravvivere ai 8 ms");

                for (int n = 0; n < 40; ++n)
                    voices.noteOff (36 + n);

                renderBlocks (voices, buffer, 40); // 213 ms, molto oltre release e decay
                expectEquals (voices.countSounding(), 0, what + "nessuna voce deve restare appesa");
            }
        }
    }
};

static VoiceStealingTests voiceStealingTests;
