#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "parameters/ParamCollect.h"
#include "parameters/ParameterTable.h"
#include "parameters/StateTree.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
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

            // Sedici note tenute a fondo scala sono il caso estremo: con headroom -3 dB/voce
            // la loro somma presenta 2.11 al soft clipper (+6.5 dBFS), ed e' esattamente il
            // lavoro del soft clipper in SynthEngine::process tenerla dentro. Quello che si
            // verifica qui e' il suo contratto: mai oltre 0 dBFS, mai un campione non finito
            // (renderPeak controlla isfinite su ognuno). Che un accordo *normale* resti sotto
            // la soglia del clipper, cioe' che non ci sia distorsione nell'uso reale, e' il
            // test successivo.
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
            // a fondo scala) venga contenuto; qui che l'uso *normale* non arrivi al clipper,
            // cioe' che il soft clipper sia una rete di sicurezza e non un compressore sempre
            // acceso. La soglia del clipper e' 0.95: sotto quella l'uscita e' bit-identica a
            // quella non clippata.
            //
            // Le soglie inferiori sono l'altra meta' del problema: con l'headroom precedente
            // (-20 dB per voce) una nota singola usciva a -26 dBFS, cioe' uno strumento
            // inutilizzabilmente piano, e il difetto non veniva preso da nessun test.
            //
            // I numeri qui sotto sono il bilancio esatto della ritaratura di kVoiceHeadroomGain
            // (da -8 a -3 dB): sono loro a fissare quel valore, non il contrario. L'accordo
            // arriva a 0.900 contro una soglia di 0.95, cioe' mezzo decibel di margine — ed e'
            // tutto il margine che c'e', perche' l'headroom e' stato scelto proprio sul punto in
            // cui l'accordo sfiora il clipper. Se un domani questo test fallisse di un soffio, la
            // risposta e' abbassare kVoiceHeadroomGain, non alzare la soglia in SynthEngine.
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

            // 0.25 e non piu' 0.1: con -3 dB per voce una nota singola a volume 0.8 misura 0.363
            // (-8.8 dBFS) su questa finestra, contro 0.204 (-13.8 dBFS) con l'headroom di prima.
            // La vecchia soglia di 0.1 sarebbe rimasta vera anche tornando ai -8 dB, quindi non
            // avrebbe visto la regressione: 0.25 sta in mezzo ai due livelli ed e' cio' che rende
            // il test capace di accorgersi di un arretramento, che e' il difetto che questa
            // ritaratura e' venuta a correggere.
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

            // Quanto sta sopra l'accordo rispetto alla nota singola e' la quantita' che decide
            // tutto il gain staging: e' il fattore di cresta a rendere impossibile portare la
            // nota singola a -14 dBFS RMS *e* tenere l'accordo fuori dal clipper (vedi il
            // commento di kVoiceHeadroomGain). Misurato 7.9 dB su questa finestra, che include
            // l'attacco; la banda +-1.5 dB lascia respirare la misura senza lasciar passare uno
            // spostamento vero fra i due livelli.
            const auto crestDb = juce::Decibels::gainToDecibels (chord / singleNote);
            expect (crestDb > 6.4f && crestDb < 9.4f,
                    "accordo e nota singola distano " + juce::String (crestDb, 2)
                        + " dB invece dei 7.9 misurati");

            const auto dbfs = [] (float peak) { return juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS"; };
            logMessage ("nota singola: " + dbfs (singleNote) + " | accordo di 4: " + dbfs (chord));
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
    }
};

static EngineRobustnessTests engineRobustnessTests;

/**
 * `engine::saturate` misurata da sola, fuori dal motore.
 *
 * E' l'unica nonlinearita' della catena, quindi e' l'unica cosa che puo' produrre alias: ogni
 * altro stadio (filtro, inviluppo, pan, somma delle voci) e' lineare tempo-variante e non crea
 * frequenze che non c'erano. Misurarla attraverso SynthEngine significherebbe misurare anche
 * l'interpolazione della wavetable, che ha un pavimento suo a circa -73 dB: sessanta decibel
 * sopra a quello che c'e' da guardare qui. Da sola, invece, il pavimento della misura sta a
 * -185 dB e la curva si vede per quello che e'.
 */
struct SaturationCurveTests final : juce::UnitTest
{
    SaturationCurveTests() : juce::UnitTest ("saturate", "engine") {}

    /**
     * Blackman-Harris a 7 termini. La finestra **non** e' un dettaglio di comodo, e' la
     * differenza fra un test che misura e uno che non misura niente: con una finestra di Hann
     * la dispersione dei lobi laterali mette un pavimento a circa -44 dB, cioe' trenta decibel
     * sopra l'alias della curva vecchia e centodieci sopra quello della nuova — tutte le curve
     * risulterebbero identiche e perfette. Con questi sette coefficienti il primo lobo laterale
     * sta a -180 dB e quello che si legge e' il segnale, non la finestra.
     */
    static constexpr double kBlackmanHarris7[7] = {
        0.27105140069342, -0.43329793923448, 0.21812299954311, -0.06592544638803,
        0.01081174209837, -0.00077658482522, 0.00001388721735
    };

    /** 65536 punti a 48 kHz: 0.73 Hz per bin, e una potenza di due, che serve sotto. */
    static constexpr size_t kWindowSize = 65536;

    /**
     * Tabelle precalcolate per una DFT valutata solo dove serve. Il seno di prova si genera
     * **dalla stessa tabella** con cui poi lo si misura, indicizzata modulo N: cosi' il segnale
     * e' periodico di periodo N *esattamente*, senza l'errore di riduzione d'argomento che
     * std::sin accumula su k*i grandi. E' quell'errore, non la curva, a fissare il pavimento
     * di misura se lo si lascia entrare: settanta decibel di differenza.
     */
    struct Tables
    {
        std::vector<double> window, cosine, sine;

        Tables()
        {
            window.resize (kWindowSize);
            cosine.resize (kWindowSize);
            sine.resize (kWindowSize);

            for (size_t i = 0; i < kWindowSize; ++i)
            {
                const auto t = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) kWindowSize;
                double w = 0.0;

                for (int m = 0; m < 7; ++m)
                    w += kBlackmanHarris7[(size_t) m] * std::cos ((double) m * t);

                window[i] = w;
                cosine[i] = std::cos (t);
                sine[i] = std::sin (t);
            }
        }

