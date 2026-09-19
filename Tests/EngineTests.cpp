#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "parameters/ParamCollect.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <map>
#include <vector>

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

            // Sedici note tenute a fondo scala sono il caso estremo: con headroom -8 dB/voce
            // la loro somma supera il fondo scala, ed e' esattamente il lavoro del soft
            // clipper in SynthEngine::process tenerla dentro. Quello che si verifica qui e'
            // il suo contratto: mai oltre 0 dBFS, mai un campione non finito (renderPeak
            // controlla isfinite su ognuno). Che un accordo *normale* resti sotto la soglia
            // del clipper, cioe' che non ci sia distorsione nell'uso reale, e' il test
            // successivo.
            const auto peak = renderPeak (synth, 50, *this);
            expect (peak <= 1.0f, "picco " + juce::String (peak) + ": il soft clipper deve tenere l'uscita entro 0 dBFS");

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
            // a fondo scala) venga contenuto; qui che l'uso *normale* non lo sfiori nemmeno,
            // cioe' che il soft clipper sia una rete di sicurezza e non un compressore sempre
            // acceso. La soglia del clipper e' 0.8: sotto quella l'uscita e' bit-identica a
            // quella non clippata.
            //
            // Le soglie inferiori sono l'altra meta' del problema: con l'headroom precedente
            // (-20 dB per voce) una nota singola usciva a -26 dBFS, cioe' uno strumento
            // inutilizzabilmente piano, e il difetto non veniva preso da nessun test.
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
            expect (singleNote < 0.8f, "una nota sola non deve arrivare al soft clipper, picco "
                                           + juce::String (singleNote));
            expect (singleNote > 0.1f, "una nota sola non deve essere inudibile, picco "
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
            expect (chord < 0.8f, "un accordo di quattro note non deve arrivare al soft clipper, picco "
                                      + juce::String (chord));

            const auto dbfs = [] (float peak) { return juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS"; };
            logMessage ("nota singola: " + dbfs (singleNote) + " | accordo di 4: " + dbfs (chord));
        }

        beginTest ("il soft clipper d'uscita non tocca il segnale sotto soglia");
        {
            // Che sia davvero trasparente sotto 0.8 e' la proprieta' che rende accettabile
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

            expect (loud < 0.8f, "il test ha senso solo se entrambi restano sotto soglia, picco " + juce::String (loud));
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
            // res passa anche per la mappa esponenziale 0.707..12 dopo la denormalizzazione:
            // si verifica la formula intera, non solo denormalise(). Esponenziale e non
            // lineare: con la vecchia jmap, res 40 % dava gia' Q 8.4 (+18 dB di picco).
            const auto expectedQ = dsp::StateVariableFilter::kButterworthQ
                                       * std::pow (12.0f / dsp::StateVariableFilter::kButterworthQ, 0.4f);
            expectWithinAbsoluteError (p.resonanceQ, expectedQ, 1.0e-4f);
            expect (expectedQ < 2.5f, "a res 40 % la risonanza deve essere ancora moderata, Q "
                                          + juce::String (expectedQ));
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

/**
 * Le conversioni da valore normalizzato a valore reale sono state estratte da
 * collectEngineParams in funzioni proprie (params::cutoffHzFromRaw e compagne) perche' il
 * percorso modulato deve usare le stesse: questo test e' il guardiano che le tiene allineate.
 * Se qualcuno cambia una formula in un posto solo, qui si spacca.
 */
struct ParamConversionTests final : juce::UnitTest
{
    ParamConversionTests() : juce::UnitTest ("conversioni dei parametri", "params") {}

    void runTest() override
    {
        beginTest ("collectEngineParams riempie le basi normalizzate dei target modulabili");
        {
            // Ogni slot ritorna un valore diverso, cosi' uno scambio fra due target si vede.
            const auto rawFor = [] (params::ParamSlot slot) noexcept
            {
                switch (slot)
                {
                    case params::ParamSlot::cutoff: return 0.11f;
                    case params::ParamSlot::res:    return 0.22f;
                    case params::ParamSlot::wtpos:  return 0.33f;
                    case params::ParamSlot::level:  return 0.44f;
                    case params::ParamSlot::pan:    return 0.55f;
                    case params::ParamSlot::fine:   return 0.66f;
                    case params::ParamSlot::drive:  return 0.77f;
                    default:                        return 0.5f;
                }
            };

            const auto p = params::collectEngineParams (rawFor);

            const auto base = [&p] (params::ParamSlot slot)
            {
                return p.modBase[(size_t) engine::modTargetIndexFor (slot)];
            };

            expectWithinAbsoluteError (base (params::ParamSlot::cutoff), 0.11f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::res), 0.22f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::wtpos), 0.33f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::level), 0.44f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::pan), 0.55f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::fine), 0.66f, 1.0e-6f);
            expectWithinAbsoluteError (base (params::ParamSlot::drive), 0.77f, 1.0e-6f);

            // I campi denormalizzati restano quelli di sempre: le basi si aggiungono, non sostituiscono.
            expectWithinAbsoluteError (p.cutoffHz, params::cutoffHzFromRaw (0.11f), 1.0e-2f);
        }

        beginTest ("i parametri dell'LFO arrivano grezzi in EngineParams");
        {
            const auto rawFor = [] (params::ParamSlot slot) noexcept
            {
                switch (slot)
                {
                    case params::ParamSlot::lshape:  return 2.0f;  // choice: il grezzo e' gia' l'indice
                    case params::ParamSlot::lrate:   return 0.45f;
                    case params::ParamSlot::lsync:   return 1.0f;
                    case params::ParamSlot::lphase:  return 0.25f;
                    case params::ParamSlot::lfade:   return 0.5f;
                    case params::ParamSlot::lretrig: return 0.0f;
                    default:                         return 0.0f;
                }
            };

            const auto p = params::collectEngineParams (rawFor);

            expectEquals (p.lfoShapeIndex, 2);
            expectWithinAbsoluteError (p.lfoRateRaw, 0.45f, 1.0e-6f);
            expect (p.lfoSync);
            // lphase e' 0..360 gradi nella tabella: in EngineParams diventa 0..1.
            expectWithinAbsoluteError (p.lfoPhaseOffset01, 0.25f, 1.0e-6f);
            // lfade e' 0..4000 ms: in EngineParams diventa secondi.
            expectWithinAbsoluteError (p.lfoFadeSeconds, 2.0f, 1.0e-3f);
            expect (! p.lfoRetrig);
        }

        beginTest ("le conversioni estratte coincidono con quelle di collectEngineParams");
        {
            // Griglia fitta: una divergenza anche solo agli estremi della corsa si vede.
            for (int i = 0; i <= 20; ++i)
            {
                const auto raw = (float) i / 20.0f;

                const auto p = params::collectEngineParams (
                    [raw] (params::ParamSlot) noexcept { return raw; });

                expectWithinAbsoluteError (params::cutoffHzFromRaw (raw), p.cutoffHz, 1.0e-2f);
                expectWithinAbsoluteError (params::resonanceQFromRaw (raw), p.resonanceQ, 1.0e-4f);
                expectWithinAbsoluteError (params::framePositionFromRaw (raw), p.framePosition, 1.0e-6f);
                expectWithinAbsoluteError (params::levelGainFromRaw (raw), p.level, 1.0e-6f);
                expectWithinAbsoluteError (params::panFromRaw (raw), p.pan, 1.0e-6f);
                expectWithinAbsoluteError (params::fineCentsFromRaw (raw), p.fineCents, 1.0e-3f);
                expectWithinAbsoluteError (params::driveGainFromRaw (raw), p.driveGain, 1.0e-4f);
            }
        }
    }
};

static ParamConversionTests paramConversionTests;

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
    }
};

static ModulationVoiceTests modulationVoiceTests;
