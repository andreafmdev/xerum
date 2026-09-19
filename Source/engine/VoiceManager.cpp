#include "engine/VoiceManager.h"

namespace engine
{
void VoiceManager::prepare (double sampleRate) noexcept
{
    for (auto& voice : voices_)
        voice.prepare (sampleRate);

    roundRobin_ = 0;
}

void VoiceManager::reset() noexcept
{
    for (auto& voice : voices_)
        voice.reset();

    roundRobin_ = 0;
}

SynthVoice* VoiceManager::findFreeVoice() noexcept
{
    for (auto& voice : voices_)
        if (! voice.isActive())
            return &voice;

    return nullptr;
}

SynthVoice* VoiceManager::findVoiceForNote (int midiNote) noexcept
{
    for (auto& voice : voices_)
        if (voice.isActive() && voice.getMidiNote() == midiNote)
            return &voice;

    return nullptr;
}

SynthVoice* VoiceManager::stealVoice() noexcept
{
    // Round-robin steal — phase 1; later: steal quietest / oldest.
    auto& voice = voices_[static_cast<size_t> (roundRobin_ % maxVoices)];
    roundRobin_ = (roundRobin_ + 1) % maxVoices;
    voice.kill();
    return &voice;
}

void VoiceManager::noteOn (int midiNote, float velocity) noexcept
{
    // Nota ribattuta mentre suona ancora: si riparte sulla stessa voce invece di ucciderla e
    // riprenderla da capo. kill() azzerava inviluppo, fase e filtro in un colpo, cioe' un
    // gradino da ampiezza piena a zero fra due campioni: il clic che si sentiva a ogni nota
    // ripetuta, anche legato.
    if (auto* existing = findVoiceForNote (midiNote))
    {
        existing->retrigger (velocity);
        return;
    }

    SynthVoice* voice = findFreeVoice();
    if (voice == nullptr)
        voice = stealVoice();

    voice->start (midiNote, velocity);
}

void VoiceManager::noteOff (int midiNote) noexcept
{
    if (auto* voice = findVoiceForNote (midiNote))
        voice->stop();
}

void VoiceManager::allNotesOff() noexcept
{
    for (auto& voice : voices_)
        voice.stop();
}

void VoiceManager::allSoundOff() noexcept
{
    for (auto& voice : voices_)
        voice.kill();
}

void VoiceManager::render (float* outL, float* outR, int numSamples) noexcept
{
    for (auto& voice : voices_)
        voice.render (outL, outR, numSamples);
}

void VoiceManager::setWavetable (const dsp::MipTable* table) noexcept
{
    for (auto& voice : voices_)
        voice.setWavetable (table);
}

void VoiceManager::setParams (const EngineParams& p) noexcept
{
    for (auto& voice : voices_)
        voice.setParams (p);
}

void VoiceManager::setGlobalLfoLevel (float level) noexcept
{
    for (auto& voice : voices_)
        voice.setGlobalLfoLevel (level);
}
} // namespace engine
