#pragma once

#include "dsp/ADSREnvelope.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace engine
{
/** Single synth voice: legge una EngineParams per blocco e sintetizza. */
class SynthVoice
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void start (int midiNote, float velocity) noexcept;
    void stop() noexcept;
    void kill() noexcept;

    bool isActive() const noexcept;
    int getMidiNote() const noexcept;

    /** Mix into stereo buffers (additive). Real-time safe. */
    void render (float* outL, float* outR, int numSamples) noexcept;

    /** La tavola attiva, o nullptr. Chiamata dal thread audio (SynthEngine::process). */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /** Applica i parametri del blocco. Nessun atomico, nessuna allocazione. */
    void setParams (const EngineParams& p) noexcept;

private:
    void updateCutoff (bool snap) noexcept;

    double sampleRate_ { 44100.0 };
    bool active_ { false };
    int midiNote_ { -1 };
    float velocity_ { 0.0f };
    float frequencyHz_ { 440.0f };

    dsp::WavetableOscillator oscillator_;
    dsp::StateVariableFilter filter_;
    dsp::ADSREnvelope envelope_;

    bool oscOn_ { true };
    bool filterOn_ { true };
    float driveGain_ { 1.0f };
    float tuningSemitones_ { 0.0f };
    float velocityAmount_ { 0.0f };
    float baseCutoffHz_ { 1000.0f };
    float keyTrack_ { 0.0f };

    // Rampe di 20 ms: evitano gradini udibili quando l'host automatizza un parametro
    // a scatti fra un blocco e l'altro. Cutoff e Position restano costosi (tan(),
    // ricalcolo degli indici di frame) e si aggiornano una sola volta per blocco;
    // Level e Pan sono economici e si aggiornano campione per campione.
    juce::SmoothedValue<float> smoothedCutoff_;
    juce::SmoothedValue<float> smoothedFramePosition_;
    juce::SmoothedValue<float> smoothedLevel_;
    juce::SmoothedValue<float> smoothedPan_;
};
} // namespace engine
