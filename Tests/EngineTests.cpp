#include "dsp/MipTable.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"
#include "parameters/ParameterTable.h"

#include <juce_core/juce_core.h>

#include <cmath>

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
