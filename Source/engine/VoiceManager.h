#pragma once

#include "dsp/MipTable.h"
#include "engine/EngineParams.h"
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

    /** Propaga la tavola attiva a tutte le voci. Chiamata dal thread audio. */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /** Propaga i parametri del blocco a tutte le voci, attive o no. */
    void setParams (const EngineParams& p) noexcept;

    /** Il livello dell'LFO della prima voce attiva, per il meter. Zero se non suona niente.
        Non esiste una setGlobalLfoLevel simmetrica: il livello dell'LFO libero arriva alle
        voci dentro EngineParams::globalLfoLevel, e un secondo canale per lo stesso dato
        sarebbe solo una via in piu' da tenere sincronizzata. */
    float getLfoLevel() const noexcept
    {
        for (const auto& voice : voices_)
            if (voice.isActive())
                return voice.getLfoLevel();

        return 0.0f;
    }

private:
    SynthVoice* findFreeVoice() noexcept;
    SynthVoice* findVoiceForNote (int midiNote) noexcept;
    SynthVoice* stealVoice() noexcept;

    std::array<SynthVoice, maxVoices> voices_ {};
    int roundRobin_ { 0 };
};
} // namespace engine
