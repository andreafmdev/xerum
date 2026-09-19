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

    /** Propaga i parametri del blocco a tutte le voci, attive o no. Una volta per blocco. */
    void setParams (const EngineParams& p) noexcept;

    /**
     * Propaga il solo livello dell'LFO libero. Una volta per **sotto-fetta** di controllo.
     *
     * Questo metodo prima non c'era, ed era una decisione motivata: il livello dell'LFO libero
     * arriva gia' alle voci dentro EngineParams::globalLfoLevel, e un secondo canale per lo
     * stesso dato e' una via in piu' da tenere sincronizzata. Era la scelta giusta finche' le
     * fette per blocco erano quattro. Con le sotto-fette da 32 campioni sono sedici, e
     * ripubblicare l'intera EngineParams per far arrivare un float costava 240 chiamate a
     * SynthVoice::setParams in piu' per blocco (16 x 16 contro 4 x 16 di prima) — ognuna con
     * tre exp() per i coefficienti dell'inviluppo e una riscorsa della lista delle route.
     *
     * Il dato resta uno solo: SynthEngine pubblica EngineParams una volta per blocco, come
     * prima del tasso di controllo fisso, e da li' in poi muove solo questo.
     */
    void setGlobalLfoLevel (float level) noexcept;

    /** Il livello dell'LFO della prima voce attiva, per il meter. Zero se non suona niente. */
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
