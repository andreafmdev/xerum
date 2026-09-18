#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"
#include "parameters/ParamCollect.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <map>

namespace
{
/** Parametri di base per far suonare una nota senza sorprese: attacco/rilascio brevi,
    filtro spalancato, niente pan/drive/keytrack. I singoli test alterano solo ciò che
    vogliono osservare. */
engine::EngineParams defaultParams()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.framePosition = 0.0f;
    p.octave = 0;
    p.semitones = 0;
    p.fineCents = 0.0f;
    p.level = 1.0f;
    p.filterOn = true;
    p.filterType = dsp::StateVariableFilter::Type::lowPass;
    p.filterStages = 2;
    p.cutoffHz = 8000.0f;
    p.resonanceQ = 0.707f;
    p.driveGain = 1.0f;
    p.keyTrack = 0.0f;
    p.attackSeconds = 0.001f;
    p.decaySeconds = 0.01f;
    p.sustain = 1.0f;
    p.releaseSeconds = 0.05f;
    p.velocityAmount = 0.0f;
    p.pan = 0.0f;
    p.bypass = false;
    return p;
}

/** Fa girare `numBlocks` blocchi da 128 campioni senza MIDI e ritorna il picco assoluto
    misurato su tutti i canali. Controlla anche che l'uscita resti finita: un bug di
    stabilità in un qualsiasi test emerge subito invece di passare inosservato. */
float renderPeak (engine::SynthEngine& synth, int numBlocks, juce::UnitTest& test)
{
    float peak = 0.0f;
    juce::MidiBuffer noMidi;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, noMidi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                test.expect (std::isfinite (data[i]), "campione non finito");
                peak = juce::jmax (peak, std::abs (data[i]));
            }
        }
    }

    return peak;
}

/** RMS su `numBlocks` blocchi da 128 campioni, canale sinistro. */
float renderRms (engine::SynthEngine& synth, int numBlocks)
{
    double sumSquares = 0.0;
    int count = 0;
    juce::MidiBuffer noMidi;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, noMidi);

        const auto* data = buffer.getReadPointer (0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            sumSquares += (double) data[i] * (double) data[i];
            ++count;
        }
    }

    return count > 0 ? (float) std::sqrt (sumSquares / count) : 0.0f;
}

/** SynthEngine tiene un std::atomic (la mailbox della wavetable): non e' copiabile ne'
    spostabile, quindi si prepara sul posto invece di ritornarlo per valore. */
void prepareEngine (engine::SynthEngine& synth, dsp::WavetableStore& store)
{
    engine::EngineSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 128;
    spec.numChannels = 2;
    synth.prepare (spec);
    synth.setWavetable (store.active());
}

/** Stessa formula del ramo Map::Linear di params::denormalise (Source/parameters/
    ParameterMapping.h), copiata qui invece di inclusa: quell'header porta dentro
    juce_audio_processors, che XerumTests non linka (nessun bisogno degli AudioParameter*
    per testare solo l'aritmetica di denormalizzazione). oct e semi usano entrambi
    Map::Linear, quindi la formula e' fedele. */
float denormaliseLinear (const params::Spec& s, float x) noexcept
{
    return s.min + x * (s.max - s.min);
}

/** Fa girare `settleSamples` di scarto (attacco/transiente) poi conta gli attraversamenti
    dello zero (da negativo a positivo) su `measureSamples`, per stimare la fondamentale senza
    dipendere dalla stessa formula usata internamente dal motore (altrimenti il test non
    proverebbe niente: userebbe la formula sbagliata per verificare se stessa). */
