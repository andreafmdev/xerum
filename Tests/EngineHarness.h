#pragma once

/**
 * I tre helper con cui si monta un motore in un test: prepararlo, dargli parametri sensati e
 * misurarne la fondamentale senza usare la formula che il motore usa internamente.
 *
 * Stavano nel namespace anonimo di EngineTests.cpp, cioe' irraggiungibili da un altro file di
 * test. Qui il codice e' lo stesso, riga per riga: se un test cambia comportamento dopo questa
 * estrazione, e' la copia a essere sbagliata, non l'estrazione.
 */

#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace harness
{
inline constexpr double kSampleRate = 48000.0;
inline constexpr int kBlock = 128;

/** Parametri di base per far suonare una nota senza sorprese: attacco/rilascio brevi,
    filtro spalancato, niente pan/drive/keytrack. I singoli test alterano solo ciò che
    vogliono osservare. */
inline engine::EngineParams defaultParams()
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

/** SynthEngine tiene un std::atomic (la mailbox della wavetable): non e' copiabile ne'
    spostabile, quindi si prepara sul posto invece di ritornarlo per valore. */
inline void prepareEngine (engine::SynthEngine& synth, dsp::WavetableStore& store, int numChannels = 2)
{
    engine::EngineSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = kBlock;
    spec.numChannels = numChannels;
    synth.prepare (spec);
    synth.setWavetable (store.active());
}

/** Fa girare `settleSamples` di scarto (attacco/transiente) poi conta gli attraversamenti
    dello zero (da negativo a positivo) su `measureSamples`, per stimare la fondamentale senza
    dipendere dalla stessa formula usata internamente dal motore (altrimenti il test non
    proverebbe niente: userebbe la formula sbagliata per verificare se stessa). */
inline float measureFundamentalHz (engine::SynthEngine& synth, double sampleRate, int settleSamples, int measureSamples)
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

/** Il patch piu' semplice che sappia suonare: una copia sola, niente filtro, sustain pieno.
    Per chi misura intonazione e inviluppo: ogni stadio in piu' confonde la misura. */
inline engine::EngineParams plainPatch()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.framePosition = 0.0f;
    p.level = 1.0f;
    p.unisonVoices = 1;
    p.detuneCents = 0.0f;
    p.filterOn = false;
    p.cutoffHz = 20000.0f;
    p.resonanceQ = 0.707f;
    p.driveGain = 1.0f;
    p.keyTrack = 0.0f;
    p.attackSeconds = 0.001f;
    p.decaySeconds = 0.001f;
    p.sustain = 1.0f;
    p.releaseSeconds = 0.2f;
    p.velocityAmount = 0.0f;
    p.pan = 0.0f;
    return p;
}

/** Il punto di partenza dei test dello stadio FX: attacco e rilascio brevi, filtro spento. */
inline engine::EngineParams baseParams()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.level = 1.0f;
    p.filterOn = false;
    p.cutoffHz = 8000.0f;
    p.attackSeconds = 0.001f;
    p.decaySeconds = 0.01f;
    p.sustain = 1.0f;
    p.releaseSeconds = 0.05f;
    p.pan = 0.0f;
    return p;
}

/** I due canali interi resi da un motore. */
struct Rendered
{
    std::vector<float> left, right;
};

/** Rende `numBlocks` blocchi con una nota tenuta e restituisce i due canali interi. */
inline Rendered renderHeldNote (const engine::EngineParams& params, dsp::WavetableStore& store, int numBlocks,
                                float masterGain = 0.8f, int note = 60)
{
    engine::SynthEngine synth;
    prepareEngine (synth, store);
    synth.setParams (params);
    synth.setMasterGainLinear (masterGain);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);

    Rendered out;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        buffer.clear();
        synth.process (buffer, midi);
        midi.clear();

        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);
        out.left.insert (out.left.end(), l, l + kBlock);
        out.right.insert (out.right.end(), r, r + kBlock);
    }

    return out;
}

/** RMS di un vettore, dal campione `from` in poi. */
inline double rms (const std::vector<float>& x, size_t from = 0)
{
    double sum = 0.0;

    for (size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * (double) x[i];

    return x.size() > from ? std::sqrt (sum / (double) (x.size() - from)) : 0.0;
}

/** Correlazione normalizzata fra due vettori della stessa lunghezza. */
inline double correlation (const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;

    for (size_t i = 0; i < a.size(); ++i)
    {
        ab += (double) a[i] * (double) b[i];
        aa += (double) a[i] * (double) a[i];
        bb += (double) b[i] * (double) b[i];
    }

    return (aa > 0.0 && bb > 0.0) ? ab / std::sqrt (aa * bb) : 0.0;
}

/** Un pool di voci pronto a suonare, reso a sotto-fette come fa SynthEngine. Lo usano i test
    che parlano a VoiceManager senza passare dal motore (glide, modi di voce, pedale). */
struct VoicePool
{
    engine::VoiceManager voices;
    juce::AudioBuffer<float> scratch { 2, engine::SynthEngine::kControlBlockSamples };

    VoicePool (const engine::EngineParams& p, dsp::WavetableStore& store)
    {
        voices.prepare (kSampleRate);
        voices.setWavetable (store.active());
        voices.setParams (p);
    }

    /**
     * Rende `numSamples` campioni **in sotto-fette da kControlBlockSamples**, come fa
     * SynthEngine::renderControlSlices.
     *
     * Non e' un dettaglio del test: il glide avanza una volta per sotto-fetta, quindi renderne
     * 4800 in un colpo solo o in centocinquanta fette e' la differenza fra misurare il tasso di
     * controllo vero e misurarne uno inventato qui dentro.
     */
inline     void render (int numSamples)
    {
        for (int done = 0; done < numSamples; )
        {
            const auto slice = std::min (engine::SynthEngine::kControlBlockSamples, numSamples - done);
            scratch.clear();
            voices.render (scratch.getWritePointer (0), scratch.getWritePointer (1), slice);
            done += slice;
        }
    }

    /** La voce che sta suonando una certa nota, o nullptr. */
inline     const engine::SynthVoice* voiceFor (int midiNote) const
    {
        for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
        {
            const auto& voice = voices.getVoice (i);

            if (voice.isActive() && ! voice.isFading() && voice.getMidiNote() == midiNote)
                return &voice;
        }

        return nullptr;
    }

    /** La prima voce che occupa un posto nella polifonia, o nullptr. */
inline     const engine::SynthVoice* sounding() const
    {
        for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
            if (voices.getVoice (i).isActive() && ! voices.getVoice (i).isFading())
                return &voices.getVoice (i);

        return nullptr;
    }

    int countFading() const
    {
        int count = 0;

        for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
            if (voices.getVoice (i).isFading())
                ++count;

        return count;
    }
};

} // namespace harness
