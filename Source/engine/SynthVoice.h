#pragma once

#include "dsp/ADSREnvelope.h"
#include "dsp/Lfo.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstddef>

namespace engine
{
/** Single synth voice: legge una EngineParams per blocco e sintetizza. */
class SynthVoice
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void start (int midiNote, float velocity) noexcept;

    /** Ribattuta della nota gia' assegnata a questa voce: fa ripartire il solo inviluppo.
        Fase dell'oscillatore e stato del filtro restano dove sono — vedi il commento
        nell'implementazione: e' quello che tiene continuo il segnale. */
    void retrigger (float velocity) noexcept;
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

    /** Il livello dell'LFO di questa voce: lo legge SynthEngine per il meter. */
    float getLfoLevel() const noexcept { return lfoRetrig_ ? lfo_.level() : globalLfoLevel_; }

private:
    void updateCutoff (bool snap) noexcept;

    /** Ricalcola i livelli delle quattro sorgenti e riapplica i sette target modulabili.
        Gira una volta per blocco (o per fetta fra due eventi MIDI), mai per campione. */
    void applyModulation() noexcept;

    /** Somma le route che puntano a `targetIndex` sul valore normalizzato di base, e clampa.
        Stessa aritmetica di liveValue() in WebUI/src/synth/mod.ts. */
    float modulated (int targetIndex) const noexcept;

    /** Vero se almeno una route punta a quel target. Vedi il commento di modMask_. */
    bool isModulated (int targetIndex) const noexcept
    {
        return targetIndex >= 0 && (modMask_ & (1u << (unsigned) targetIndex)) != 0u;
    }

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

    // --- modulazione ---

    /** I parametri del blocco per intero: la modulazione si valuta in render(), non in
        setParams(), quindi servono ancora dopo che il processore li ha depositati. */
    EngineParams params_ {};

    dsp::Lfo lfo_;

    /** I livelli delle quattro sorgenti, nell'ordine di engine::ModSource. */
    std::array<float, (size_t) ModSource::count> sourceLevels_ {};

    /**
     * Un bit per target modulabile: acceso quando almeno una route punta li'.
     *
     * Serve a tenere separati i due percorsi. Per un target senza route la voce continua a
     * usare il valore gia' denormalizzato che arriva in EngineParams, cioe' esattamente il
     * codice di prima della modulazione: la non-regressione diventa cosi' una proprieta'
     * strutturale invece di una coincidenza numerica fra due formule che devono combaciare.
     * Come effetto secondario, a matrix vuoto si risparmiano sette conversioni per blocco e
     * per voce, due delle quali con std::pow.
     */
    unsigned int modMask_ { 0 };

    float globalLfoLevel_ { 0.0f };
    bool lfoRetrig_ { true };
    float lfoPhaseOffset01_ { 0.0f };
};
} // namespace engine