        /** Ampiezza al bin intero `bin`. L'indice del fasore avanza di `bin` e si avvolge:
            nessuna chiamata trigonometrica nel ciclo, nessun errore che cresca con i. */
        double magnitudeAtBin (const std::vector<double>& windowed, size_t bin) const
        {
            double re = 0.0, im = 0.0;
            size_t index = 0;

            for (size_t i = 0; i < kWindowSize; ++i)
            {
                re += windowed[i] * cosine[index];
                im -= windowed[i] * sine[index];
                index += bin;

                if (index >= kWindowSize)
                    index -= kWindowSize;
            }

            return std::sqrt (re * re + im * im) / (double) kWindowSize;
        }
    };

    struct AliasMeasurement
    {
        double aliasDb;   ///< il partial ripiegato piu' forte, rispetto alla fondamentale
        double floorDb;   ///< quanto misura la stessa DFT dove non c'e' niente
        int worstPartial; ///< l'ordine dell'armonica che ha prodotto quell'alias
    };

    /**
     * Rapporto alias/fondamentale di un seno di ampiezza 1 moltiplicato per `driveDb` e passato
     * per la curva, valutato direttamente a 48 kHz senza oversampling — cioe' esattamente come
     * gira nel motore.
     *
     * Il bin della fondamentale si sceglie **dispari**: con N potenza di due e k dispari,
     * gcd(k, N) = 1, quindi nessun partial di ordine m puo' ripiegarsi sopra il bin di un altro
     * partial di ordine m' (servirebbe (m +- m') multiplo di N). Senza quella precauzione la
     * misura confonderebbe un alias con un'armonica legittima e leggerebbe numeri a caso.
     *
     * `useDouble` sceglie fra la funzione di produzione (float, quella che gira davvero) e la
     * stessa formula in doppia precisione. Le due coincidono finche' l'alias sta sopra il
     * pavimento dei float, circa -185 dB; sotto, il float misura il proprio arrotondamento.
     */
    static AliasMeasurement measureAlias (const Tables& tables, int midiNote, double driveDb, bool useDouble)
    {
        constexpr double sampleRate = 48000.0;
        const auto fundamentalHz = 440.0 * std::pow (2.0, ((double) midiNote - 69.0) / 12.0);

        auto k = (size_t) juce::roundToInt (fundamentalHz * (double) kWindowSize / sampleRate);

        if ((k % 2) == 0)
            ++k;

        const auto drive = std::pow (10.0, driveDb / 20.0);

        std::vector<double> windowed (kWindowSize);

        for (size_t i = 0; i < kWindowSize; ++i)
        {
            const auto x = drive * tables.sine[(k * i) % kWindowSize];
            const auto y = useDouble ? engine::saturateCurve (x)
                                     : (double) engine::saturate ((float) x);
            windowed[i] = tables.window[i] * y;
        }

        const auto fundamental = tables.magnitudeAtBin (windowed, k);

        // I partial dispari (la curva e' dispari: quelli pari non esistono) fino al 201esimo.
        // Quelli sotto Nyquist sono armoniche vere e non contano; quelli sopra si ripiegano, ed
        // e' la loro somma a essere l'inarmonicita' che si sente.
        double worst = 0.0;
        int worstPartial = 0;

        for (int m = 3; m <= 201; m += 2)
        {
            const auto position = (size_t) m * k;

            if (position < kWindowSize / 2)
                continue;

            auto bin = position % kWindowSize;

            if (bin > kWindowSize / 2)
                bin = kWindowSize - bin;

            if (bin == k || bin == 0)
                continue;

            const auto amplitude = tables.magnitudeAtBin (windowed, bin);

            if (amplitude > worst)
            {
                worst = amplitude;
                worstPartial = m;
            }
        }

        // Il pavimento: bin **pari**, dove per costruzione non puo' cadere nessun partial (k e'
        // dispari e m e' dispari, quindi m*k e' dispari, e il ripiegamento di un indice dispari
        // modulo una potenza di due resta dispari). Se questo numero non stesse molto sotto la
        // soglia che il test pretende, il test non starebbe misurando la curva ma se stesso.
        double floor = 0.0;

        for (const size_t bin : { kWindowSize / 4, kWindowSize / 4 + 2, kWindowSize / 2 - 4,
                                  (size_t) 1000, (size_t) 20002 })
            floor = juce::jmax (floor, tables.magnitudeAtBin (windowed, bin));

        AliasMeasurement result;
        result.aliasDb = 20.0 * std::log10 (worst / fundamental);
        result.floorDb = 20.0 * std::log10 (floor / fundamental);
        result.worstPartial = worstPartial;
        return result;
    }

    void runTest() override
    {
        beginTest ("la curva e' dispari, monotona e non esce mai da +-1");
        {
            // f(0) = 0 esatto, non "vicino a zero": e' cio' che garantisce che il silenzio
            // resti silenzio quando drive e' acceso, e che un DC offset non nasca dal nulla.
            expectEquals (engine::saturate (0.0f), 0.0f);

            // Dispari, e come uguaglianza **esatta** in float: la formula nega solo il
            // numeratore (x2 e il denominatore non cambiano) e IEEE garantisce (-a)/b == -(a/b).
            // Se un domani qualcuno introducesse un termine pari — una polarizzazione, un
            // arrotondamento asimmetrico — la seconda armonica comparirebbe e questo lo prende.
            for (float x = 0.0f; x <= 8.0f; x += 0.0009765625f) // passo 2^-10: esatto in binario
                expectEquals (engine::saturate (-x), -engine::saturate (x),
                              "simmetria a x = " + juce::String (x));

            // Monotona crescente su tutta la corsa utile e oltre il ginocchio: una curva che
            // ripiegasse (come farebbe la cubica senza clamp sopra 1.5) produrrebbe un
            // wavefolder, non un saturatore. In esatta, la derivata e'
            // f'(x) = 9(x^2-9)^2 / (27+9x^2)^2, un quadrato: non negativa ovunque, quindi la
            // monotonia e' una proprieta' della formula e qui si chiede **stretta**.
            {
                double previous = engine::saturateCurve (-8.0);

                for (double x = -8.0; x <= 8.0; x += 0.0009765625) // passo 2^-10: esatto in binario
                {
                    const auto y = engine::saturateCurve (x);
                    expect (y >= previous, "non monotona in doppia precisione a x = " + juce::String (x));
                    previous = y;
                }
            }

            // In float la stessa cosa, ma con un ULP di tolleranza, ed e' una concessione
            // all'aritmetica, non alla curva: vicino al ginocchio la pendenza vera scende sotto
            // 1e-4, quindi fra due punti adiacenti la funzione cresce di meno di quanto valga
            // l'ultimo bit del risultato, e l'arrotondamento della divisione puo' far scendere il
            // valore di un ULP. Misurato: esattamente 5.96e-8, cioe' un ULP a 1.0, a x = -2.986.
            // Quattro ULP di soglia lasciano passare quello e fermano qualunque ripiegamento
            // vero, che sarebbe milioni di volte piu' grande.
            {
                constexpr float tolerance = 4.0f * 5.9604645e-8f;
                float previous = engine::saturate (-8.0f);
                float worstDrop = 0.0f;

                for (float x = -8.0f; x <= 8.0f; x += 0.0009765625f)
                {
                    const auto y = engine::saturate (x);
                    worstDrop = juce::jmax (worstDrop, previous - y);
                    expect (y >= previous - tolerance, "non monotona a x = " + juce::String (x));
                    previous = y;
                }

                logMessage ("monotonia in float: calo massimo fra due punti adiacenti "
                            + juce::String (worstDrop, 12) + " (un ULP vale 5.96e-8)");
            }

            // Limitata, compresi i casi che un mod matrix impazzito o un denormale possono
            // davvero produrre. L'infinito e' il caso che distingue la forma a rami
            // (if x > 3 -> 1) da un clamp applicato al risultato: quest'ultimo calcolerebbe
            // inf/inf = NaN e lo lascerebbe passare, perche' ogni confronto con NaN e' falso.
            for (const float x : { 0.5f, 1.0f, 2.0f, 3.0f, 10.0f, 1.0e9f, 1.0e30f,
                                   std::numeric_limits<float>::infinity() })
            {
                expect (std::abs (engine::saturate (x)) <= 1.0f, "|f(x)| > 1 a x = " + juce::String (x));
                expect (std::abs (engine::saturate (-x)) <= 1.0f, "|f(-x)| > 1 a x = " + juce::String (x));
                expect (std::isfinite (engine::saturate (x)), "f(x) non finita a x = " + juce::String (x));
                expect (std::isfinite (engine::saturate (-x)), "f(-x) non finita a x = " + juce::String (x));
            }
        }

        beginTest ("il raccordo con il tratto piatto e' continuo in derivata prima e seconda");
        {
            // Il difetto della curva precedente, e la ragione del cambio. `clamp(x, +-1.5)` +
            // cubica raccorda con derivata prima nulla (C1) ma lascia la derivata seconda che
            // salta da -1.333 a 0: uno spigolo che la serie di Fourier paga con armoniche che
            // decadono come 1/n^3 invece che esponenzialmente. Il Pade di tanh ha invece una
            // radice **tripla** del denominatore in 3 (x^3-9x^2+27x-27 = (x-3)^3), quindi
            // f(3)=1, f'(3)=0 e f''(3)=0 tutte insieme.
            //
            // Si misura in doppia precisione perche' una differenza seconda divisa per h^2 con
            // h = 1e-3 amplifica l'errore di un milione di volte: in float il rumore di
            // arrotondamento coprirebbe il salto che si vuole vedere.
            constexpr double h = 1.0e-3;

            double maxFirstJump = 0.0, maxSecondJump = 0.0;
            double previousFirst = 0.0, previousSecond = 0.0;
            bool first = true;

            for (double x = -6.0; x <= 6.0; x += h)
            {
                const auto f = [] (double v) { return engine::saturateCurve (v); };
                const auto d1 = (f (x + h) - f (x - h)) / (2.0 * h);
                const auto d2 = (f (x + h) - 2.0 * f (x) + f (x - h)) / (h * h);

                if (! first)
                {
                    maxFirstJump = juce::jmax (maxFirstJump, std::abs (d1 - previousFirst));
                    maxSecondJump = juce::jmax (maxSecondJump, std::abs (d2 - previousSecond));
                }

                previousFirst = d1;
                previousSecond = d2;
                first = false;
            }

            // Le soglie non sono arrotondamenti prudenti, sono due popolazioni separate da tre
            // ordini di grandezza. Misurato: f' salta al massimo di 7.5e-4 fra due punti
            // adiacenti (con la curva vecchia 1.3e-3, anche lei continua), f'' di 1.8e-3 —
            // contro i 6.7e-1 della curva vecchia, che e' lo spigolo. Una curva che tornasse
            // C1-e-basta finirebbe di nuovo a ~1e-1 e questo test la fermerebbe.
            expect (maxFirstJump < 5.0e-3,
                    "la derivata prima salta di " + juce::String (maxFirstJump, 6));
            expect (maxSecondJump < 2.0e-2,
                    "la derivata seconda salta di " + juce::String (maxSecondJump, 6)
                        + ": il raccordo e' solo C1");

            logMessage ("salto massimo fra punti adiacenti: f' " + juce::String (maxFirstJump, 6)
                        + ", f'' " + juce::String (maxSecondJump, 6));
        }

        beginTest ("a MIDI 84 e drive 6 dB l'alias resta sotto -120 dB dalla fondamentale");
        {
            const Tables tables;

            struct Case { int note; double driveDb; double limitDb; };

            // I limiti sono il criterio, non la misura: stanno venti-trenta decibel sopra il
            // numero misurato con questa curva e molto sotto quello della curva precedente,
            // che e' riportato accanto. Un test che chiedesse esattamente il valore misurato
            // fallirebbe al primo cambio di compilatore; uno che chiedesse -40 dB sarebbe
            // passato anche prima.
            const Case cases[] = {
                { 60, 6.0, -160.0 },  // prima: -111.7
                { 84, 4.8, -140.0 },  // prima:  -85.2
                { 84, 6.0, -120.0 },  // prima:  -75.5   <- il criterio del brief
                { 84, 12.0, -78.0 },  // prima:  -57.3
            };

            // A MIDI 60 il limite non e' -160 perche' la curva non sappia fare meglio: in doppia
            // precisione la stessa misura da' -334 dB. E' che il percorso di produzione e' in
            // float, e a quel punto quello che la DFT legge non e' piu' l'alias della curva ma
            // l'arrotondamento dei float, che sta attorno a -180 dB e non scende oltre. Il
            // numero double, che il log riporta accanto, e' la proprieta' della curva; quello
            // float e' la proprieta' del motore. Il limite si riferisce al secondo.

            for (const auto& c : cases)
            {
                const auto measured = measureAlias (tables, c.note, c.driveDb, false);
                const auto exact = measureAlias (tables, c.note, c.driveDb, true);

                // Prima di credere alla misura: la stessa DFT, letta dove non c'e' niente, deve
                // stare a -170 dB o meno. E' il controllo che con una finestra di Hann
                // (pavimento a -44 dB) fallirebbe subito, invece di lasciar passare un test che
                // non misura piu' niente.
                expect (measured.floorDb < -170.0,
                        "MIDI " + juce::String (c.note) + ": il pavimento della misura e' a "
                            + juce::String (measured.floorDb, 1)
                            + " dB invece dei -185 attesi; la misura non prova piu' niente");

                expect (measured.aliasDb < c.limitDb,
                        "MIDI " + juce::String (c.note) + " drive " + juce::String (c.driveDb, 1)
                            + " dB: alias a " + juce::String (measured.aliasDb, 1)
                            + " dB, sopra il limite di " + juce::String (c.limitDb, 1));

                logMessage ("MIDI " + juce::String (c.note) + " drive " + juce::String (c.driveDb, 1)
                            + " dB -> alias " + juce::String (measured.aliasDb, 1) + " dB (float), "
                            + juce::String (exact.aliasDb, 1) + " dB (double), partial "
                            + juce::String (measured.worstPartial) + ", pavimento "
                            + juce::String (measured.floorDb, 1) + " dB");
            }

            // Il contrappeso onesto: sopra MIDI ~100 la fondamentale e' a 4 kHz, sotto Nyquist
            // ci stanno cinque armoniche e **qualunque** saturazione alias. Non e' una
            // regressione da correggere, e' il limite fisico della valutazione diretta; e
            // pretendere qui una soglia bassa vorrebbe dire pretendere l'oversampling, che e'
            // una decisione diversa e non e' stata presa. Il test lo fissa come fatto noto,
            // cosi' nessuno lo riscopre come se fosse un bug.
            const auto high = measureAlias (tables, 108, 6.0, false);
            expect (high.aliasDb > -50.0 && high.aliasDb < -35.0,
                    "MIDI 108: alias a " + juce::String (high.aliasDb, 1)
                        + " dB, atteso circa -43 (prima: -43.4, le curve pareggiano)");
            logMessage ("MIDI 108 drive 6.0 dB -> alias " + juce::String (high.aliasDb, 1)
                        + " dB: qui nessuna curva puo' fare meglio senza oversampling");
        }
    }
};

