#pragma once

namespace dsp
{
/**
 * LFO a tasso di controllo: avanza una volta per blocco, non per campione.
 *
 * E' una scelta, non una semplificazione mancata. Cutoff e Position gia' si aggiornano una
 * volta per blocco perche' costano tan() e ricalcolo degli indici di frame; far girare l'LFO
 * per campione non renderebbe piu' fine la loro modulazione. Il render e' spezzato in sotto-fette di al piu' 32
 * campioni (SynthEngine::kControlBlockSamples), quindi il tasso di aggiornamento e'
 * 1500 Hz a 48 kHz qualunque buffer passi l'host, abbondante per un LFO che arriva a 20 Hz.
 * Il rovescio della medaglia: FM e AM audio-rate non sono esprimibili, e non sono un obiettivo.
 */
class Lfo
{
public:
    enum class Shape { sine, triangle, saw, square, sampleHold };

    void prepare (double sampleRate) noexcept;

    void setShape (Shape shape) noexcept { shape_ = shape; }
    void setFrequencyHz (float hz) noexcept;

    /** Durata della dissolvenza in entrata, dal retrigger. Zero: nessuna dissolvenza. */
    void setFadeSeconds (float seconds) noexcept;

    /** Riparte da `startPhase01` e azzera la dissolvenza. */
    void retrigger (float startPhase01) noexcept;

    /** Avanza di `numSamples` e ritorna il livello, -1..1, dissolvenza inclusa. */
    float advance (int numSamples) noexcept;

    /** Il livello corrente, senza avanzare. */
    float level() const noexcept { return level_; }

private:
    void updateLevel() noexcept;

    double sampleRate_ { 48000.0 };
    double phase_ { 0.0 };          // 0..1
    double phaseIncrement_ { 0.0 }; // per campione
    Shape shape_ { Shape::sine };

    double fadeSamples_ { 0.0 };
    double fadeProgress_ { 1.0 };   // 0..1
    float level_ { 0.0f };
};

/** Le stesse formule di lfoShape in WebUI/src/synth/mod.ts. */
float lfoShapeValue (Lfo::Shape shape, float phase01) noexcept;

/** Hz della divisione ritmica selezionata da `raw` (0..1) al tempo dato, in BPM. */
float syncedRateHz (float raw, double bpm) noexcept;
} // namespace dsp
