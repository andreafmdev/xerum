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

struct EngineParamsTests final : juce::UnitTest
{
    EngineParamsTests() : juce::UnitTest ("SynthEngine params", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (0);

        beginTest ("oscOn false silences the oscillator while the voice keeps running");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = defaultParams();
            synth.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
            }

            renderPeak (synth, 5, *this); // lascia passare l'attacco

            p.oscOn = false;
            synth.setParams (p);

            // Spegnere l'oscillatore non azzera lo stato che il filtro aveva già accumulato:
            // ci mette un blocco a smaltirlo (il filtro non viene resettato, solo l'ingresso
            // diventa zero). Si scarta quel blocco di coda prima di verificare il silenzio.
            renderPeak (synth, 1, *this);
            const auto silent = renderPeak (synth, 10, *this);

            expect (silent < 1.0e-5f, "picco a oscillatore spento " + juce::String (silent));
        }

        beginTest ("filtOn false bypasses the filter");
        {
            dsp::WavetableStore localStore;
            localStore.setActive (2); // "Digital Grit": ricco di armoniche, il filtro ha di che tagliare

            engine::SynthEngine withFilter;
            engine::SynthEngine withoutFilter;
            prepareEngine (withFilter, localStore);
            prepareEngine (withoutFilter, localStore);

            auto p = defaultParams();
            p.cutoffHz = 200.0f; // molto sotto la fondamentale: se il filtro lavora, l'ampiezza crolla
            p.filterOn = true;

            withFilter.setParams (p);
            p.filterOn = false;
            withoutFilter.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            for (auto* synth : { &withFilter, &withoutFilter })
            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth->process (buffer, midi);
            }

            const auto filteredRms = renderRms (withFilter, 20);
            const auto bypassedRms = renderRms (withoutFilter, 20);

            expect (bypassedRms > filteredRms * 2.0f,
                    "con filtro " + juce::String (filteredRms) + ", senza " + juce::String (bypassedRms));
        }

        beginTest ("bypass true outputs silence and kills the voices");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = defaultParams();
            synth.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
            }
            renderPeak (synth, 5, *this);

            p.bypass = true;
            synth.setParams (p);

            juce::AudioBuffer<float> buffer (2, 128);
            buffer.setSample (0, 0, 1.0f); // per verificare che process() lo azzeri davvero
            juce::MidiBuffer noMidi;
            synth.process (buffer, noMidi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expect (buffer.getSample (ch, i) == 0.0f, "campione non azzerato dal bypass");

            // Le voci sono state spente: uscendo dal bypass non deve restare nulla in coda.
            p.bypass = false;
            synth.setParams (p);
            const auto afterBypass = renderPeak (synth, 3, *this);
            expect (afterBypass < 1.0e-5f, "picco dopo il bypass " + juce::String (afterBypass));
        }

        beginTest ("level scales the output amplitude");
        {
            engine::SynthEngine loud;
            engine::SynthEngine quiet;
            prepareEngine (loud, store);
            prepareEngine (quiet, store);

            auto p = defaultParams();
            p.level = 1.0f;
            loud.setParams (p);
            p.level = 0.2f;
            quiet.setParams (p);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            for (auto* synth : { &loud, &quiet })
            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth->process (buffer, midi);
            }

            renderPeak (loud, 3, *this);
            renderPeak (quiet, 3, *this);

            const auto loudPeak = renderPeak (loud, 10, *this);
            const auto quietPeak = renderPeak (quiet, 10, *this);

            expect (quietPeak < loudPeak * 0.5f,
                    "level 1.0 -> " + juce::String (loudPeak) + ", level 0.2 -> " + juce::String (quietPeak));
        }

        beginTest ("un salto di cutoff non produce un gradino nel segnale");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            // Risonanza alta apposta: a Q=0.707 (il default) un salto di cutoff non lascia una
            // traccia misurabile nel segnale nemmeno senza rampa, quindi la soglia non
            // discriminerebbe niente (verificato: la review l'ha dimostrato, e forzare uno snap
            // con Q di default passava comunque). A Q=19.9 lo snap produce un salto misurabile
            // (~0.42, verificato disattivando temporaneamente la rampa) mentre la rampa reale lo
            // tiene a ~0.14: la soglia sotto sta esattamente in mezzo.
            auto p = defaultParams();
            p.cutoffHz = 200.0f;
            p.resonanceQ = 19.9f;
            synth.setParams (p);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            // L'ultimo campione prima del salto: la revisione ha dimostrato che il confine fra
            // il blocco precedente e questo era esattamente dove il gradino non rampato si
            // nascondeva, e un controllo solo "for i = 1" dentro il nuovo buffer lo saltava.
            float lastSampleBeforeJump = buffer.getSample (0, buffer.getNumSamples() - 1);
            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                synth.process (buffer, midi);
                lastSampleBeforeJump = buffer.getSample (0, buffer.getNumSamples() - 1);
            }

            p.cutoffHz = 12000.0f;
            synth.setParams (p);

            buffer.clear();
            synth.process (buffer, midi);

            float maxJump = std::abs (buffer.getSample (0, 0) - lastSampleBeforeJump);
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxJump = juce::jmax (maxJump, std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

            expect (maxJump < 0.25f, "salto massimo fra campioni " + juce::String (maxJump));
        }

        beginTest ("un salto di volume non produce un gradino nel segnale");
        {
            // Stesso schema del test di cutoff sopra, stessa trappola: il confine da guardare
            // e' fra l'ultimo campione del blocco vecchio e il primo del blocco nuovo, non un
            // punto qualunque dentro il blocco rampato.
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = defaultParams();
            synth.setParams (p);
            synth.setMasterGainLinear (0.2f);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            float lastSampleBeforeJump = buffer.getSample (0, buffer.getNumSamples() - 1);
            for (int b = 0; b < 20; ++b)
            {
                buffer.clear();
                synth.process (buffer, midi);
                lastSampleBeforeJump = buffer.getSample (0, buffer.getNumSamples() - 1);
            }

            // Salto di guadagno grosso apposta (0.2 -> 1.0, x5): senza rampa il gradino al
            // confine di blocco e' proporzionale all'ampiezza del segnale in quel punto per la
            // differenza di gain: misurato disattivando temporaneamente applyGainRamp (sostituita
            // con applyGain(masterGain_) come nel codice originale), il salto al confine arriva a
            // ~0.065; con la rampa reale resta sotto ~0.004 (dominato dalla pendenza naturale
            // dell'onda, non dal gradino di volume). La soglia sta a meta' strada.
            synth.setMasterGainLinear (1.0f);

            buffer.clear();
            synth.process (buffer, midi);

            float maxJump = std::abs (buffer.getSample (0, 0) - lastSampleBeforeJump);
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxJump = juce::jmax (maxJump, std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

            expect (maxJump < 0.03f, "salto massimo fra campioni " + juce::String (maxJump));
        }

        beginTest ("ribattere una nota che suona gia\' non produce un gradino");
        {
            // Legato ribattuto: note-on sulla stessa nota senza note-off in mezzo. Prima di
            // questo test VoiceManager::noteOn faceva kill() sulla voce esistente, cioe'
            // azzerava di colpo inviluppo, fase dell'oscillatore e stato del filtro: un gradino
            // da ampiezza piena a zero, misurato a 0.176 a fondo scala. E' il clic che si sente
            // a ogni nota ripetuta.
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = defaultParams();
            p.attackSeconds = 0.005f;
            p.decaySeconds = 0.005f;
            p.sustain = 1.0f;
            p.filterOn = false;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::AudioBuffer<float> buffer (2, 128);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            // Regime: l'inviluppo e' arrivato al sustain e l'onda scorre.
            float previous = 0.0f;
            for (int b = 0; b < 40; ++b)
            {
                buffer.clear();
                synth.process (buffer, midi);
                previous = buffer.getSample (0, buffer.getNumSamples() - 1);
            }

            // Secondo note-on sulla stessa nota, senza note-off.
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, midi);

            float maxJump = std::abs (buffer.getSample (0, 0) - previous);
            for (int i = 1; i < buffer.getNumSamples(); ++i)
                maxJump = juce::jmax (maxJump, std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

            // La soglia e' la pendenza naturale dell'onda a questa nota (~0.02 fra campioni
            // adiacenti), con margine: un kill() mascherato ricadrebbe a 0.17 e verrebbe preso.
            expect (maxJump < 0.05f, "salto massimo fra campioni " + juce::String (maxJump));
        }

        beginTest ("oct/semi: la mappa lineare della UI e' invertibile");
        {
            // Attenzione a cosa prova questo test: la mappa Linear di oct/semi, che serve alla
            // *UI* (formatValue in ParameterMapping.h e il relay web, che scambiano valori
            // normalizzati 0..1). Il motore invece li legge in unita' naturali dall'APVTS —
            // vedi "oct/semi arrivano in unita' naturali" piu' sotto e ParameterSeamTests.cpp.
            // Confondere i due percorsi e' esattamente il difetto che ha trasposto lo strumento
            // di quattro ottave, quindi vale la pena dire qui quale dei due si sta guardando.
            const auto* semiSpec = params::find ("semi");
            const auto* octSpec = params::find ("oct");
            expect (semiSpec != nullptr && octSpec != nullptr, "spec di oct/semi non trovate");

            const int semiCases[] = { -4, -1, 2, 5, 8, 0, -12, 12 };
            for (auto expected : semiCases)
            {
                const float raw = ((float) expected - semiSpec->min) / (semiSpec->max - semiSpec->min);
                const auto recovered = juce::roundToInt (denormaliseLinear (*semiSpec, raw));
                expectEquals (recovered, expected, "semi " + juce::String (expected));
            }

            const int octCases[] = { -1, 2, 0, -3, 3 };
            for (auto expected : octCases)
            {
                const float raw = ((float) expected - octSpec->min) / (octSpec->max - octSpec->min);
                const auto recovered = juce::roundToInt (denormaliseLinear (*octSpec, raw));
                expectEquals (recovered, expected, "oct " + juce::String (expected));
            }
        }

        beginTest ("oct/semi/fine: la fondamentale renderizzata e' quella attesa");
        {
            struct Case { int octave; int semitones; float fineCents; };
            // Stessi valori di semi/oct del test precedente, verificati questa volta end-to-end
            // sull'audio renderizzato (zero-crossing), non solo sull'aritmetica dei parametri.
            const Case cases[] = {
                { 0, -4, 0.0f }, { 0, -1, 0.0f }, { 0, 2, 0.0f }, { 0, 5, 0.0f }, { 0, 8, 0.0f },
                { -1, 0, 0.0f }, { 2, 0, 0.0f }, { 0, 0, 50.0f }
            };

            constexpr int noteNumber = 60; // Do centrale, ~261.63 Hz
            constexpr double sampleRate = 48000.0;

            for (const auto& c : cases)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = defaultParams();
                p.octave = c.octave;
                p.semitones = c.semitones;
                p.fineCents = c.fineCents;
                p.filterOn = false; // niente filtro/risonanza a sporcare il conteggio degli zero
                synth.setParams (p);

                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, noteNumber, 1.0f), 0);
                {
                    juce::AudioBuffer<float> buffer (2, 128);
                    buffer.clear();
                    synth.process (buffer, midi); // accende la nota prima di misurare
                }

                const auto measuredHz = measureFundamentalHz (synth, sampleRate, 4800, 43200);

                const auto offsetSemitones = (float) (12 * c.octave + c.semitones) + c.fineCents * 0.01f;
                const auto expectedHz = 440.0f * std::pow (2.0f, ((float) noteNumber + offsetSemitones - 69.0f) / 12.0f);

                expectWithinAbsoluteError (measuredHz, expectedHz, expectedHz * 0.02f,
                                           "oct " + juce::String (c.octave) + " semi " + juce::String (c.semitones)
                                               + " fine " + juce::String (c.fineCents) + ": misurato "
                                               + juce::String (measuredHz) + " atteso " + juce::String (expectedHz));
            }
        }
    }
};