static SaturationCurveTests saturationCurveTests;

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
        beginTest ("oct/semi arrivano in unita' naturali, non normalizzate");
        {
            // Questo test alimentava l'accessore finto con valori *normalizzati*, ed e' quella
            // assunzione ad aver nascosto per mesi il difetto vero: oct e semi sono gli unici
            // Kind::Int, creati da ParameterMapping.h come juce::AudioParameterInt nel loro
            // range naturale, quindi getRawParameterValue restituisce -3..3 e -12..12, non
            // 0..1. Denormalizzarli dava -3 ottave e -12 semitoni al default, cioe' ogni nota
            // quattro ottave sotto il tasto premuto. La verifica contro l'APVTS vero sta in
            // Tests/ParameterSeamTests.cpp; qui si blocca l'aritmetica.
            const int semiCases[] = { -12, -4, -1, 0, 2, 5, 8, 12 };
            for (auto expected : semiCases)
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::semi] = (float) expected;
                const auto p = params::collectEngineParams (raw);
                expectEquals (p.semitones, expected, "semi " + juce::String (expected));
            }

            const int octCases[] = { -3, -1, 0, 2, 3 };
            for (auto expected : octCases)
            {
                FakeRaw raw;
                raw.values[params::ParamSlot::oct] = (float) expected;
                const auto p = params::collectEngineParams (raw);
                expectEquals (p.octave, expected, "oct " + juce::String (expected));
            }

            // Il float che arriva puo' cadere appena sotto l'intero per arrotondamento: e' il
            // caso che rende roundToInt necessario al posto di un cast troncante.
            FakeRaw nearlyEight;
            nearlyEight.values[params::ParamSlot::semi] = 7.999998f;
            expectEquals (params::collectEngineParams (nearlyEight).semitones, 8);
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

        beginTest ("att2/dec2/sus2/rel2: il secondo inviluppo arriva in secondi e frazione");
        {
            FakeRaw raw;
            raw.values[params::ParamSlot::att2] = rawForMsSquared ("att2", 250.0f);
            raw.values[params::ParamSlot::dec2] = rawForMsSquared ("dec2", 750.0f);
            raw.values[params::ParamSlot::sus2] = rawForLinear ("sus2", 30.0f);
            raw.values[params::ParamSlot::rel2] = rawForMsSquared ("rel2", 1500.0f);
            const auto p = params::collectEngineParams (raw);

            expectWithinAbsoluteError (p.attack2Seconds, 0.25f, 1.0e-4f);
            expectWithinAbsoluteError (p.decay2Seconds, 0.75f, 1.0e-4f);
            expectWithinAbsoluteError (p.sustain2, 0.3f, 1.0e-5f);
            expectWithinAbsoluteError (p.release2Seconds, 1.5f, 1.0e-4f);
        }

        beginTest ("env2 riusa mappe, range e default dei quattro di env");
        {
            // Non e' pignoleria: e' la promessa fatta a chi carica un patch vecchio. I quattro
            // parametri nuovi non compaiono in nessuno stato salvato ne' in nessun preset di
            // fabbrica, quindi cadono sul default — e un default diverso da quello dell'env
            // d'ampiezza renderebbe il secondo inviluppo una sorpresa invece che un punto di
            // partenza prevedibile.
            const std::pair<const char*, const char*> pairs[] = {
                { "att", "att2" }, { "dec", "dec2" }, { "sus", "sus2" }, { "rel", "rel2" }
            };

            for (const auto& [firstId, secondId] : pairs)
            {
                const auto* a = params::find (firstId);
                const auto* b = params::find (secondId);
                expect (a != nullptr && b != nullptr, juce::String (firstId) + "/" + secondId + ": spec mancante");

                expect (a->kind == b->kind, juce::String (secondId) + ": kind diverso da " + firstId);
                expect (a->map == b->map, juce::String (secondId) + ": mappa diversa da " + firstId);
                expectWithinAbsoluteError (b->min, a->min, 0.0f, juce::String (secondId) + ": min diverso");
                expectWithinAbsoluteError (b->max, a->max, 0.0f, juce::String (secondId) + ": max diverso");
                expectWithinAbsoluteError (b->def, a->def, 0.0f, juce::String (secondId) + ": default diverso");
            }
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
 * Il percorso che PluginProcessor::rebuildModSnapshot() fa sul message thread, meno
 * l'AudioProcessor: XerumTests non linka juce_audio_processors, quindi il processore non è
 * istanziabile qui, ma il pezzo che conta — dal JSON che la WebUI manda a setMods fino allo
 * snapshot che il motore riceve — sì.
 *
 * Tiene insieme tre cose scritte in file diversi: gli id di state::ids, la forma del nodo che
 * state::setMods produce e quella che engine::buildModSnapshot legge. ModMatrixTests costruisce
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
            engine::buildModSnapshot (root.getChildWithName (state::ids::MODS), snapshot);

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
            engine::buildModSnapshot (root.getChildWithName (state::ids::MODS), snapshot);

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
            engine::buildModSnapshot (root.getChildWithName (state::ids::MODS), snapshot);

            expectEquals (snapshot.count, 0);
        }
    }
};

static ModStateWiringTests modStateWiringTests;

/**
 * Task 10: unison e detune.
 *
 * L'unison non ha un "valore giusto" campione per campione da confrontare, quindi i test qui
 * sotto misurano proprieta': quante copie suonano davvero, dove stanno intonate, quanto forte
 * esce la somma e quanto e' larga. Il test piu' importante e' pero' quello negativo, in fondo:
 * con unison 1 lo strumento deve restare esattamente quello di prima.
 */
struct UnisonTests final : juce::UnitTest
{
    UnisonTests() : juce::UnitTest ("unison e detune", "engine") {}

