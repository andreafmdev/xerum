#pragma once

#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/MeterFrame.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "parameters/ParamCollect.h"
#include "parameters/ParameterLayout.h"
#include "parameters/ParameterTable.h"
#include "parameters/StateTree.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>

class SerumStyleSynthAudioProcessor final : public juce::AudioProcessor,
                                             private juce::AudioProcessorValueTreeState::Listener,
                                             private juce::ValueTree::Listener,
                                             private juce::AsyncUpdater,
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

    /** Riaggancia il listener del ValueTree a `root`, staccandolo dall'albero precedente.
        Serve perché `apvts_.replaceState()` (setStateInformation) non modifica l'albero
        esistente: lo *sostituisce*. Un listener rimasto su quello vecchio smetterebbe
        semplicemente di ricevere eventi, e lo snapshot resterebbe fermo sulle assegnazioni del
        preset precedente senza che niente segnali l'errore. Stesso pattern di
        bridge::StateChannel::listenTo(). */
    void listenToState (juce::ValueTree root);

    /** Traduce il nodo MODS in un engine::ModSnapshot e lo pubblica al motore. Message thread:
        qui si confrontano stringhe e si costruisce lo snapshot sullo stack. Al thread audio
        arriva solo un puntatore a uno slot preallocato (vedi engine::SynthEngine::setMods). */
    void rebuildModSnapshot();

    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;

    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override {}
    void valueTreeParentChanged (juce::ValueTree&) override {}
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts_;

    // L'albero a cui il listener è effettivamente agganciato. Tenerlo a parte da apvts_.state
    // è ciò che permette di staccarsi da quello *vecchio* dopo un replaceState: a quel punto
    // apvts_.state punta già altrove e non saprebbe più da dove rimuoversi.
    juce::ValueTree listenedState_;

    juce::MidiKeyboardState keyboardState_;
    std::unique_ptr<engine::SynthEngine> engine_;
    engine::MeterFrame meters_;
    juce::ChangeBroadcaster stateReplaced_;

    // Puntatore grezzo dell'APVTS (letto solo con load() sul thread audio) per ognuno dei
    // parametri che params::collectEngineParams() (ParamCollect.h) consuma, indicizzato da
    // params::ParamSlot. Risolto una volta sola nel costruttore ciclando su params::kSlotIds,
    // che e' generato da parameters.json insieme all'enum: l'accessore passato a
    // collectEngineParams e' quindi una singola lettura d'array, senza hashing ne' confronto di
    // stringhe per blocco. wtIndex e volume restano puntatori a parte: non passano da
    // EngineParams (vedi wavetableIndexFromParam()/processBlock()).
    std::array<std::atomic<float>*, (size_t) params::ParamSlot::count> paramSlots_ {};
    std::atomic<float>* paramWtIndex_ { nullptr };
    std::atomic<float>* paramVolume_ { nullptr };

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
