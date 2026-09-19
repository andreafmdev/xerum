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
/** Copie dell'oscillatore per voce al massimo dell'unison. Array a dimensione fissa, come il
    pool di voci: nessuna allocazione sul thread audio, mai. */
inline constexpr int kMaxUnison = 8;

/**
 * Posizione della copia `index` fra `voices` sull'asse -1..+1: -1 la prima, +1 l'ultima, e per
 * costruzione la somma su tutte le copie e' zero. Con una copia sola vale **esattamente** 0.
 *
 * E' la distribuzione usata sia per il detune (moltiplicata per i cent) sia per lo spread
 * stereo (moltiplicata per kUnisonSpreadWidth): una sola formula, cosi' le due immagini —
 * quella tonale e quella spaziale — non possono scivolare l'una rispetto all'altra.
 *
 * Lo zero esatto a `voices == 1` non e' un dettaglio: da li' discende che il rapporto di
 * frequenza sia exp2(0) = 1.0f e l'offset di pan 0.0f, cioe' che con unison 1 il segnale resti
 * identico campione per campione a quello di prima dell'unison.
 */
constexpr float unisonSpread (int index, int voices) noexcept
{
    return voices > 1 ? 2.0f * (float) index / (float) (voices - 1) - 1.0f : 0.0f;
}

/**
 * Larghezza dello spread stereo delle copie, in unita' di pan (-1..+1), attorno al pan della
 * voce. Fissa: non esiste un parametro `width` e non va aggiunto.
 *
 * 0.6 e non 1.0 per due ragioni. La prima e' che a larghezza piena le due copie estreme
 * finiscono **completamente** in un canale solo, e un orecchio ne perde meta' della stirpe;
 * qui la copia piu' esterna sta a circa 10 dB di sbilanciamento L/R, larga senza diventare una
 * sorgente puntiforme sull'altoparlante. La seconda e' che lo spread si somma al pan della voce
 * e poi si limita a +-1: con larghezza piena il grappolo sarebbe gia' attaccato a entrambi i
 * bordi al centro della corsa, e il knob `pan` smetterebbe di fare qualcosa. Con 0.6 restano
 * 0.4 di corsa per lato prima che il limite morda.
 *
 * Il guadagno non ne risente: con pan a potenza costante la somma dei cos^2 su un insieme
 * simmetrico di posizioni vale N*cos^2(pan), quindi la compensazione 1/sqrt(N) resta esatta
 * canale per canale qualunque sia la larghezza (finche' il limite non interviene).
 */
inline constexpr float kUnisonSpreadWidth = 0.6f;

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

    /** Ricalcola rapporti di detune e guadagno di compensazione. Gira solo quando `unison` o
        `detune` cambiano davvero: exp2() non ha niente da fare in un loop per campione. */
    void updateUnison (int voices, float detuneCents) noexcept;

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

    /** Le otto copie possibili; ne girano `unisonVoices_`. L'array e' sempre grande otto:
        allocarlo a runtime sarebbe vietato, e ottanta byte per voce non si notano. */
    std::array<dsp::WavetableOscillator, kMaxUnison> oscillators_;

    /** Il filtro del canale sinistro — l'unico quando unison e' 1, perche' in quel caso la
        voce e' monofonica fino al pan finale, esattamente come prima. */
    dsp::StateVariableFilter filter_;

    /** Il filtro del canale destro, usato solo con unison > 1. Con le copie sparse nel campo
        stereo i due canali portano miscele diverse, e un filtro solo le rifonderebbe in una:
        lo spread non sopravviverebbe al filtro, che e' il punto di averlo. */
    dsp::StateVariableFilter filterRight_;

    dsp::ADSREnvelope envelope_;

    bool oscOn_ { true };
    bool filterOn_ { true };
    float driveGain_ { 1.0f };
    float tuningSemitones_ { 0.0f };
    float velocityAmount_ { 0.0f };
    float baseCutoffHz_ { 1000.0f };
    float keyTrack_ { 0.0f };

    // --- unison ---

    int unisonVoices_ { 1 };
    float detuneCents_ { 0.0f };

    /** Rapporto di frequenza di ogni copia, exp2(cent / 1200). Calcolato in updateUnison(),
        cioe' a cambio di parametro, mai per campione. Parte da 1 su tutte e otto: con unison 1
        la copia zero deve moltiplicare la frequenza per *esattamente* 1.0f. */
    std::array<float, kMaxUnison> detuneRatio_ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    /** 1/sqrt(N): N copie scorrelate sommano in potenza, non in ampiezza, quindi crescono come
        la radice. Senza, passare da unison 1 a unison 8 aggiungerebbe circa 9 dB. */
    float unisonGain_ { 1.0f };

    /** Guadagni di pan per copia, ricalcolati una volta per blocco: N coppie invece di una, ma
        sempre fuori dal loop per campione — cos()/sin() li' dentro sono vietati. */
    std::array<float, kMaxUnison> unisonGainL_ {};
    std::array<float, kMaxUnison> unisonGainR_ {};

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