    /** Accende una nota consumando un blocco: le misure successive partono a nota gia' viva. */
    static void noteOn (engine::SynthEngine& synth, int note)
    {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, midi);
    }

    /**
     * RMS del canale sinistro su finestre da `blocksPerWindow` blocchi. La finestra e' lunga
     * apposta: su 128 campioni soli il conteggio dei periodi che ci stanno dentro non e' intero
     * e l'RMS oscilla di qualche punto percentuale anche su un segnale perfettamente periodico,
     * cioe' proprio il rumore di fondo che questo test deve distinguere dai battimenti.
     */
    static std::vector<float> rmsWindows (engine::SynthEngine& synth, int numWindows, int blocksPerWindow)
    {
        std::vector<float> series;
        series.reserve ((size_t) numWindows);
        juce::MidiBuffer none;

        for (int w = 0; w < numWindows; ++w)
        {
            double sumSquares = 0.0;
            int count = 0;

            for (int b = 0; b < blocksPerWindow; ++b)
            {
                juce::AudioBuffer<float> buffer (2, 128);
                buffer.clear();
                synth.process (buffer, none);

                const auto* data = buffer.getReadPointer (0);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    sumSquares += (double) data[i] * (double) data[i];
                    ++count;
                }
            }

            series.push_back (count > 0 ? (float) std::sqrt (sumSquares / (double) count) : 0.0f);
        }

        return series;
    }

    /** Scarto quadratico medio diviso la media: quanto "respira" l'ampiezza. */
    static float relativeSpread (const std::vector<float>& series)
    {
        if (series.empty())
            return 0.0f;

        double mean = 0.0;
        for (const auto value : series)
            mean += (double) value;
        mean /= (double) series.size();

        if (mean <= 0.0)
            return 0.0f;

        double variance = 0.0;
        for (const auto value : series)
            variance += ((double) value - mean) * ((double) value - mean);
        variance /= (double) series.size();

        return (float) (std::sqrt (variance) / mean);
    }

    /**
     * Correlazione fra i due canali. Un segnale audio ha media nulla, quindi non si sottrae
     * nessuna media: e' il coseno fra i due vettori. 1 = i due canali sono lo stesso segnale
     * (immagine puntiforme al centro), 0 = del tutto scorrelati (immagine larghissima).
     */
    static float channelCorrelation (engine::SynthEngine& synth, int numBlocks)
    {
        double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;
        juce::MidiBuffer none;

        for (int b = 0; b < numBlocks; ++b)
        {
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, none);

            const auto* l = buffer.getReadPointer (0);
            const auto* r = buffer.getReadPointer (1);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                sumLL += (double) l[i] * (double) l[i];
                sumRR += (double) r[i] * (double) r[i];
                sumLR += (double) l[i] * (double) r[i];
            }
        }

        const auto denominator = std::sqrt (sumLL * sumRR);
        return denominator > 0.0 ? (float) (sumLR / denominator) : 0.0f;
    }

    /**
     * `numSamples` campioni del segnale mid (L + R), senza MIDI. Si guarda il mid e non il solo
     * canale sinistro perche' lo spread stereo da' a ogni copia un guadagno diverso in L: nel
     * mid i guadagni restano invece quasi uguali fra loro (cos + sin varia poco attorno ai 45
     * gradi), quindi un confronto di ampiezza fra copie misura l'unison e non il pan.
     */
    static std::vector<float> renderMid (engine::SynthEngine& synth, int numSamples)
    {
        std::vector<float> out;
        out.reserve ((size_t) numSamples + 128);
        juce::MidiBuffer none;

        while ((int) out.size() < numSamples)
        {
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, none);

            const auto* l = buffer.getReadPointer (0);
            const auto* r = buffer.getReadPointer (1);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                out.push_back (0.5f * (l[i] + r[i]));
        }

        out.resize ((size_t) numSamples);
        return out;
    }

    /**
     * Ampiezza del segnale a una frequenza qualunque: una singola riga di DFT, valutata dove
     * serve invece che su tutta la griglia di una FFT. La finestra di Hann non e' un dettaglio
     * — senza, la dispersione spettrale di una sinusoide che non cade esattamente su un bin
     * riempirebbe i vuoti fra una copia e l'altra, e il test non saprebbe piu' distinguere otto
     * righe da una macchia larga.
     */
    static double dftMagnitude (const std::vector<float>& x, double frequencyHz, double sampleRate)
    {
        const auto n = (double) x.size();

        if (n <= 0.0)
            return 0.0;

        const auto omega = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
        double re = 0.0, im = 0.0;

        for (size_t i = 0; i < x.size(); ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / n);
            const auto phase = omega * (double) i;
            re += (double) x[i] * window * std::cos (phase);
            im -= (double) x[i] * window * std::sin (phase);
        }

        return std::sqrt (re * re + im * im) / n;
    }

    /** Un motore pronto a suonare con questi due parametri e nient'altro di diverso. */
    static engine::EngineParams unisonParams (int voices, float detuneCents)
    {
        auto p = defaultParams();
        p.filterOn = false; // niente filtro a sporcare conteggi di zero e misure di livello
        p.sustain = 1.0f;
        p.unisonVoices = voices;
        p.detuneCents = detuneCents;
        return p;
    }

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("collectEngineParams: il choice unison diventa 1/2/4/8, detune diventa cent");
        {
            const int expected[] = { 1, 2, 4, 8 };

            for (int index = 0; index < 4; ++index)
            {
                const auto rawFor = [index] (params::ParamSlot slot) noexcept
                {
                    return slot == params::ParamSlot::unison ? (float) index : 0.0f;
                };

                expectEquals (params::collectEngineParams (rawFor).unisonVoices, expected[index],
                              "indice " + juce::String (index));
            }

            for (const float real : { 0.0f, 18.0f, 100.0f })
            {
                const auto raw = real / 100.0f; // detune ha mappa lineare 0..100
                const auto rawFor = [raw] (params::ParamSlot slot) noexcept
                {
                    return slot == params::ParamSlot::detune ? raw : 0.0f;
                };

                expectWithinAbsoluteError (params::collectEngineParams (rawFor).detuneCents, real, 1.0e-3f);
            }
        }

        beginTest ("gli offset di detune sono simmetrici attorno allo zero e coprono +-detune");
        {
            // L'esempio del brief: N = 4 e detune 18 cent danno -18, -6, +6, +18.
            const float expected4[] = { -18.0f, -6.0f, 6.0f, 18.0f };

            for (int i = 0; i < 4; ++i)
                expectWithinAbsoluteError (18.0f * engine::unisonSpread (i, 4), expected4[i], 1.0e-4f,
                                           "copia " + juce::String (i));

            expectWithinAbsoluteError (18.0f * engine::unisonSpread (0, 2), -18.0f, 1.0e-4f);
            expectWithinAbsoluteError (18.0f * engine::unisonSpread (1, 2), 18.0f, 1.0e-4f);

            // N = 1: nessun offset, ed e' un'uguaglianza esatta perche' da questa dipende la
            // non-regressione (la conversione cent -> rapporto deve dare esattamente 1.0f).
            expectEquals (engine::unisonSpread (0, 1), 0.0f);

            for (const int n : { 2, 4, 8 })
            {
                double sum = 0.0;
                for (int i = 0; i < n; ++i)
                    sum += (double) engine::unisonSpread (i, n);

                expectWithinAbsoluteError ((float) sum, 0.0f, 1.0e-5f, "somma, N " + juce::String (n));
                expectWithinAbsoluteError (engine::unisonSpread (0, n), -1.0f, 1.0e-6f);
                expectWithinAbsoluteError (engine::unisonSpread (n - 1, n), 1.0f, 1.0e-6f);
            }
        }

        beginTest ("resetToPhase sposta la fase di esattamente la frazione chiesta");
        {
            // A 375 Hz con sample rate 48 kHz l'incremento di fase vale 2^-7 esatti: un quarto
            // di ciclo sono 32 campioni tondi, e il confronto puo' essere a tolleranza zero.
            // E' il mattone su cui poggia la distribuzione delle fasi dell'unison, quindi si
            // verifica da solo prima di verificarlo attraverso il motore.
            dsp::WavetableOscillator fromZero, fromQuarter;

            for (auto* oscillator : { &fromZero, &fromQuarter })
            {
                oscillator->prepare (48000.0);
                oscillator->setTable (store.active());
                oscillator->setFrequencyHz (375.0f);
                oscillator->setFramePosition (0.0f);
            }

            fromZero.reset();
            fromQuarter.resetToPhase (0.25f);

            std::vector<float> zero (160);
            for (auto& value : zero)
                value = fromZero.getSample();

            for (int i = 0; i < 128; ++i)
                expectWithinAbsoluteError (fromQuarter.getSample(), zero[(size_t) (i + 32)], 0.0f,
                                           "campione " + juce::String (i));

            // E un giro intero riporta esattamente dove si era: 1.0 deve valere 0.0.
            dsp::WavetableOscillator wrapped;
            wrapped.prepare (48000.0);
            wrapped.setTable (store.active());
            wrapped.setFrequencyHz (375.0f);
            wrapped.setFramePosition (0.0f);
            wrapped.resetToPhase (1.0f);
            expectWithinAbsoluteError (wrapped.getSample(), zero[0], 0.0f);
        }

        beginTest ("le N copie suonano a N intonazioni distinte, simmetriche attorno alla nota");
        {
            // Si guarda lo spettro attorno alla nota: con detune a fondo corsa le copie sono
            // separate di abbastanza da risolverle una per una. Il test chiede tre cose
            // insieme — che ci sia una riga su ciascuna delle N frequenze attese (quindi le
            // copie attive sono N), che fra una riga e l'altra non ci sia niente, e che fuori
            // dall'intervallo +-detune non ci sia niente (quindi la distribuzione e' proprio
            // quella simmetrica, non una a partire da zero verso l'alto).
            constexpr int noteNumber = 81; // La 880 Hz: piu' alta e' la nota, piu' larghe in Hz
            constexpr float baseHz = 880.0f;   // sono le distanze fra le copie
            constexpr float detune = 100.0f;   // fondo corsa
            constexpr double sampleRate = 48000.0;
            constexpr int windowSamples = 32768; // 0.68 s: 1.46 Hz per bin

            const auto centsToHz = [] (float cents) { return baseHz * std::exp2 (cents / 1200.0f); };

            for (const int voices : { 1, 2, 4, 8 })
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (voices, detune));
                noteOn (synth, noteNumber);

                renderPeak (synth, 40, *this);
                const auto mid = renderMid (synth, windowSamples);

                // Le righe attese, e l'ampiezza piu' bassa e piu' alta fra loro.
                double weakest = 1.0e9, strongest = 0.0;

                for (int i = 0; i < voices; ++i)
                {
                    const auto amplitude = dftMagnitude (mid, centsToHz (detune * engine::unisonSpread (i, voices)),
                                                         sampleRate);
                    weakest = juce::jmin (weakest, amplitude);
                    strongest = juce::jmax (strongest, amplitude);
                }

                expect (weakest > 0.5 * strongest,
                        "unison " + juce::String (voices) + ": una copia e' molto piu' debole "
                            + "delle altre (" + juce::String (weakest) + " contro "
                            + juce::String (strongest) + ")");

                // Fra due copie adiacenti (e appena fuori dall'intervallo) non deve esserci
                // nulla: e' cio' che distingue N righe da una macchia larga.
                double intruder = 0.0;

                for (int i = 0; i + 1 < voices; ++i)
                {
                    const auto midpoint = 0.5f * detune
                                              * (engine::unisonSpread (i, voices)
                                                 + engine::unisonSpread (i + 1, voices));
                    intruder = juce::jmax (intruder, dftMagnitude (mid, centsToHz (midpoint), sampleRate));
                }

                for (const float outside : { -1.5f * detune, 1.5f * detune })
                    intruder = juce::jmax (intruder, dftMagnitude (mid, centsToHz (outside), sampleRate));

                expect (intruder < 0.2 * weakest,
                        "unison " + juce::String (voices) + ": c'e' energia dove non dovrebbe "
                            + "essercene (" + juce::String (intruder) + " contro "
                            + juce::String (weakest) + ")");
            }
        }

        beginTest ("a detune zero le fasi distribuite impediscono alle copie di sommarsi in fase");
        {
            // Otto copie tutte alla stessa frequenza e tutte a fase zero sarebbero lo stesso
            // segnale sommato otto volte: ampiezza 8x, che la compensazione 1/sqrt(8) riduce a
            // 2.83x — quasi 9 dB regalati appena si tocca il knob. Distribuite su i/8 del ciclo
            // si sommano invece come sorgenti distinte, e la riga fondamentale resta ben sotto
            // quella di una copia sola.
            constexpr int noteNumber = 81;
            constexpr double sampleRate = 48000.0;

            const auto fundamental = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (voices, 0.0f));
                noteOn (synth, noteNumber);

                renderPeak (synth, 40, *this);
                return dftMagnitude (renderMid (synth, 16384), 880.0, sampleRate);
            };

            const auto one = fundamental (1);
            const auto eight = fundamental (8);
            const auto ratio = one > 0.0 ? eight / one : 0.0;

            expect (ratio < 0.7,
                    "le otto copie si stanno sommando in fase: fondamentale " + juce::String (ratio)
                        + " volte quella di una copia sola (a fase zero sarebbe 2.83)");

            logMessage ("detune 0, unison 8: fondamentale " + juce::String (ratio)
                        + " volte quella di unison 1");
        }

        beginTest ("detune zero con unison 8 non produce battimenti; con detune si'");
        {
            const auto breathing = [&store, this] (float detuneCents)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (8, detuneCents));
                noteOn (synth, 60);

                renderPeak (synth, 40, *this); // ~0.1 s: attacco e rampa di level
                return relativeSpread (rmsWindows (synth, 50, 8));
            };

            const auto flat = breathing (0.0f);
            const auto beating = breathing (18.0f);

            // Otto copie alla stessa identica frequenza: la somma e' periodica, l'ampiezza non
            // ha ragione di muoversi. Se le fasi non fossero distribuite il segnale sarebbe
            // ancora piatto, ma otto volte piu' forte — quello lo becca il test del livello.
            expect (flat < 0.02f,
                    "detune 0: l'ampiezza dovrebbe restare piatta, variazione relativa "
                        + juce::String (flat));

            // A 261.6 Hz, +-18 cent separano le copie estreme di 5.4 Hz: un battimento ogni
            // 185 ms, cioe' cinque o sei cicli dentro la finestra misurata.
            expect (beating > flat * 5.0f && beating > 0.05f,
                    "detune 18: i battimenti non si vedono, variazione relativa "
                        + juce::String (beating) + " contro " + juce::String (flat));
        }

        beginTest ("il livello resta costante fra unison 1 e unison 8");
        {
            const auto rms = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                synth.setParams (unisonParams (voices, 18.0f));
                noteOn (synth, 60);

                renderPeak (synth, 40, *this);
                return renderRms (synth, 750); // ~2 s: una decina di cicli di battimento
            };

            const auto one = rms (1);
            const auto eight = rms (8);
            const auto deltaDb = juce::Decibels::gainToDecibels (eight / one);

            expect (std::abs (deltaDb) < 1.5f,
                    "unison 8 sta " + juce::String (deltaDb, 2) + " dB da unison 1");

            logMessage ("livello: unison 1 " + juce::String (juce::Decibels::gainToDecibels (one), 2)
                        + " dBFS RMS, unison 8 "
                        + juce::String (juce::Decibels::gainToDecibels (eight), 2) + " dBFS RMS ("
                        + juce::String (deltaDb, 2) + " dB)");
        }

        beginTest ("l'immagine stereo si allarga con unison");
        {
            const auto correlation = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                auto p = unisonParams (voices, 18.0f);
                p.pan = 0.0f;
                synth.setParams (p);
                noteOn (synth, 60);

                renderPeak (synth, 40, *this);
                return channelCorrelation (synth, 400);
            };

            const auto mono = correlation (1);
            const auto wide = correlation (8);

            // Una copia sola al centro: i due canali sono lo stesso segnale, moltiplicato per
            // lo stesso guadagno. Correlazione 1, senza margini.
            expectWithinAbsoluteError (mono, 1.0f, 1.0e-4f,
                                       "unison 1 dovrebbe restare puntiforme, correlazione "
                                           + juce::String (mono));

            expect (wide < 0.9f, "unison 8 non allarga: correlazione " + juce::String (wide));
            expect (wide > 0.0f, "correlazione negativa (" + juce::String (wide)
                                     + "): lo spread sta invertendo la fase, non allargando");

            logMessage ("correlazione L/R: unison 1 " + juce::String (mono) + ", unison 8 "
                        + juce::String (wide));
        }

        beginTest ("unison 1: detune non tocca un solo campione");
        {
            // Il criterio di accettazione che conta piu' di tutti, in forma di test permanente:
            // il knob detune di default e' a 18 cent, quindi se l'unison lo leggesse anche con
            // una copia sola lo strumento cambierebbe suono a parametri invariati.
            const auto render = [&store] (float detuneCents)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);
                auto p = unisonParams (1, detuneCents);
                p.filterOn = true;
                synth.setParams (p);
                synth.setMasterGainLinear (0.8f);

                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                juce::AudioBuffer<float> first (2, 128);
                first.clear();
                synth.process (first, midi);

                std::vector<float> out;
                juce::MidiBuffer none;
                for (int b = 0; b < 30; ++b)
                {
                    juce::AudioBuffer<float> buffer (2, 128);
                    buffer.clear();
                    synth.process (buffer, none);
                    const auto* data = buffer.getReadPointer (0);
                    out.insert (out.end(), data, data + buffer.getNumSamples());
                }

                return out;
            };

            const auto quiet = render (0.0f);
            const auto full = render (100.0f);

            expectEquals ((int) full.size(), (int) quiet.size());
            for (size_t i = 0; i < quiet.size(); ++i)
                expectWithinAbsoluteError (full[i], quiet[i], 0.0f);
        }

        beginTest ("unison 8 su sedici voci: niente NaN, niente campioni fuori scala");
        {
            // La stessa configurazione dura misurata anche a unison 1: il numero da guardare non
            // e' il picco in se' (il soft clipper lo tiene comunque sotto 1.0) ma di quanto
            // unison lo sposta, perche' e' quello che entra nel gain staging.
            const auto peakWith = [&store, this] (int voices)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = unisonParams (voices, 100.0f); // detune a fondo corsa
                p.filterOn = true;
                p.cutoffHz = 4000.0f;
                p.resonanceQ = 6.0f;
                p.driveGain = 4.0f;
                synth.setParams (p);
                synth.setMasterGainLinear (1.0f);

                juce::MidiBuffer midi;
                for (int i = 0; i < engine::VoiceManager::maxVoices; ++i)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 30 + i * 4, 1.0f), 0);

                juce::AudioBuffer<float> first (2, 128);
                first.clear();
                synth.process (first, midi);

                return renderPeak (synth, 200, *this); // verifica gia' che ogni campione sia finito
            };

            for (const int voices : { 1, 8 })
            {
                const auto peak = peakWith (voices);

                expect (peak <= 1.0f, "unison " + juce::String (voices) + ": campione fuori scala, picco "
                                          + juce::String (peak));
                expect (peak > 0.05f, "unison " + juce::String (voices) + ": uscita troppo bassa ("
                                          + juce::String (peak) + "), il test non verifica niente");

                logMessage ("16 voci, detune 100 ct, Q 6, drive +12 dB, unison "
                            + juce::String (voices) + ": picco " + juce::String (peak) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
            }
        }
    }
};

