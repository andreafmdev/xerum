#pragma once

namespace dsp
{
/**
 * SVF topology-preserving (Zavalishin): resta stabile anche quando il cutoff
 * viene modulato in fretta, che è la ragione per cui non usiamo una biquad.
 *
 * `g = tan(π · fc / sr)` è l'unico `tan` del percorso e si ricalcola solo in
 * setCutoffHz(), che il chiamante invoca una volta per blocco.
 *
 * Gestione della risonanza (due scelte deliberate — vedi anche updateCoefficients):
 *  - in cascata a 24 dB la risonanza sta **solo sull'ultimo stadio**, il primo resta
 *    Butterworth. Mettendola su entrambi il picco andrebbe a Q², cioè +52 dB a Q 20;
 *  - l'ingresso viene attenuato di `(Qbutter / Q)^(1/8)`: limita il picco di 2.9 dB a
 *    Q 24 e costa 3.5 dB di banda passante allo stesso Q. E' il massimo che sta dentro il
 *    budget di 4 dB: alzare `res` deve far squillare il filtro, non abbassare il volume
 *    dello strumento, e una compensazione forte (prima era l'esponente 1/2) costava
 *    -14.7 dB a Q 24 — cioe' rendeva magro lo strumento invece di domarne il picco.
 */
class StateVariableFilter
{
public:
    enum class Type { lowPass, highPass, bandPass };

    /** Q neutro (Butterworth): nessun picco, nessuna attenuazione d'ingresso. */
    static constexpr float kButterworthQ = 0.707f;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setType (Type type) noexcept { type_ = type; }

    /** Il cutoff viene comunque limitato a 0.49 · sampleRate: oltre, tan() esplode. */
    void setCutoffHz (float hz) noexcept;

    /** Fattore di qualità: 0.707 = Butterworth, valori alti = risonanza. */
    void setResonance (float q) noexcept;

    /** 1 stadio = 12 dB/ottava, 2 = 24. */
    void setNumStages (int stages) noexcept;

    float processSample (float input) noexcept;

private:
    struct Stage
    {
        float s1 { 0.0f };
        float s2 { 0.0f };
    };

    /** Coefficienti dipendenti da Q: `g` è comune, questi no (vedi commento di classe). */
    struct Coefficients
    {
        float twoR { 1.414f };
        float denominator { 1.0f };
    };

    float processStage (Stage& stage, const Coefficients& c, float input) const noexcept;
    void updateCoefficients() noexcept;

    double sampleRate_ { 44100.0 };
    Type type_ { Type::lowPass };
    float cutoffHz_ { 1000.0f };
    float resonance_ { kButterworthQ };
    int numStages_ { 1 };

    float g_ { 0.0f };
    float inputGain_ { 1.0f };
    Coefficients butterworth_ {};
    Coefficients resonant_ {};

    Stage stages_[2];
};
} // namespace dsp