static EngineParamsTests engineParamsTests;

// --- Task 8: robustezza a livello motore (polifonia, estremi, silenzio dopo il release) ---
//
// NOTA sulla copertura: il test "bypass" del brief di Task 8 non e' stato aggiunto qui perche'
// duplicherebbe "bypass true outputs silence and kills the voices" sopra, che e' piu' severo
// (forza un campione a 1.0f prima del bypass per provare che venga davvero azzerato, invece di
// limitarsi a osservare che il picco e' basso). Si tiene quello.
struct EngineRobustnessTests final : juce::UnitTest
{
    EngineRobustnessTests() : juce::UnitTest ("SynthEngine robustness", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (0);

        beginTest ("a note sounds and silence returns after release");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);
            synth.setParams (defaultParams());

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            expect (buffer.getMagnitude (0, 0, buffer.getNumSamples()) > 0.01f, "la nota deve suonare");

            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buffer.clear();
            synth.process (buffer, midi);

            // release e' 50 ms (~19 blocchi da 128 campioni): si scarta il decadimento vero e
            // proprio (che non e' silenzioso, sta ancora suonando) prima di misurare il vero
            // silenzio. Controllare il picco sull'intera finestra, decadimento incluso, fallirebbe
            // sempre appena dopo il note-off: non sarebbe un bug, sarebbe il release che lavora.
            renderPeak (synth, 30, *this);
            const auto tail = renderPeak (synth, 20, *this); // dopo il release, silenzio vero
            expect (tail < 1.0e-4f, "dopo il release deve tornare il silenzio, picco " + juce::String (tail));
        }

        beginTest ("sixteen voices together do not clip nor hang");
        {
            // VoiceManager::maxVoices == 16: sedici note distinte riempiono il pool esatto,
            // senza voice stealing a confondere il risultato.
            engine::SynthEngine synth;
            prepareEngine (synth, store);
            synth.setParams (defaultParams());

            juce::MidiBuffer midi;
            for (int note = 48; note < 64; ++note)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);

            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
            }

            // Sedici note tenute a fondo scala sono il caso estremo *senza* modulazione, ed e'
            // esattamente il lavoro del soft clipper in SynthEngine::process tenerle dentro.
            // Quello che si verifica qui e' il suo contratto: mai oltre 0 dBFS, mai un campione
            // non finito (renderPeak controlla isfinite su ognuno). Che un accordo *normale*
            // resti sotto la soglia del clipper, cioe' che non ci sia distorsione nell'uso reale,
            // e' il test successivo.
            const auto peak = renderPeak (synth, 50, *this);
            expect (peak <= 1.0f, "picco " + juce::String (peak) + ": il soft clipper deve tenere l'uscita entro 0 dBFS");

            // E quanto presentano davvero al clipper, che il picco d'uscita non puo' dire: sopra
            // soglia il clipper comprime, quindi l'uscita satura verso 1 e smette di distinguere
            // un ingresso da un altro. Misurato con il metodo del gain ridotto, cioe' l'unico
            // numero confrontabile con la tabella di docs/architecture.md.
            {
                engine::SynthEngine probeSynth;
                prepareEngine (probeSynth, store);
                probeSynth.setParams (defaultParams());

                std::vector<int> notes;
                for (int note = 48; note < 64; ++note)
                    notes.push_back (note);

                // Due finestre, e la differenza fra le due e' il punto. Saltando il primo blocco
                // si misura il regime, cioe' "sedici note tenute": 2.000 (+6.0 dBFS). Includendo
                // l'attacco si misura una cosa diversa e molto piu' alta — 3.640, +11.2 dBFS —
                // perche' sedici note che partono sullo stesso campione partono anche sulla
                // stessa fase della tavola, e per il primo millisecondo si sommano in fase. Non
                // e' un caso raggiungibile suonando (nessuna mano preme sedici tasti nello
                // stesso campione) ed e' raggiungibile da un sequencer: e' il clipper a
                // occuparsene, ed e' il motivo per cui questo test esiste.
                const auto held = measureAtClipper (probeSynth, notes, 1.0f, 50, 40, 0.02f, 2);
                expect (held.clipperOff, "il gain di prova non basta: la misura non vale");

                engine::SynthEngine onsetSynth;
                prepareEngine (onsetSynth, store);
                onsetSynth.setParams (defaultParams());
                const auto onset = measureAtClipper (onsetSynth, notes, 1.0f, 50, 40);
                expect (onset.clipperOff, "il gain di prova non basta: la misura non vale");

                logMessage ("sedici note: ingresso al clipper a regime " + juce::String (held.peak, 3)
                            + " (" + juce::String (juce::Decibels::gainToDecibels (held.peak), 2)
                            + " dBFS), con l'attacco in fase " + juce::String (onset.peak, 3)
                            + " (" + juce::String (juce::Decibels::gainToDecibels (onset.peak), 2) + " dBFS)");
            }

            midi.clear();
            for (int note = 48; note < 64; ++note)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            // release e' 50 ms (~19 blocchi da 128 campioni): si scarta il decadimento vero e
            // proprio (che ovviamente non e' silenzioso, sta ancora suonando) prima di verificare
            // che non resti nulla appeso. Controllare il picco sull'intera finestra, decadimento
            // incluso, fallirebbe sempre: non sarebbe un voice leak, sarebbe il release che lavora.
            renderPeak (synth, 30, *this);
            expect (renderPeak (synth, 20, *this) < 1.0e-4f, "nessuna voce deve restare appesa");
        }

        beginTest ("gain staging: una nota e un accordo normale stanno sotto il soft clipper");
        {
            // Il contrappeso del test precedente. Li' si verifica che il caso estremo (16 note
            // a fondo scala) venga contenuto; qui che l'uso *normale* non arrivi al clipper,
            // cioe' che il soft clipper sia una rete di sicurezza e non un compressore sempre
            // acceso. La soglia del clipper e' 0.95: sotto quella l'uscita e' bit-identica a
            // quella non clippata.
            //
            // Le soglie inferiori sono l'altra meta' del problema: con l'headroom di due
            // ritarature fa (-20 dB per voce) una nota singola usciva a -26 dBFS, cioe' uno
            // strumento inutilizzabilmente piano, e il difetto non veniva preso da nessun test.
            //
            // **Il margine dell'accordo e' il numero che fissa kVoiceHeadroomGain**, e adesso e'
            // una quantita' dichiarata invece che un avanzo. Fino alla ritaratura l'accordo
            // presentava 0.898 contro 0.95, cioe' 0.47 dB: l'headroom era stato scelto proprio
            // sul punto in cui l'accordo sfiorava la soglia, quindi il margine era zero per
            // costruzione. Con l'headroom a -4 dB l'accordo scende a 0.797 e il margine diventa
            // **1.5 dB**, e la regola di progetto e' che non debba mai scendere sotto 1.0 dB —
            // il conto e' 1:1, ogni decibel di margine e' un decibel di livello dello strumento.
            engine::SynthEngine synth;
            prepareEngine (synth, store);
            synth.setParams (defaultParams());
            synth.setMasterGainLinear (0.8f); // il default di `volume`

            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
            }

            const auto singleNote = renderPeak (synth, 20, *this);
            expect (singleNote < 0.95f, "una nota sola non deve arrivare al soft clipper, picco "
                                           + juce::String (singleNote));

            // 0.25 sta in mezzo fra i due livelli che questa costante ha avuto: 0.204 con
            // l'headroom a -8 dB (dove lo strumento era inutilizzabilmente piano) e 0.323 con
            // quello di adesso. E' quello che rende il test capace di accorgersi di un
            // arretramento, che e' il difetto che la prima ritaratura era venuta a correggere.
            // Un movimento nell'altro verso lo prende l'assertione sul margine dell'accordo,
            // qui sotto: le due si guardano le spalle a vicenda.
            expect (singleNote > 0.25f, "una nota sola non deve essere inudibile, picco "
                                           + juce::String (singleNote));

            {
                juce::MidiBuffer midi;
                for (int note : { 64, 67, 72 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
            }

            const auto chord = renderPeak (synth, 20, *this);
            expect (chord < 0.95f, "un accordo di quattro note non deve arrivare al soft clipper, picco "
                                      + juce::String (chord));

            // Il margine, che e' la cosa che il solo `< 0.95` non diceva. 1 dB e' la regola di
            // progetto; misurato 1.51 dB. Se questa riga comincia a mordere, il numero da
            // rivedere e' kVoiceHeadroomGain — mai la soglia in SynthEngine, che non e' un
            // posto dove si guadagna margine: alzarla accorcia il ginocchio e basta.
            constexpr float kMinChordMarginDb = 1.0f;
            const auto chordMarginDb = juce::Decibels::gainToDecibels (0.95f / chord);
            expect (chordMarginDb > kMinChordMarginDb,
                    "l'accordo ha " + juce::String (chordMarginDb, 2)
                        + " dB di margine sotto il clipper, meno del minimo di progetto ("
                        + juce::String (kMinChordMarginDb, 1) + " dB)");

            // Quanto sta sopra l'accordo rispetto alla nota singola e' la quantita' che decide
            // tutto il gain staging: e' il fattore di cresta a rendere impossibile portare la
            // nota singola a -14 dBFS RMS *e* tenere l'accordo fuori dal clipper (vedi il
            // commento di kVoiceHeadroomGain). Misurato 7.86 dB su questa finestra, che include
            // l'attacco, e non si e' mosso di un centesimo con la ritaratura: e' un rapporto fra
            // due misure che scalano insieme, quindi l'headroom non lo tocca per costruzione.
            const auto crestDb = juce::Decibels::gainToDecibels (chord / singleNote);
            expect (crestDb > 6.4f && crestDb < 9.4f,
                    "accordo e nota singola distano " + juce::String (crestDb, 2)
                        + " dB invece dei 7.86 misurati");

            const auto dbfs = [] (float peak) { return juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS"; };
            logMessage ("nota singola: " + dbfs (singleNote) + " | accordo di 4: " + dbfs (chord)
                        + " | margine " + juce::String (chordMarginDb, 2) + " dB");
        }

        beginTest ("il soft clipper d'uscita non tocca il segnale sotto soglia");
        {
            // Che sia davvero trasparente sotto 0.95 e' la proprieta' che rende accettabile
            // averlo sempre in catena: si confronta lo stesso segnale a due volumi diversi e
            // si verifica che il rapporto fra i picchi sia esattamente quello dei due gain.
            engine::SynthEngine synth;
            prepareEngine (synth, store);
            synth.setParams (defaultParams());
            synth.setMasterGainLinear (0.25f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);

            const auto quiet = renderPeak (synth, 20, *this);

            engine::SynthEngine louder;
            prepareEngine (louder, store);
            louder.setParams (defaultParams());
            louder.setMasterGainLinear (0.5f);

            juce::MidiBuffer midi2;
            midi2.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            juce::AudioBuffer<float> buffer2 (2, 128);
            buffer2.clear();
            louder.process (buffer2, midi2);

            const auto loud = renderPeak (louder, 20, *this);

            expect (loud < 0.95f, "il test ha senso solo se entrambi restano sotto soglia, picco " + juce::String (loud));
            expectWithinAbsoluteError (loud, quiet * 2.0f, quiet * 0.01f,
                    "raddoppiando il volume il picco deve raddoppiare: " + juce::String (quiet)
                        + " -> " + juce::String (loud));
        }

        beginTest ("every parameter at its extremes: no NaN, no explosion");
        {
            // Il test piu' debole del file: con 8 parametri portati a coppie di estremi (256
            // varianti) verifica solo che l'uscita resti finita e sotto una soglia larga, non che
            // il timbro sia quello giusto. Un motore che tornasse sempre silenzio la passerebbe
            // comunque: serve a beccare NaN/instabilita', non a garantire correttezza sonora.
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            for (int variant = 0; variant < 256; ++variant)
            {
                auto p = defaultParams();
                p.cutoffHz = (variant & 0x01) != 0 ? 20000.0f : 20.0f;
                p.resonanceQ = (variant & 0x02) != 0 ? 20.0f : 0.707f;
                p.driveGain = (variant & 0x04) != 0 ? juce::Decibels::decibelsToGain (24.0f) : 1.0f;
                p.filterStages = (variant & 0x08) != 0 ? 2 : 1;
                p.framePosition = (variant & 0x10) != 0 ? 1.0f : 0.0f;
                p.octave = (variant & 0x20) != 0 ? 3 : -3;
                p.semitones = (variant & 0x20) != 0 ? 12 : -12;
                p.pan = (variant & 0x40) != 0 ? 1.0f : -1.0f;
                p.keyTrack = (variant & 0x80) != 0 ? 1.0f : 0.0f;
                p.sustain = (variant & 0x80) != 0 ? 1.0f : 0.0f;
                p.velocityAmount = 1.0f;
                p.attackSeconds = 0.0f;
                p.releaseSeconds = 0.0f;
                p.level = 1.0f;
                synth.setParams (p);

                const int note = 24 + (variant % 84); // copre l'intero range MIDI utile
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
                {
                    juce::AudioBuffer<float> buffer (2, 128);
                    buffer.clear();
                    synth.process (buffer, midi);
                }

                // Il soft clipper d'uscita tiene tutte le varianti entro 0 dBFS, quindi la
                // soglia di prima (10.0) non discriminerebbe piu' niente. Quello che questo
                // test cerca non e' comunque il livello: e' NaN/inf e instabilita', e li
                // prende renderPeak, che chiama isfinite su ogni campione — un NaN attraversa
                // il soft clipper intatto (il confronto con la soglia e' falso e l'aritmetica
                // successiva lo propaga), quindi resta visibile.
                const auto peak = renderPeak (synth, 4, *this);
                expect (peak <= 1.0f, "variante " + juce::String (variant) + ": picco " + juce::String (peak));

                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
                renderPeak (synth, 4, *this); // scarica la coda prima della prossima variante
            }
        }

        beginTest ("il mask delle note segue cio' che il motore sta suonando");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);
            synth.setParams (defaultParams());

            const auto bitOf = [] (const engine::SynthEngine& s, int note)
            {
                const auto mask = note < 64 ? s.getActiveNotesLo() : s.getActiveNotesHi();
                return (mask & (juce::uint64 (1) << (note % 64))) != 0;
            };

            juce::AudioBuffer<float> buffer (2, 128);

            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            on.addEvent (juce::MidiMessage::noteOn (1, 100, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, on);

            expect (bitOf (synth, 60), "il DO centrale deve risultare acceso");
            expect (bitOf (synth, 100), "una nota sopra il 64 finisce nella meta' alta");
            expect (! bitOf (synth, 61), "una nota mai suonata resta spenta");

            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buffer.clear();
            synth.process (buffer, off);

            expect (! bitOf (synth, 60), "il note-off deve spegnere il bit");
            expect (bitOf (synth, 100), "e non deve toccare le altre note");

            // Il 60 si ripreme prima del panico: senza, al momento dell'all-notes-off l'unica
            // nota accesa starebbe nella meta' alta e le asserzioni sulla meta' bassa non
            // potrebbero fallire comunque. Cosi' entrambe le parole hanno un bit da spegnere.
            juce::MidiBuffer again;
            again.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            buffer.clear();
            synth.process (buffer, again);
            expect (bitOf (synth, 60), "il 60 ripremuto deve tornare acceso");

            juce::MidiBuffer panic;
            panic.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            buffer.clear();
            synth.process (buffer, panic);

            // Non `expectEquals ((int) ...)`: il cast a int di un juce::uint64 tiene solo i 32 bit
            // bassi, quindi il bit 60 di Lo e il bit 36 di Hi — le due note usate qui — sparivano
            // nel troncamento e le due asserzioni passavano anche col mask mai pulito. Si guarda
            // quindi bit per bit con lo stesso bitOf del resto del test, e poi la parola intera.
            expect (! bitOf (synth, 60), "all notes off deve spegnere il bit della meta' bassa");
            expect (! bitOf (synth, 100), "all notes off deve spegnere il bit della meta' alta");
            expect (synth.getActiveNotesLo() == 0, "all notes off pulisce la meta' bassa per intero");
            expect (synth.getActiveNotesHi() == 0, "all notes off pulisce la meta' alta per intero");
        }
    }
};

static EngineRobustnessTests engineRobustnessTests;