static UnisonTests unisonTests;

/**
 * Task 9: il mod matrix a pieno carico.
 *
 * Non e' un test di correttezza timbrica — con trentadue route addosso non esiste un valore
 * "giusto" da confrontare. E' la rete di sicurezza real-time: quando ogni target e' tirato da
 * piu' sorgenti insieme e i parametri si muovono sotto, ogni campione deve restare finito e
 * dentro il fondo scala, e nessuna voce deve restare appesa.
 *
 * Piu' severo del test degli estremi (EngineRobustnessTests): li' i parametri sono fissi per
 * l'intera variante, qui cambiano *fra un blocco e l'altro* mentre la modulazione e' attiva, e
 * il contratto verificato non e' solo il picco ma ogni singolo campione.
 */
struct ModulationStressTests final : juce::UnitTest
{
    ModulationStressTests() : juce::UnitTest ("mod matrix a pieno carico", "engine") {}

    /**
     * Riempie lo snapshot fino al tetto di kMaxRoutes (32). Le prime 4 x 7 = 28 route sono
     * tutte le combinazioni sorgente x target; le quattro che avanzano ricominciano da capo,
     * quindi sono route *doppie* — stessa sorgente, stesso target, stesso segno. Non e'
     * riempitivo: e' il caso che mette alla prova la somma e il clamp, non la sola capienza
     * dell'array.
     *
     * I depth alternano +1 e -1 e il passo fra due sorgenti (7) e' dispari, quindi ogni target
     * riceve due route positive e due negative da sorgenti diverse: le modulazioni si
     * contendono il parametro invece di spingerlo tutte dalla stessa parte.
     */
    static engine::ModSnapshot fullSnapshot() noexcept
    {
        engine::ModSnapshot mods;

        for (int i = 0; i < engine::ModSnapshot::kMaxRoutes; ++i)
        {
            const auto src = (engine::ModSource) ((i / engine::kNumModTargets)
                                                  % (int) engine::ModSource::count);
            mods.routes[i] = { src, i % engine::kNumModTargets, (i % 2 == 0) ? 1.0f : -1.0f };
        }

        mods.count = engine::ModSnapshot::kMaxRoutes;
        return mods;
    }

    struct Config
    {
        double sampleRate { 48000.0 };
        int numNotes { 16 };
        bool lfoSync { false };
        bool lfoRetrig { true };
        dsp::StateVariableFilter::Type filterType { dsp::StateVariableFilter::Type::lowPass };
        int filterStages { 2 };
        bool republishEachBlock { false };
        int unisonVoices { 1 };
        float detuneCents { 0.0f };
    };

    static size_t targetOf (params::ParamSlot slot) noexcept
    {
        return (size_t) engine::modTargetIndexFor (slot);
    }

    /**
     * Una passata completa: matrix pieno, note tenute, parametri che si muovono a ogni blocco.
     * Controlla ogni campione qui dentro (finito e |x| <= 1), scarica la coda dopo un
     * all-notes-off e ritorna il picco.
     */
    float sweep (dsp::WavetableStore& store, const Config& cfg, int numBlocks, const juce::String& what)
    {
        engine::SynthEngine synth;

        engine::EngineSpec spec;
        spec.sampleRate = cfg.sampleRate;
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());
        synth.setMods (fullSnapshot());

        auto p = defaultParams();
        p.filterOn = true;
        p.filterType = cfg.filterType;
        p.filterStages = cfg.filterStages;
        p.keyTrack = 0.5f;
        p.velocityAmount = 1.0f;
        p.attackSeconds = 0.005f;
        p.decaySeconds = 0.2f;
        p.sustain = 0.9f;
        p.releaseSeconds = 0.08f;
        p.lfoRateRaw = 1.0f;     // il massimo della mappa: la sorgente lfo si muove a ogni blocco
        p.lfoSync = cfg.lfoSync;
        p.lfoRetrig = cfg.lfoRetrig;
        p.lfoFadeSeconds = 0.0f;
        p.bpm = 200.0f;          // sync a tempo alto: divisioni brevi, quindi LFO veloce
        p.unisonVoices = cfg.unisonVoices;
        p.detuneCents = cfg.detuneCents;
        p.modBase.fill (0.5f);

