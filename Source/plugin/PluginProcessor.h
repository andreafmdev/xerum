#pragma once

#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/MeterFrame.h"
#include "engine/SynthEngine.h"
#include "parameters/ParameterLayout.h"
#include "parameters/ParameterTable.h"
#include "parameters/StateTree.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

class SerumStyleSynthAudioProcessor final : public juce::AudioProcessor,
                                             private juce::AudioProcessorValueTreeState::Listener,
                                             private juce::Timer
{
public:
    SerumStyleSynthAudioProcessor();
    ~SerumStyleSynthAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }

    /** Shared with the editor's on-screen keyboard; merged into the MIDI stream in processBlock(). */
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState_; }

    /** Picchi per blocco pubblicati dal thread audio; letti dal timer dell'editor (task successivo). */
    engine::MeterFrame& getMeters() noexcept { return meters_; }

    /** Notifica che replaceState() ha sostituito l'albero: chi ascolta il ValueTree deve riagganciarsi. */
    juce::ChangeBroadcaster& getStateReplacedBroadcaster() noexcept { return stateReplaced_; }

private:
    /** Indice `wtIndex` corrente, come intero valido per WavetableStore. */
    int wavetableIndexFromParam() const noexcept;

    /** Legge tutti gli atomici dell'APVTS e li denormalizza in una EngineParams. Thread audio,
        una volta per blocco: nessuna ricerca nella tabella dei parametri, gli spec sono già
        risolti nel costruttore. */
    engine::EngineParams collectParams() const noexcept;

    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;

    juce::AudioProcessorValueTreeState apvts_;
    juce::MidiKeyboardState keyboardState_;
    std::unique_ptr<engine::SynthEngine> engine_;
    engine::MeterFrame meters_;
    juce::ChangeBroadcaster stateReplaced_;

    // Puntatori grezzi ai valori normalizzati 0..1 dell'APVTS: letti solo con load() sul thread
    // audio, e passati a params::collectEngineParams() (ParamCollect.h) tramite una lambda.
    // Le spec di denormalizzazione non servono piu' qui: collectEngineParams le risolve da
    // sola a tempo di compilazione (vedi il commento su quella funzione).
    std::atomic<float>* paramOscOn_ { nullptr };
    std::atomic<float>* paramWtIndex_ { nullptr };
    std::atomic<float>* paramWtpos_ { nullptr };
    std::atomic<float>* paramOct_ { nullptr };
    std::atomic<float>* paramSemi_ { nullptr };
    std::atomic<float>* paramFine_ { nullptr };
    std::atomic<float>* paramLevel_ { nullptr };
    std::atomic<float>* paramFiltOn_ { nullptr };
    std::atomic<float>* paramFtype_ { nullptr };
    std::atomic<float>* paramSlope_ { nullptr };
    std::atomic<float>* paramCutoff_ { nullptr };
    std::atomic<float>* paramRes_ { nullptr };
    std::atomic<float>* paramDrive_ { nullptr };
    std::atomic<float>* paramKeytrk_ { nullptr };
    std::atomic<float>* paramAtt_ { nullptr };
    std::atomic<float>* paramDec_ { nullptr };
    std::atomic<float>* paramSus_ { nullptr };
    std::atomic<float>* paramRel_ { nullptr };
    std::atomic<float>* paramEnvVel_ { nullptr };
    std::atomic<float>* paramVolume_ { nullptr };
    std::atomic<float>* paramPan_ { nullptr };
    std::atomic<float>* paramBypass_ { nullptr };

    dsp::WavetableStore wavetables_;
    int lastWavetableIndex_ { -1 };

    // prepareToPlay() e timerCallback() possono girare su thread diversi (nello Standalone
    // prepareToPlay non è detto sia sul message thread) e mutano entrambi wavetables_ e
    // lastWavetableIndex_: senza questo lock sarebbe una race genuina su tables_ (non atomico).
    // Nessuno dei due gira mai dentro processBlock, quindi il lock non tocca il thread audio.
    juce::CriticalSection wavetableLock_;

    // parameterChanged() può arrivare dal thread audio durante l'automazione host: si limita a
    // marcare il flag. Il timer (message thread) fa il lavoro vero, che alloca.
    std::atomic<bool> wavetableDirty_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SerumStyleSynthAudioProcessor)
};