float measureFundamentalHz (engine::SynthEngine& synth, double sampleRate, int settleSamples, int measureSamples)
{
    juce::MidiBuffer noMidi;
    float prevSample = 0.0f;
    int consumed = 0;

    while (consumed < settleSamples)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, noMidi);
        consumed += buffer.getNumSamples();
        prevSample = buffer.getSample (0, buffer.getNumSamples() - 1);
    }

    int crossings = 0;
    int measured = 0;

    while (measured < measureSamples)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, noMidi);
        const auto* data = buffer.getReadPointer (0);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (prevSample < 0.0f && data[i] >= 0.0f)
                ++crossings;
            prevSample = data[i];
        }

        measured += buffer.getNumSamples();
    }

    return measured > 0 ? (float) crossings * (float) sampleRate / (float) measured : 0.0f;
}
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

        beginTest ("oct/semi: il valore normalizzato si arrotonda, non si tronca");
        {
            // Combinazioni segnalate dalla review come sbagliate con un cast troncante
            // ((int) invece di roundToInt): semi -4,-1,+2,+5,+8 e oct -1,+2, piu' qualche
            // controllo di cornice.
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

            // Bound generoso ma non arbitrario: con headroom -20 dB/voce il caso peggiore (nessuna
            // cancellazione di fase fra le 16 voci) misurato a mano arriva a circa -4.2 dBFS
            // (~0.62 lineare); 1.0 lascia comodo margine senza nascondere un vero clipping.
            const auto peak = renderPeak (synth, 50, *this);
            expect (peak < 1.0f, "picco " + juce::String (peak) + ": la somma delle voci deve restare sotto 0 dBFS");

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

                // Bound largo ma non arbitrario: Q=20 su due stadi in cascata puo' legittimamente
                // risuonare parecchio (StateVariableFilterTests tollera fino a 100x l'ingresso per
                // un singolo stadio), moltiplicato per l'headroom di voce (0.1x) e il guadagno di
                // pan (~0.7x) da' un ordine di grandezza intorno a 10. Misurato: il picco peggiore
                // di tutte le 256 varianti e' 6.61 (variante 63: risonanza+2 stadi+cutoff alto+
                // drive+ottava estrema tutti insieme). Non e' silenzio ne' un timbro "giusto",
                // e' solo "non e' esploso".
                const auto peak = renderPeak (synth, 4, *this);
                expect (peak < 10.0f, "variante " + juce::String (variant) + ": picco " + juce::String (peak));

                midi.clear();
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, midi);
                renderPeak (synth, 4, *this); // scarica la coda prima della prossima variante
            }
        }
    }
};

static EngineRobustnessTests engineRobustnessTests;

// --- Task 8, Ruling C: la stessa mappatura di PluginProcessor::collectParams, ma esercitata
// direttamente su params::collectEngineParams (ParamCollect.h) con un accessor finto. A
// differenza dei due test di tuning sopra (che ricostruiscono l'aritmetica di Map::Linear a
// mano, perche' non potevano linkare juce_audio_processors), questo chiama il vero codice di
// produzione: se il cast troncante tornasse, questo test lo becca, gli altri due no.
struct ParamCollectTests final : juce::UnitTest
{
    ParamCollectTests() : juce::UnitTest ("collectEngineParams", "engine") {}

    /** Accessor finto: id -> valore normalizzato 0..1. Non e' codice del thread audio (e'
        codice di test), quindi std::map va benissimo qui. */
    struct FakeRaw
    {
        // Chiave params::ParamSlot, non stringa: e' esattamente cio' che collectEngineParams
        // passa all'accessore ora (vedi ParamCollect.h), quindi il finto qui rispecchia il vero
        // PluginProcessor::paramSlots_[(size_t) slot].
        std::map<params::ParamSlot, float> values;

        float operator() (params::ParamSlot slot) const
        {
            const auto it = values.find (slot);
            return it != values.end() ? it->second : 0.0f;
        }
    };

    /** Normalizzato che, passato a Map::Linear, ridà `real`. */
    static float rawForLinear (const char* id, float real)
    {
        const auto* spec = params::find (id);
        return (real - spec->min) / (spec->max - spec->min);
    }

    /** Normalizzato che, passato a Map::MsSquared, ridà `realMs`. */
    static float rawForMsSquared (const char* id, float realMs)
    {
        const auto* spec = params::find (id);
        return std::sqrt ((realMs - spec->min) / (spec->max - spec->min));
    }