        juce::MidiBuffer midi;

        for (int i = 0; i < cfg.numNotes; ++i)
            midi.addEvent (juce::MidiMessage::noteOn (1, 24 + (i * 5) % 96, 0.3f + 0.7f * (float) (i % 3) * 0.5f), 0);

        float peak = 0.0f;
        int badBlock = -1;
        int badSample = -1;
        float badValue = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            // Triangolo 0..1 di periodo `period`. I periodi sono primi fra loro, cosi' le sette
            // basi non tornano mai in fase e la passata attraversa molte combinazioni diverse.
            const auto ramp = [b] (int period) noexcept
            {
                const auto phase = (float) (b % period) / (float) period;
                return phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
            };

            // Gradino: 0 o 1, niente vie di mezzo. res e drive non passano da nessuno
            // SmoothedValue, quindi il salto arriva intero al filtro e alla saturazione — che
            // e' esattamente il caso da verificare, non da evitare.
            const auto step = [b] (int period) noexcept { return ((b / period) % 2) == 0 ? 0.0f : 1.0f; };

            p.modBase[targetOf (params::ParamSlot::cutoff)] = ramp (7);
            p.modBase[targetOf (params::ParamSlot::wtpos)] = ramp (11);
            p.modBase[targetOf (params::ParamSlot::level)] = 0.5f + 0.5f * ramp (13);
            p.modBase[targetOf (params::ParamSlot::pan)] = ramp (17);
            p.modBase[targetOf (params::ParamSlot::fine)] = ramp (19);
            p.modBase[targetOf (params::ParamSlot::res)] = step (5);
            p.modBase[targetOf (params::ParamSlot::drive)] = step (3);

            // Gli stessi valori anche nei campi denormalizzati: se un target smettesse di
            // passare dal percorso modulato (modMask_ spento) il test deve restare severo lo
            // stesso, invece di ritrovarsi i default innocui di defaultParams().
            p.cutoffHz = params::cutoffHzFromRaw (p.modBase[targetOf (params::ParamSlot::cutoff)]);
            p.framePosition = params::framePositionFromRaw (p.modBase[targetOf (params::ParamSlot::wtpos)]);
            p.level = params::levelGainFromRaw (p.modBase[targetOf (params::ParamSlot::level)]);
            p.pan = params::panFromRaw (p.modBase[targetOf (params::ParamSlot::pan)]);
            p.fineCents = params::fineCentsFromRaw (p.modBase[targetOf (params::ParamSlot::fine)]);
            p.resonanceQ = params::resonanceQFromRaw (p.modBase[targetOf (params::ParamSlot::res)]);
            p.driveGain = params::driveGainFromRaw (p.modBase[targetOf (params::ParamSlot::drive)]);

            p.lfoShapeIndex = (b / 9) % 5; // tutte e cinque le forme, sample & hold compresa

            synth.setParams (p);

            // Anche il master gain si muove: il ramp fra previousMasterGain_ e masterGain_ e'
            // l'ultimo stadio prima del soft clipper.
            synth.setMasterGainLinear ((b % 2) == 0 ? 1.0f : 0.6f);

            // Ripubblicare a ogni blocco fa girare l'anello di quattro slot: un giro completo
            // ogni quattro blocchi, con il lettore che nel frattempo sta usando uno di essi.
            if (cfg.republishEachBlock)
                synth.setMods (fullSnapshot());

