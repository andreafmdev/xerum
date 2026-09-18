#pragma once

namespace dsp
{
/**
 * SVF topology-preserving (Zavalishin): resta stabile anche quando il cutoff
 * viene modulato in fretta, che è la ragione per cui non usiamo una biquad.
 *
 * `g = tan(π · fc / sr)` è l'unico `tan` del percorso e si ricalcola solo in
 * setCutoffHz(), che il chiamante invoca una volta per blocco.
 */
class StateVariableFilter
{
public:
    enum class Type { lowPass, highPass, bandPass };

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

    float processStage (Stage& stage, float input) const noexcept;
    void updateCoefficients() noexcept;

    double sampleRate_ { 44100.0 };
    Type type_ { Type::lowPass };
    float cutoffHz_ { 1000.0f };
    float resonance_ { 0.707f };
    int numStages_ { 1 };

    float g_ { 0.0f };
    float twoR_ { 1.414f };
    float denominator_ { 1.0f };

    Stage stages_[2];
};
} // namespace dsp
