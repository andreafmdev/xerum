#pragma once

namespace dsp
{
/**
 * Inviluppo esponenziale stile analogico: ogni stadio è un polo singolo che
 * insegue un target. I coefficienti si ricalcolano solo quando cambia un
 * parametro o a note-on, mai per campione — nel loop audio non entra `exp`.
 *
 * I tempi sono nominali e vengono rispettati: l'attacco arriva a 0.99 in `att`,
 * il decay copre il 99 % della distanza in `dec`, il release scende sotto
 * -80 dB in `rel` e lì l'inviluppo si dichiara spento.
 */
class ADSREnvelope
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setAttackSeconds (float seconds) noexcept;
    void setDecaySeconds (float seconds) noexcept;
    void setSustainLevel (float level) noexcept;
    void setReleaseSeconds (float seconds) noexcept;

    /** `peak` è il livello massimo di questa nota (velocity già applicata). */
    void noteOn (float peak) noexcept;
    void noteOff() noexcept;

    bool isActive() const noexcept { return stage_ != Stage::idle; }
    float getNextSample() noexcept;

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void updateCoefficients() noexcept;

    double sampleRate_ { 44100.0 };
    Stage stage_ { Stage::idle };

    float attackSeconds_ { 0.01f };
    float decaySeconds_ { 0.1f };
    float sustain_ { 1.0f };
    float releaseSeconds_ { 0.1f };

    float attackCoeff_ { 1.0f };
    float decayCoeff_ { 1.0f };
    float releaseCoeff_ { 1.0f };

    float peak_ { 1.0f };
    float level_ { 0.0f };
    float decayDistance_ { 0.0f };
};
} // namespace dsp