            // Mod wheel a scatti: e' la quarta sorgente, e a differenza delle altre tre arriva
            // da un evento MIDI a meta' buffer.
            if ((b % 12) == 0)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, (b % 24) == 0 ? 127 : 0), 64);

            // Una nota che riparte a meta' blocco: costringe process() a spezzare il render in
            // due fette, e applyModulation() a rigirare su una voce che sta gia' suonando.
            if ((b % 16) == 8)
            {
                const int note = 24 + (b * 7) % 96;
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.2f + 0.8f * ramp (3)), 100);
            }

            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);

                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto value = data[i];

                    // Una sola expect() alla fine invece di una per campione: un NaN che
                    // dura mezzo secondo produrrebbe altrimenti migliaia di righe di log
                    // identiche e nasconderebbe il primo punto in cui e' comparso.
                    if (badBlock < 0 && (! std::isfinite (value) || std::abs (value) > 1.0f))
                    {
                        badBlock = b;
                        badSample = i;
                        badValue = value;
                    }

                    peak = juce::jmax (peak, std::abs (value));
                }
            }
        }

        expect (badBlock < 0,
                what + ": campione fuori contratto al blocco " + juce::String (badBlock) + ", indice "
                    + juce::String (badSample) + ", valore " + juce::String (badValue));

        // Il test e' privo di valore se il motore stesse restituendo silenzio: qualunque
        // soglia superiore la passerebbe.
        expect (peak > 0.05f, what + ": uscita troppo bassa (" + juce::String (peak)
                                  + "): la passata non sta verificando niente");

        // Nessuna voce appesa, nemmeno con il matrix pieno: una route con depth negativo su
        // `level` puo' portare il parametro a zero, ma e' l'inviluppo — non il livello — a
        // decidere quando la voce si libera.
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        {
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, off);
        }

        // release e' 80 ms: si scarta il decadimento (che sta ancora suonando) prima di
        // misurare il silenzio vero. Il numero di blocchi segue il sample rate.
        const int releaseBlocks = (int) std::ceil (0.08 * cfg.sampleRate / 128.0) + 10;
        renderPeak (synth, releaseBlocks, *this);
        const auto tail = renderPeak (synth, 20, *this);
        expect (tail < 1.0e-4f, what + ": voce appesa dopo il release, picco " + juce::String (tail));

        return peak;
    }

    /**
     * Passata casuale ma riproducibile: matrix di lunghezza e contenuto qualunque, parametri
     * ridisegnati a ogni blocco, blocchi di lunghezza variabile, eventi MIDI in posizioni
     * arbitrarie dentro il buffer. Stesso contratto delle passate guidate — ogni campione
     * finito e dentro il fondo scala — ma percorso in un modo che nessuno ha scelto a mano.
     */
    void fuzz (dsp::WavetableStore& store, int seed, int numBlocks)
    {
        juce::Random rng (seed);

        engine::SynthEngine synth;
        engine::EngineSpec spec;
        spec.sampleRate = (double) juce::jmap (rng.nextFloat(), 44100.0f, 96000.0f);
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());

        auto p = defaultParams();
        float peak = 0.0f;
        int badBlock = -1;
        int badSample = -1;
        float badValue = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            // Una ripubblicazione ogni quattro blocchi in media: abbastanza frequente da far
            // girare l'anello, abbastanza rara da lasciare che uno snapshot resti in uso per
            // piu' blocchi di fila.
            if (rng.nextInt (4) == 0)
            {
                engine::ModSnapshot mods;
                mods.count = rng.nextInt (engine::ModSnapshot::kMaxRoutes + 1);

                for (int i = 0; i < mods.count; ++i)
                    mods.routes[i] = { (engine::ModSource) rng.nextInt ((int) engine::ModSource::count),
                                       rng.nextInt (engine::kNumModTargets),
                                       rng.nextFloat() * 2.0f - 1.0f };

                synth.setMods (mods);
            }

            for (auto& base : p.modBase)
                base = rng.nextFloat();

            p.cutoffHz = params::cutoffHzFromRaw (p.modBase[targetOf (params::ParamSlot::cutoff)]);
            p.resonanceQ = params::resonanceQFromRaw (p.modBase[targetOf (params::ParamSlot::res)]);
            p.framePosition = params::framePositionFromRaw (p.modBase[targetOf (params::ParamSlot::wtpos)]);
            p.level = params::levelGainFromRaw (p.modBase[targetOf (params::ParamSlot::level)]);
            p.pan = params::panFromRaw (p.modBase[targetOf (params::ParamSlot::pan)]);
            p.fineCents = params::fineCentsFromRaw (p.modBase[targetOf (params::ParamSlot::fine)]);
            p.driveGain = params::driveGainFromRaw (p.modBase[targetOf (params::ParamSlot::drive)]);

            p.oscOn = rng.nextInt (16) != 0;
            p.filterOn = rng.nextInt (8) != 0;
            p.filterType = (dsp::StateVariableFilter::Type) rng.nextInt (3);
            p.filterStages = 1 + rng.nextInt (2);
            p.keyTrack = rng.nextFloat();
            p.octave = rng.nextInt (7) - 3;
            p.semitones = rng.nextInt (25) - 12;
            p.attackSeconds = rng.nextFloat() * 0.05f;
            p.decaySeconds = rng.nextFloat() * 0.3f;
            p.sustain = rng.nextFloat();
            p.releaseSeconds = 0.01f + rng.nextFloat() * 0.1f;
            p.velocityAmount = rng.nextFloat();
            p.lfoShapeIndex = rng.nextInt (5);
            p.lfoRateRaw = rng.nextFloat();
            p.lfoSync = rng.nextBool();
            p.lfoRetrig = rng.nextBool();
            p.lfoPhaseOffset01 = rng.nextFloat();
            p.lfoFadeSeconds = rng.nextFloat() * 2.0f;
            p.bpm = 20.0f + rng.nextFloat() * 280.0f;
            p.bypass = rng.nextInt (64) == 0; // raro: serve che le voci suonino davvero

            synth.setParams (p);
            synth.setMasterGainLinear (rng.nextFloat());

            // Blocchi di lunghezza variabile: l'host non ne garantisce una fissa, e con blocchi
            // corti l'LFO e gli SmoothedValue avanzano di pochi campioni per volta.
            const int numSamples = 1 + rng.nextInt (128);
            juce::MidiBuffer midi;

            for (int e = rng.nextInt (4); --e >= 0;)
            {
                const int pos = rng.nextInt (numSamples);
                const int roll = rng.nextInt (10);

                if (roll < 5)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 12 + rng.nextInt (108), rng.nextFloat()), pos);
                else if (roll < 8)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 12 + rng.nextInt (108)), pos);
                else
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rng.nextInt (128)), pos);
            }

            juce::AudioBuffer<float> buffer (2, numSamples);
            buffer.clear();
            synth.process (buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto value = data[i];

                    if (badBlock < 0 && (! std::isfinite (value) || std::abs (value) > 1.0f))
                    {
                        badBlock = b;
                        badSample = i;
                        badValue = value;
                    }

                    peak = juce::jmax (peak, std::abs (value));
                }
            }
        }

        const auto what = "fuzz seme " + juce::String (seed);
        expect (badBlock < 0, what + ": campione fuori contratto al blocco " + juce::String (badBlock)
                                  + ", indice " + juce::String (badSample) + ", valore " + juce::String (badValue));
        expect (peak > 0.01f, what + ": uscita troppo bassa (" + juce::String (peak) + ")");
    }

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("matrix pieno e parametri in movimento: ogni campione resta nel contratto");
        {
            const auto peak = sweep (store, {}, 240, "passata lunga");

            // La soglia del soft clipper e' 0.95, e sotto soglia l'uscita e' identica
            // all'ingresso: un picco sopra 0.95 *dimostra* che il clipper e' entrato in
            // funzione. Se restasse sotto, questo test non starebbe verificando il clipper ma
            // solo che un segnale gia' piccolo resta piccolo, e l'asserzione serve proprio a
            // impedire che diventi vacuo quando il gain staging cambia.
            //
            // La soglia sale da 0.8 a 0.95 insieme a kSoftClipThreshold: tenerla a 0.8 avrebbe
            // lasciato passare un caso in cui il clipper non fa piu' niente. Il picco misurato,
            // 0.999, corrisponde a 3.48 in ingresso al clipper (+10.8 dBFS, erano +8.0 prima
            // della ritaratura): con `res` modulato a fondo corsa e' il picco risonante del
            // filtro, ora +26.8 dB sopra Butterworth, a portarcelo.
            expect (peak > 0.95f, "picco " + juce::String (peak)
                                      + ": il soft clipper non e' nemmeno entrato in funzione");

            logMessage ("matrix pieno, 16 note, 240 blocchi: picco " + juce::String (peak) + " ("
                        + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
        }

        beginTest ("matrix pieno nelle tre configurazioni di filtro e a tre sample rate");
        {
            // Il picco di risonanza dipende dal sample rate (il warping di tan()) e dal numero
            // di stadi; il passa-alto e il passa-banda hanno un percorso diverso dentro l'SVF.
            // Con `res` modulato a fondo corsa e' proprio qui che il soft clipper va verificato.
            for (const auto rate : { 44100.0, 48000.0, 96000.0 })
                for (const auto type : { dsp::StateVariableFilter::Type::lowPass,
                                         dsp::StateVariableFilter::Type::highPass,
                                         dsp::StateVariableFilter::Type::bandPass })
                    for (const auto stages : { 1, 2 })
                    {
                        Config cfg;
                        cfg.sampleRate = rate;
                        cfg.filterType = type;
                        cfg.filterStages = stages;

                        const auto what = juce::String (rate, 0) + " Hz, tipo " + juce::String ((int) type)
                                              + ", " + juce::String (stages) + " stadi";
                        const auto peak = sweep (store, cfg, 96, what);
                        logMessage (what + ": picco " + juce::String (peak));
                    }
        }

        beginTest ("matrix pieno con LFO in sync e senza retrigger");
        {
            // Senza retrigger la sorgente `lfo` e' quella libera del motore, condivisa da tutte
            // le voci: un percorso diverso da quello per voce, e va stressato anch'esso.
            for (const auto sync : { false, true })
                for (const auto retrig : { false, true })
                {
                    Config cfg;
                    cfg.lfoSync = sync;
                    cfg.lfoRetrig = retrig;
                    sweep (store, cfg, 96,
                           juce::String ("sync ") + (sync ? "on" : "off") + ", retrig "
                               + (retrig ? "on" : "off"));
                }
        }

        beginTest ("matrix pieno e voice stealing: ventiquattro note su un pool da sedici");
        {
            // Oltre il pool si ruba, e rubare chiama kill(): la voce riparte con start(), che
            // rifa' applyModulation() da capo. Il gradino del furto e' un difetto noto
            // (vedi docs/architecture.md), ma non deve mai diventare un campione fuori scala.
            Config cfg;
            cfg.numNotes = 24;
            const auto peak = sweep (store, cfg, 180, "24 note");
            logMessage ("24 note (8 rubate): picco " + juce::String (peak));
        }

        beginTest ("l'anello regge una ripubblicazione a ogni blocco");
        {
            Config cfg;
            cfg.republishEachBlock = true;
            sweep (store, cfg, 180, "ripubblicazione continua");
        }

        beginTest ("matrix pieno e unison 8: centoventotto oscillatori sotto modulazione");
        {
            // Unison moltiplica per otto gli oscillatori ma non tocca il resto della catena:
            // cio' che va verificato e' che la somma delle copie, compensata di 1/sqrt(N), non
            // faccia saltare il contratto proprio dove il margine e' gia' stretto — con `res`
            // modulato a fondo corsa su sedici note.
            Config cfg;
            cfg.unisonVoices = 8;
            cfg.detuneCents = 18.0f;
            const auto peak = sweep (store, cfg, 240, "unison 8"); // stesso numero di blocchi della passata a unison 1

            logMessage ("unison 8, detune 18 ct, 16 note: picco " + juce::String (peak) + " ("
                        + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
        }

        beginTest ("fuzz a semi fissi: matrix casuale, parametri casuali, eventi casuali");
        {
            // Le passate sopra percorrono traiettorie scelte a mano, quindi esplorano solo le
            // combinazioni a cui ho pensato. Questa attraversa lo spazio in modo diverso a ogni
            // seme: matrix di lunghezza qualunque, depth qualunque, basi che saltano di blocco
            // in blocco, note che entrano ed escono. I semi sono fissi apposta — un test che
            // fallisce una volta su venti non e' un test — ma il generatore e' stato fatto
            // girare su duecento semi durante la scrittura, senza trovare un solo campione
            // fuori dal contratto.
            for (const auto seed : { 1, 7, 42, 1337, 90210 })
                fuzz (store, seed, 400);
        }

        beginTest ("count a zero copre le route rimaste nello slot dell'anello");
        {
            // buildModSnapshot riscrive `count` ma non ripulisce le route oltre di esso, e gli
            // slot dell'anello vengono riusati: se qualcuno leggesse routes[] senza fermarsi a
            // `count`, una modulazione cancellata dalla UI continuerebbe a suonare. Qui
            // l'anello viene riempito di snapshot pieni, poi svuotato, e l'uscita deve tornare
            // identica campione per campione a quella di un motore che non ha mai visto una
            // route.
            const auto render = [&store] (bool fillRingFirst)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                if (fillRingFirst)
                {
                    // Cinque pubblicazioni: l'anello ha quattro slot, quindi ne riscrive uno.
                    for (int i = 0; i < 5; ++i)
                        synth.setMods (fullSnapshot());

                    auto empty = fullSnapshot();
                    empty.count = 0; // route ancora tutte li' dentro, ma invisibili
                    synth.setMods (empty);
                }

                auto p = defaultParams();
                p.filterOn = true;
                p.modBase.fill (0.5f);
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

            const auto clean = render (false);
            const auto emptied = render (true);

            expectEquals ((int) emptied.size(), (int) clean.size());
            for (size_t i = 0; i < clean.size(); ++i)
                expectWithinAbsoluteError (emptied[i], clean[i], 0.0f);
        }
    }
};

static ModulationStressTests modulationStressTests;


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



/**
 * La profondita' di modulazione che arriva davvero all'uscita, in funzione della frequenza
 * dell'LFO.
 *
 * Perche' esiste. Gli SmoothedValue della voce hanno una rampa di 20 ms, cioe' — con il
 * bersaglio riposato una volta per sotto-fetta e la rampa che ne recupera 32/960 per volta —
 * un passa-basso a un polo a circa 8 Hz. Finche' dentro quegli smoother finiva il valore *gia'
 * modulato*, quel filtro si applicava anche alla modulazione: una route lfo -> cutoff a 8 Hz
 * usciva attenuata di 3 dB, a 20 Hz di 8. Lo smoothing esiste per togliere lo zipper quando
 * l'utente o l'host muovono un parametro a scatti, non per filtrare un segnale che e' gia'
 * continuo per costruzione.
 *
 * Come si misura. Non si guarda il valore di cutoff dentro la voce — sarebbe una finestra
 * aperta nel percorso audio solo per il test — ma l'unica cosa che l'utente sente: l'uscita.
 * Si tiene una nota ferma, si calcola l'RMS su finestre consecutive di 64 campioni (un
 * inviluppo a 750 Hz a 48 kHz, quaranta punti per ciclo anche a 20 Hz), si scarta il mezzo
 * secondo iniziale — attacco e assestamento delle rampe — e si estrae dall'inviluppo la sola
 * componente alla frequenza dell'LFO, con una DFT a una riga e finestra di Hann. Il rapporto
 * fra quell'ampiezza e il valor medio dell'inviluppo e' la profondita' vista all'uscita.
 *
 * Il numero assoluto non conta e non e' confrontabile fra un bersaglio e l'altro (su `level` e'
 * l'ampiezza stessa, su `cutoff` e' il modo in cui il filtro traduce una spazzata in energia):
 * quello che conta e' che **non cambi con la frequenza dell'LFO**. Si confronta quindi ogni
 * frequenza con la piu' bassa della serie.
 */
struct ModulationDepthTests final : juce::UnitTest
{
    ModulationDepthTests() : juce::UnitTest ("profondita' di modulazione", "engine") {}

    static constexpr double kSampleRate = 48000.0;

