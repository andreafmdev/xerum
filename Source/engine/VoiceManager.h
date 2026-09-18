#pragma once

#include "dsp/MipTable.h"
#include "engine/SynthVoice.h"

#include <array>
#include <cstddef>

namespace engine
{
/** Fixed voice pool — no heap activity on the audio thread. */
class VoiceManager
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void noteOn (int midiNote, float velocity) noexcept;
    void noteOff (int midiNote) noexcept;
    void allNotesOff() noexcept;
    void allSoundOff() noexcept;

    void render (float* outL, float* outR, int numSamples) noexcept;

    /** Propaga la tavola attiva a tutte le voci. Message thread. */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /** Propaga la posizione del morph a tutte le voci. */
    void setFramePosition (float normalised) noexcept;

private:
    SynthVoice* findFreeVoice() noexcept;
    SynthVoice* findVoiceForNote (int midiNote) noexcept;
    SynthVoice* stealVoice() noexcept;

    std::array<SynthVoice, maxVoices> voices_ {};
    int roundRobin_ { 0 };
};
} // namespace engine
