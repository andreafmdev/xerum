#include "engine/SynthEngine.h"

#include "parameters/ParameterDenormalise.h"
#include "parameters/ParameterTable.h"

#include <algorithm>
#include <cmath>


namespace engine
{
namespace
{
/** Sopra questa soglia l'uscita smette di essere lineare e comincia a piegare. */
constexpr float kSoftClipThreshold = 0.8f;

/**
 * Rete di sicurezza sull'uscita: identica all'ingresso fino a 0.8, poi piega dolcemente e
 * non supera mai 1.0. Serve perche' il guadagno per voce e' una costante (vedi
 * SynthVoice::kVoiceHeadroomGain): un accordo abbastanza fitto, o un volume master alto,
 * possono comunque superare il fondo scala, e senza questo l'host riceverebbe campioni
 * troncati a zero decibel — il clipping digitale netto, quello che si sente come strappo.
 *
 * Lineare sotto soglia: niente distorsione aggiunta al segnale normale, a differenza di un
 * soft clipper polinomiale attivo su tutta la corsa. La derivata vale 1 alla soglia, quindi
 * non c'e' spigolo nel punto di innesto. Nessuna libm: un confronto, una divisione.
 */
float softClip (float x) noexcept
{
    const auto magnitude = std::abs (x);

    if (magnitude <= kSoftClipThreshold)
        return x;

    const auto headroom = 1.0f - kSoftClipThreshold;
    const auto over = (magnitude - kSoftClipThreshold) / headroom;
    const auto bent = kSoftClipThreshold + headroom * (over / (1.0f + over));

    return x < 0.0f ? -bent : bent;
}
} // namespace

void SynthEngine::prepare (const EngineSpec& spec) noexcept
{
    spec_ = spec;
    voices_.prepare (spec_.sampleRate);
    globalLfo_.prepare (spec_.sampleRate);
}

void SynthEngine::reset() noexcept
{
    voices_.reset();
}

void SynthEngine::setMasterGainLinear (float gain) noexcept
{
    masterGain_ = gain;
}

void SynthEngine::setParams (const EngineParams& p) noexcept
{
    params_ = p;
    voices_.setParams (p);
}

void SynthEngine::setWavetable (const dsp::MipTable* table) noexcept
{
    voices_.setWavetable (table);
}

void SynthEngine::setPendingWavetable (const dsp::MipTable* table) noexcept
{
    pendingWavetable_.store (table, std::memory_order_release);
}

void SynthEngine::setMods (const engine::ModSnapshot& snapshot) noexcept
{
    // fetch_add invece di uno scrittore singolo: setMods puo' arrivare dal message thread e,
    // in futuro, da chi ricarica un preset. L'incremento atomico garantisce che due chiamate
    // concorrenti scelgano slot diversi, quindi nessuna delle due riscrive l'altra a meta'.
    const auto slot = modWriteSlot_.fetch_add (1, std::memory_order_relaxed) & 3;
    modRing_[slot] = snapshot;
    activeMods_.store (&modRing_[slot], std::memory_order_release);
}

void SynthEngine::handleMidiEvent (const juce::MidiMessage& message) noexcept
{
    if (message.isController() && message.getControllerNumber() == 1)
    {
        // Mod wheel: sorgente `mw` del matrix, 0..1. Prima degli altri rami perche' e' il caso
        // piu' frequente fra i messaggi non di nota e non ha niente a che vedere con le voci.
        modWheel_ = (float) message.getControllerValue() / 127.0f;
        return;
    }

    if (message.isNoteOn())
    {
        voices_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        voices_.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff())
    {
        voices_.allNotesOff();
    }
    else if (message.isAllSoundOff())
    {
        voices_.allSoundOff();
    }
}

void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    if (params_.bypass)
    {
        // E' un sintetizzatore, non c'e' un ingresso da far passare: bypass significa silenzio
        // e nessuna voce che continua a suonare sotto sotto.
        voices_.allSoundOff();
        buffer.clear();
        return;
    }

    // Se il message thread ha pubblicato una nuova tavola (wtIndex cambiato), applicarla
    // qui: siamo sul thread audio, l'unico che può mutare in sicurezza lo stato delle voci.
    if (const auto* table = pendingWavetable_.exchange (nullptr, std::memory_order_acquire); table != nullptr)
        voices_.setWavetable (table);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // L'LFO libero avanza una volta per blocco, note o non note: e' cio' che lo rende "libero".
    // Con matrix vuoto non tocca niente — le voci leggono globalLfoLevel solo se una route
    // punta a un target — quindi farlo girare sempre non cambia di un campione chi non lo usa.
    constexpr auto* specLrate = params::find ("lrate");
    static_assert (specLrate != nullptr, "lrate non e' in ParameterTable.h");

    globalLfo_.setShape ((dsp::Lfo::Shape) juce::jlimit (0, 4, params_.lfoShapeIndex));
    globalLfo_.setFadeSeconds (0.0f); // la dissolvenza e' per nota: non ha senso sull'LFO libero
    globalLfo_.setFrequencyHz (params_.lfoSync
                                   ? dsp::syncedRateHz (params_.lfoRateRaw, (double) params_.bpm)
                                   : params::denormalise (*specLrate, params_.lfoRateRaw));
    const auto globalLevel = globalLfo_.advance (numSamples);

    // Se il message thread non ha ancora pubblicato niente si tiene il puntatore arrivato con
    // setParams(): e' nullptr in produzione (collectEngineParams non lo riempie) ed e' la via
    // con cui i test della voce iniettano uno snapshot senza passare da setMods().
    if (const auto* published = activeMods_.load (std::memory_order_acquire); published != nullptr)
        params_.mods = published;

    params_.modWheel = modWheel_;
    params_.globalLfoLevel = globalLevel;

    // Prima del ciclo MIDI, non dopo: una nota che parte in questo blocco deve gia' vedere le
    // modulazioni di questo blocco. Il mod wheel letto qui e' invece quello di fine blocco
    // precedente, perche' un CC 1 a meta' buffer viene gestito nel ciclo qui sotto e avra'
    // effetto dal blocco dopo — coerente con una modulazione a tasso di blocco.
    voices_.setParams (params_);

    lfoLevel_.store (params_.lfoRetrig ? voices_.getLfoLevel() : globalLevel,
                     std::memory_order_relaxed);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : left;

    int samplePos = 0;

    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);
        const int slice = eventPos - samplePos;

        if (slice > 0)
        {
            voices_.render (left + samplePos, right + samplePos, slice);
            samplePos = eventPos;
        }

        handleMidiEvent (metadata.getMessage());
    }

    if (samplePos < numSamples)
        voices_.render (left + samplePos, right + samplePos, numSamples - samplePos);

    // Rampato, non applicato di scatto: spec 6.4 elenca volume fra i cinque bersagli di
    // smoothing (cutoff, wtpos, level, volume, pan). Un salto a gain di blocco produrrebbe lo
    // stesso zipper noise che gli altri quattro evitano gia' — muovere il fader master o
    // automatizzarlo con lo step precedente avrebbe prodotto un gradino udibile a ogni blocco.
    buffer.applyGainRamp (0, numSamples, previousMasterGain_, masterGain_);
    previousMasterGain_ = masterGain_;

    // Dopo il volume master, mai prima: e' il livello che esce davvero dal plugin quello da
    // proteggere. Si passa sui canali reali del buffer (non su left/right, che in mono sono
    // lo stesso puntatore e verrebbe clippato due volte).
    for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            data[i] = softClip (data[i]);
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
}
} // namespace engine
