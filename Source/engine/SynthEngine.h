#pragma once

#include "engine/EngineParams.h"
#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace engine
{
struct EngineSpec
{
    double sampleRate { 44100.0 };
    int maximumBlockSize { 512 };
    int numChannels { 2 };
};

/** Real-time synth core. Owned by PluginProcessor; no UI / allocations here. */
class SynthEngine
{
public:
    void prepare (const EngineSpec& spec) noexcept;
    void reset() noexcept;

    /** Apply MIDI for this block then render. Buffers must be cleared by caller. */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    void setMasterGainLinear (float gain) noexcept;

    /** Propaga i parametri del blocco alle voci. Thread audio, una volta per blocco. */
    void setParams (const EngineParams& p) noexcept;

    /** Propaga subito la tavola attiva alle voci. Non real-time safe quanto a chi la chiama:
        usarla solo quando il thread audio non gira ancora (prepareToPlay). */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /**
     * Pubblica una nuova tavola da applicare alla prossima process(). Lock-free, chiamabile
     * da qualunque thread (il message thread, quando wtIndex cambia a runtime): SynthVoice::
     * setWavetable muta più campi non atomici, farlo da un thread diverso da quello audio
     * mentre render() legge sarebbe una race. Qui si pubblica solo il puntatore; l'applicazione
     * vera avviene dentro process(), sul thread audio.
     */
    void setPendingWavetable (const dsp::MipTable* table) noexcept;

private:
    void handleMidiEvent (const juce::MidiMessage& message) noexcept;

    VoiceManager voices_;
    EngineSpec spec_ {};
    EngineParams params_ {};
    float masterGain_ { 1.0f };
    float previousMasterGain_ { 1.0f }; // per rampare il gain fra un blocco e l'altro, vedi process()
    std::atomic<const dsp::MipTable*> pendingWavetable_ { nullptr };
};
} // namespace engine
