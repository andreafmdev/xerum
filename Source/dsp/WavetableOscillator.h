#pragma once

#include "dsp/MipTable.h"

#include <juce_core/juce_core.h>

namespace dsp
{
/** Il livello più corto le cui armoniche stanno tutte sotto Nyquist a questa frequenza. */
int levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept;

/**
 * Oscillatore wavetable con doppia interpolazione: lineare fra campioni adiacenti
 * dentro il frame, lineare fra i due frame adiacenti alla posizione. È la seconda
 * che produce il morph: senza, muovere Position dà scatti.
 */
class WavetableOscillator
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Tavola attiva; nullptr significa silenzio, non crash. */
    void setTable (const MipTable* table) noexcept;

    void setFrequencyHz (float hz) noexcept;

    /** Posizione nel morph, 0..1 sull'intero set di frame. */
    void setFramePosition (float normalised) noexcept;

    float getSample() noexcept;

private:
    void updateLevel() noexcept;

    double sampleRate_ { 44100.0 };
    double phase_ { 0.0 };
    double phaseIncrement_ { 0.0 };
    float frequencyHz_ { 0.0f };

    const MipTable* table_ { nullptr };
    int level_ { 0 };
    int frameLo_ { 0 };
    int frameHi_ { 0 };
    float frameMix_ { 0.0f };
};
} // namespace dsp
