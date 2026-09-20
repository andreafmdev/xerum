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

    /** Forma dei segmenti, -1..1: 0 e' l'esponenziale di sempre, +1 rette nel tempo nominale,
        -1 ginocchio anticipato. Fuori range viene limitato. */
    void setCurve (float curve) noexcept;

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

    /**
     * La forma dei segmenti (setCurve), gia' tradotta in tre numeri che il loop legge:
     *
     *  - `overshoot_` (curve > 0): il polo insegue un bersaglio oltre il vero — `peak·(1+o)`
     *    in attacco, `sustain - o·distanza` in decay, `-o·livello di partenza` in release — e
     *    il livello viene fermato al vero. Piu' e' lontano il bersaglio, piu' il tratto percorso
     *    e' dritto: a +1 (o = 8) e' una retta al 3 %. La costante di tempo si ricalcola perche'
     *    il segmento finisca comunque nel tempo nominale.
     *  - `attackDone_` / `decayDone_` (curve < 0): l'esponenziale si stringe (costante di tempo
     *    per 1..1.5) e la soglia di "segmento finito" scende con lei, cosi' il ginocchio arriva
     *    prima ma la durata dichiarata resta.
     *
     * A curve 0 i tre valgono 0, 0.99 e 0.01 e le formule del loop si riducono bit per bit a
     * quelle di prima: `peak·(1+0)`, `target - 0·d`, `level + c·(-0·s - level)`.
     */
    float curve_ { 0.0f };
    float overshoot_ { 0.0f };
    float attackDone_ { 0.99f };
    float decayDone_ { 0.01f };
    float releaseStart_ { 1.0f };

    float peak_ { 1.0f };
    float level_ { 0.0f };
    float decayDistance_ { 0.0f };
};
} // namespace dsp