    /**
     * Larghezza della finestra d'RMS, in campioni. E' il compromesso su cui sta in piedi la
     * misura, e va stretto fra due limiti opposti: la finestra deve contenere **piu' cicli
     * della nota** — altrimenti l'RMS insegue la forma d'onda invece dell'inviluppo, ed e'
     * rumore che si somma alla riga che si vuole leggere — e deve restare **molto piu' corta
     * del ciclo dell'LFO**, o attenua proprio cio' che misura.
     *
     * 192 campioni a 48 kHz sono 4 ms: quattro cicli della nota (MIDI 84, 1046 Hz) e un
     * inviluppo a 250 Hz, cioe' dodici punti per ciclo anche a 20 Hz. Il lobo della media
     * mobile a 20 Hz vale 0.989: un per cento di pendenza, dieci volte sotto la tolleranza.
     */
    static constexpr int kRmsWindow = 192;

    /**
     * La nota della misura. Acuta apposta: fra il ciclo della nota e quello dell'LFO deve
     * entrarci una finestra d'RMS, e a 130 Hz (MIDI 48) i due sono separati da un fattore sei,
     * troppo poco perche' una finestra esista. A 1046 Hz il fattore e' cinquanta.
     */
    static constexpr int kMidiNote = 84;

    /** Il grezzo di `lrate` che da' quella frequenza: la mappa e' Log 0.05..20 Hz, quindi il
        grezzo non e' la frequenza divisa per venti. */
    static float lfoRateRawFor (float hz)
    {
        constexpr auto* spec = params::find ("lrate");
        static_assert (spec != nullptr, "lrate non e' in ParameterTable.h");
        return params::normalise (*spec, hz);
    }

    /** La frequenza vera che ne esce: e' quella su cui si sintonizza la DFT, non quella chiesta. */
    static float lfoRateHzFor (float raw)
    {
        constexpr auto* spec = params::find ("lrate");
        static_assert (spec != nullptr, "lrate non e' in ParameterTable.h");
        return params::denormalise (*spec, raw);
    }

    /** Una nota tenuta con una sola route `lfo -> targetSlot`, tutto il resto fermo. */
    static engine::EngineParams heldNoteParams (params::ParamSlot targetSlot, float base, float rateHz)
    {
        auto p = defaultParams();
        p.attackSeconds = 0.001f;
        p.decaySeconds = 0.001f;
        p.sustain = 1.0f;        // l'inviluppo d'ampiezza e' piatto: ogni oscillazione e' dell'LFO
        p.releaseSeconds = 0.05f;
        p.modBase[(size_t) engine::modTargetIndexFor (targetSlot)] = base;

        p.lfoShapeIndex = 0;     // sine
        p.lfoRateRaw = lfoRateRawFor (rateHz);
        p.lfoSync = false;
        p.lfoRetrig = true;
        p.lfoFadeSeconds = 0.0f;
        p.lfoPhaseOffset01 = 0.0f;
        return p;
    }

    /** Inviluppo d'ampiezza dell'uscita: RMS su finestre consecutive di kRmsWindow campioni. */
    static std::vector<float> renderRmsEnvelope (dsp::WavetableStore& store,
                                                 const engine::EngineParams& params,
                                                 const engine::ModSnapshot& mods,
                                                 int totalSamples)
    {
        engine::SynthEngine synth;
        engine::EngineSpec spec;
        spec.sampleRate = kSampleRate;
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());

        auto p = params;
        p.mods = &mods;
        synth.setParams (p);
        synth.setMasterGainLinear (1.0f);

        std::vector<float> envelope;
        envelope.reserve ((size_t) (totalSamples / kRmsWindow + 1));

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, kMidiNote, 1.0f), 0);

        juce::AudioBuffer<float> buffer (2, 128);
        double sumSquares = 0.0;
        int count = 0;

        for (int start = 0; start < totalSamples; start += 128)
        {
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            const auto* d = buffer.getReadPointer (0);

            for (int i = 0; i < 128; ++i)
            {
                sumSquares += (double) d[i] * (double) d[i];

                if (++count == kRmsWindow)
                {
                    envelope.push_back ((float) std::sqrt (sumSquares / (double) kRmsWindow));
                    sumSquares = 0.0;
                    count = 0;
                }
            }
        }

        return envelope;
    }

    /** Ampiezza della componente a `rateHz` dell'inviluppo, in frazione del suo valor medio. */
    static float depthOf (const std::vector<float>& envelope, float rateHz, float discardSeconds)
    {
        const auto envelopeRate = kSampleRate / (double) kRmsWindow;
        const auto first = (size_t) (discardSeconds * (float) envelopeRate);

        if (first + 16 >= envelope.size())
            return 0.0f;

        const auto n = envelope.size() - first;

        double mean = 0.0;
        for (size_t k = 0; k < n; ++k)
            mean += (double) envelope[first + k];
        mean /= (double) n;

        const auto twoPi = 2.0 * juce::MathConstants<double>::pi;
        const auto omega = twoPi * (double) rateHz / envelopeRate;

        double re = 0.0;
        double im = 0.0;
        double windowSum = 0.0;

        for (size_t k = 0; k < n; ++k)
        {
            // Hann: la finestra d'analisi non contiene un numero intero di cicli, e senza
            // finestratura la dispersione spettrale del residuo continuo falserebbe la riga.
            const auto w = 0.5 - 0.5 * std::cos (twoPi * (double) k / (double) (n - 1));
            const auto v = (double) envelope[first + k] - mean;

            re += w * v * std::cos (omega * (double) k);
            im -= w * v * std::sin (omega * (double) k);
            windowSum += w;
        }

        // Fattore 2 e divisione per la somma della finestra: e' la normalizzazione che rende
        // l'ampiezza letta uguale a quella della sinusoide, guadagno coerente della Hann incluso.
        const auto amplitude = 2.0 * std::sqrt (re * re + im * im) / windowSum;

        return mean > 0.0 ? (float) (amplitude / mean) : 0.0f;
    }

    /** La profondita' vista all'uscita per una route su `targetSlot` a quella frequenza. */
    static float measureDepth (dsp::WavetableStore& store, params::ParamSlot targetSlot,
                               float base, float depth, float rateHz,
                               const engine::EngineParams& extra)
    {
        auto p = heldNoteParams (targetSlot, base, rateHz);
        p.cutoffHz = extra.cutoffHz;
        p.resonanceQ = extra.resonanceQ;

        engine::ModSnapshot mods;
        mods.count = 1;
        mods.routes[0] = { engine::ModSource::lfo, engine::modTargetIndexFor (targetSlot), depth };

        constexpr float settleSeconds = 0.5f;
        constexpr float periods = 8.0f;

        const auto trueHz = lfoRateHzFor (p.lfoRateRaw);
        const auto seconds = settleSeconds + periods / trueHz;
        const auto totalSamples = ((int) (seconds * (float) kSampleRate) / 128 + 1) * 128;

        const auto envelope = renderRmsEnvelope (store, p, mods, totalSamples);
        return depthOf (envelope, trueHz, settleSeconds);
    }

    void runTest() override
    {
        // Da 2 a 20 Hz: 2 Hz sta comodamente dentro la banda passante dello smoother, 20 Hz e'
        // il massimo che `lrate` sa produrre. Se lo smoothing tocca la modulazione, la serie
        // scende; se non la tocca, resta piatta.
        const float rates[] = { 2.0f, 4.0f, 8.0f, 16.0f, 20.0f };

        // Piatta vuol dire piatta: il solo scostamento ammesso e' quello della misura, non una
        // pendenza. La finestra d'RMS a 20 Hz attenua dello 0.1 %, il mantenimento a tasso di
        // controllo di un altro 0.01 %.
        constexpr float kFlat = 0.90f;

        beginTest ("lfo -> level: la profondita' non dipende dalla frequenza dell'LFO");
        {
            dsp::WavetableStore store; store.setActive (1);

            auto extra = defaultParams();

            std::vector<float> depths;
            for (const auto rate : rates)
                depths.push_back (measureDepth (store, params::ParamSlot::level, 0.5f, 0.4f, rate, extra));

            for (size_t i = 0; i < depths.size(); ++i)
                logMessage ("level: LFO " + juce::String (rates[i], 1) + " Hz -> profondita' "
                            + juce::String (depths[i], 4) + " ("
                            + juce::String (depths[0] > 0.0f ? depths[i] / depths[0] : 0.0f, 3) + " della prima)");

            expect (depths[0] > 0.05f, "a 2 Hz non si misura modulazione: il test non proverebbe niente");

            for (size_t i = 1; i < depths.size(); ++i)
                expect (depths[i] > kFlat * depths[0],
                        "a " + juce::String (rates[i], 1) + " Hz la profondita' e' scesa a "
                            + juce::String (depths[i] / depths[0], 3) + " di quella a 2 Hz");
        }

        beginTest ("lfo -> cutoff: la profondita' non dipende dalla frequenza dell'LFO");
        {
            dsp::WavetableStore store; store.setActive (1);

            // Il passa-basso a 24 dB/ottava fa da rivelatore: la spazzata porta il cutoff da
            // 390 a 2700 Hz attorno alla fondamentale (1046 Hz), quindi l'energia in uscita la
            // segue con un'escursione larga. Q resta a Butterworth apposta: con la risonanza il
            // picco passerebbe *sopra* la fondamentale due volte per ciclo, e l'inviluppo
            // guadagnerebbe una componente al doppio della frequenza dell'LFO invece che alla
            // sua — la riga che si legge non sarebbe piu' quella della modulazione.
            auto extra = defaultParams();

            std::vector<float> depths;
            for (const auto rate : rates)
                depths.push_back (measureDepth (store, params::ParamSlot::cutoff, 0.57f, 0.14f, rate, extra));

            for (size_t i = 0; i < depths.size(); ++i)
                logMessage ("cutoff: LFO " + juce::String (rates[i], 1) + " Hz -> profondita' "
                            + juce::String (depths[i], 4) + " ("
                            + juce::String (depths[0] > 0.0f ? depths[i] / depths[0] : 0.0f, 3) + " della prima)");

            expect (depths[0] > 0.05f, "a 2 Hz non si misura modulazione: il test non proverebbe niente");

            for (size_t i = 1; i < depths.size(); ++i)
                expect (depths[i] > kFlat * depths[0],
                        "a " + juce::String (rates[i], 1) + " Hz la profondita' e' scesa a "
                            + juce::String (depths[i] / depths[0], 3) + " di quella a 2 Hz");
        }
    }
};

static ModulationDepthTests modulationDepthTests;
