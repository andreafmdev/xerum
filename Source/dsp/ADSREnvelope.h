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
 *
 * Lo stesso vale per il sustain: se il livello effettivo (`peak * sus`) finisce
 * sotto -80 dB — il caso tipico è `sus = 0`, cioè un patch percussivo — la nota
 * è finita e l'inviluppo si spegne da solo, senza aspettare un note-off che
 * altrimenti terrebbe la voce occupata a rendere silenzio.
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

    /** Il livello corrente 0..1, senza avanzare. Serve al mod matrix: la sorgente `env` e'
        questo inviluppo riusato come modulatore, non un secondo inviluppo dedicato (vedi
        docs/superpowers/specs/2026-09-19-modulation-design.md). */
    float getLevel() const noexcept { return level_; }
    float getNextSample() noexcept;

private:
    enum class Stage { idle, attack, decay, sustain, release };

    void updateCoefficients() noexcept;

    /** Entra in sustain, oppure si spegne se il livello di sustain e' sotto la soglia di silenzio. */
    void enterSustain() noexcept;

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
