#include "engine/SynthEngine.h"

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

void SynthEngine::handleMidiEvent (const juce::MidiMessage& message) noexcept
{
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