    void runTest() override
    {
        beginTest ("oct/semi: il valore normalizzato si arrotonda, non si tronca (sito reale)");
        {
            // Le combinazioni segnalate dalla review come sbagliate con un cast troncante
            // ((int) invece di roundToInt): semi -4,-1,+2,+5,+8 e oct -1,+2.
            const int semiCases[] = { -4, -1, 2, 5, 8 };
            for (auto expected : semiCases)
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::semi] = rawForLinear ("semi", (float) expected);
                const auto p = params::collectEngineParams (raw);
                expectEquals (p.semitones, expected, "semi " + juce::String (expected));
            }

            const int octCases[] = { -1, 2 };
            for (auto expected : octCases)
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::oct] = rawForLinear ("oct", (float) expected);
                const auto p = params::collectEngineParams (raw);
                expectEquals (p.octave, expected, "oct " + juce::String (expected));
            }
        }

        beginTest ("att/dec/rel: da ms denormalizzati a secondi per il motore");
        {
            FakeRaw raw;
            raw.values[params::ParamSlot::att] = rawForMsSquared ("att", 500.0f);
            raw.values[params::ParamSlot::dec] = rawForMsSquared ("dec", 1000.0f);
            raw.values[params::ParamSlot::rel] = rawForMsSquared ("rel", 2000.0f);
            const auto p = params::collectEngineParams (raw);

            expectWithinAbsoluteError (p.attackSeconds, 0.5f, 1.0e-4f);
            expectWithinAbsoluteError (p.decaySeconds, 1.0f, 1.0e-4f);
            expectWithinAbsoluteError (p.releaseSeconds, 2.0f, 1.0e-4f);
        }

        beginTest ("sus/res/keytrk/envVel: dalla percentuale alla frazione 0..1");
        {
            FakeRaw raw;
            raw.values[params::ParamSlot::sus] = rawForLinear ("sus", 70.0f);
            raw.values[params::ParamSlot::res] = rawForLinear ("res", 40.0f);
            raw.values[params::ParamSlot::keytrk] = rawForLinear ("keytrk", 25.0f);
            raw.values[params::ParamSlot::envVel] = rawForLinear ("envVel", 60.0f);
            const auto p = params::collectEngineParams (raw);

            expectWithinAbsoluteError (p.sustain, 0.7f, 1.0e-5f);
            // res passa anche per il jmap 0.707..20 dopo la denormalizzazione: si verifica la
            // formula intera, non solo denormalise().
            expectWithinAbsoluteError (p.resonanceQ, juce::jmap (0.4f, 0.707f, 20.0f), 1.0e-4f);
            expectWithinAbsoluteError (p.keyTrack, 0.25f, 1.0e-5f);
            expectWithinAbsoluteError (p.velocityAmount, 0.6f, 1.0e-5f);
        }

        beginTest ("pan: da -50..+50 a -1..+1");
        {
            for (float target : { -50.0f, 0.0f, 25.0f, 50.0f })
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::pan] = rawForLinear ("pan", target);
                const auto p = params::collectEngineParams (raw);
                expectWithinAbsoluteError (p.pan, target * 0.02f, 1.0e-5f);
            }
        }

        beginTest ("level: il valore grezzo della mappa Db e' gia' il guadagno lineare");
        {
            // Se collectEngineParams chiamasse per errore denormalise() su level (che ha
            // Map::Db), un x=0.37 diventerebbe circa -8.6 dB invece di restare 0.37: questo test
            // lo becca. (volume ha la stessa proprieta' ma non passa da qui: il guadagno master
            // si calcola altrove, in PluginProcessor::processBlock, non toccato da questo refactor.)
            for (float target : { 0.0f, 0.37f, 1.0f })
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::level] = target;
                const auto p = params::collectEngineParams (raw);
                expectWithinAbsoluteError (p.level, target, 1.0e-6f);
            }
        }
    }
};

static ParamCollectTests paramCollectTests;
