#include "dsp/ADSREnvelope.h"
#include "dsp/Constants.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace dsp
{
namespace
{
/** Soglia di spegnimento: dsp::kSilenceFloor, -80 dB. Sotto, la coda è inudibile e la voce va liberata. */
constexpr float kSilence = kSilenceFloor;

/** Uguaglianza bit a bit: un parametro che arriva identico non deve rifare gli `exp`. */
inline bool sameFloat (float a, float b) noexcept
{
    return std::bit_cast<std::uint32_t> (a) == std::bit_cast<std::uint32_t> (b);
}

/** Quante costanti di tempo servono per considerare finito ogni stadio. */
constexpr float kAttackTau = 4.605170f;   // 1 - e^-4.605170 ≈ 0.99
constexpr float kDecayTau = 4.605170f;    // 99 % della distanza
constexpr float kReleaseTau = 9.210340f; // e^-9.210340 ≈ 1e-4, cioè -80 dB

/** Quanto oltre il bersaglio punta il polo a curve +1: con 8 il tratto percorso e' una retta al 3 %. */
constexpr float kMaxOvershoot = 8.0f;

float coefficientFor (float seconds, float constants, double sampleRate) noexcept
{
    const auto samples = std::max (1.0, (double) seconds * sampleRate);
    return 1.0f - (float) std::exp (-(double) constants / samples);
}

/** Come coefficientFor, ma con expm1 in doppia precisione: con il bersaglio sorpassato la
    costante di tempo e' piccola (0.12 su un segmento) e `1 - exp()` in float tiene tre cifre
    del coefficiente, che sull'arco di un attacco diventano un errore dell'1 %. Il ramo a curve
    0 usa ancora coefficientFor: e' quello che deve restare bit per bit com'era. */
float preciseCoefficientFor (float seconds, float constants, double sampleRate) noexcept
{
    const auto samples = std::max (1.0, (double) seconds * sampleRate);
    return (float) -std::expm1 (-(double) constants / samples);
}

/** Il resto lasciato dal segmento a fine corsa, leggermente sotto la soglia di "finito" (0.99 /
    0.01 / -80 dB) perche' la soglia scatti entro il tempo nominale e non un campione dopo. */
constexpr float kRemainderMargin = 0.99f;
} // namespace

void ADSREnvelope::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients();
    reset();
}

void ADSREnvelope::reset() noexcept
{
    stage_ = Stage::idle;
    level_ = 0.0f;
    decayDistance_ = 0.0f;
}

void ADSREnvelope::setAttackSeconds (float seconds) noexcept
{
    // I setter arrivano una volta per voce per blocco, quasi sempre con lo stesso valore: senza
    // questo ritorno ogni voce rifaceva tre std::exp per inviluppo a knob fermi.
    const auto clamped = std::max (0.0f, seconds);

    if (sameFloat (clamped, attackSeconds_))
        return;

    attackSeconds_ = clamped;
    updateCoefficients();
}

void ADSREnvelope::setDecaySeconds (float seconds) noexcept
{
    // I setter arrivano una volta per voce per blocco, quasi sempre con lo stesso valore: senza
    // questo ritorno ogni voce rifaceva tre std::exp per inviluppo a knob fermi.
    const auto clamped = std::max (0.0f, seconds);

    if (sameFloat (clamped, decaySeconds_))
        return;

    decaySeconds_ = clamped;
    updateCoefficients();
}

void ADSREnvelope::setSustainLevel (float level) noexcept
{
    sustain_ = std::clamp (level, 0.0f, 1.0f);
}

void ADSREnvelope::setReleaseSeconds (float seconds) noexcept
{
    // I setter arrivano una volta per voce per blocco, quasi sempre con lo stesso valore: senza
    // questo ritorno ogni voce rifaceva tre std::exp per inviluppo a knob fermi.
    const auto clamped = std::max (0.0f, seconds);

    if (sameFloat (clamped, releaseSeconds_))
        return;

    releaseSeconds_ = clamped;
    updateCoefficients();
}

void ADSREnvelope::setCurve (float curve) noexcept
{
    const auto clamped = std::clamp (curve, -1.0f, 1.0f);

    if (sameFloat (clamped, curve_))
        return;

    curve_ = clamped;
    updateCoefficients();
}

void ADSREnvelope::updateCoefficients() noexcept
{
    if (sameFloat (curve_, 0.0f))
    {
        // Il ramo di sempre, con le sue costanti: nessun log() che possa spostare un bit.
        overshoot_ = 0.0f;
        attackDone_ = 0.99f;
        decayDone_ = 0.01f;
        attackCoeff_ = coefficientFor (attackSeconds_, kAttackTau, sampleRate_);
        decayCoeff_ = coefficientFor (decaySeconds_, kDecayTau, sampleRate_);
        releaseCoeff_ = coefficientFor (releaseSeconds_, kReleaseTau, sampleRate_);
        return;
    }

    if (curve_ > 0.0f)
    {
        // Bersaglio sorpassato di `o` volte la distanza: il segmento finisce (soglia 0.99 / 0.01 /
        // -80 dB) quando l'esponenziale ha percorso 1/(1+o) della corsa, cioe' il tratto quasi
        // dritto in cima. tau = ln((1+o) / (o + resto)) e' la costante che ci arriva nel tempo
        // nominale: a o = 0 torna ln(100) e ln(1e4), le costanti di sempre.
        const auto o = kMaxOvershoot * curve_;
        overshoot_ = o;
        attackDone_ = 0.99f;
        decayDone_ = 0.01f;
        attackCoeff_ = preciseCoefficientFor (attackSeconds_, std::log ((1.0f + o) / (o + 0.01f * kRemainderMargin)), sampleRate_);
        decayCoeff_ = preciseCoefficientFor (decaySeconds_, std::log ((1.0f + o) / (o + 0.01f * kRemainderMargin)), sampleRate_);
        releaseCoeff_ = preciseCoefficientFor (releaseSeconds_, std::log ((1.0f + o) / (o + kSilence * kRemainderMargin)), sampleRate_);
        return;
    }

    // Esponenziale piu' stretto: costante di tempo scalata di 1..1.5, e la soglia di fine
    // segmento scende con lei (0.01^s), cosi' il picco arriva ancora nel tempo nominale.
    const auto sharpen = 1.0f + 0.5f * (-curve_);
    overshoot_ = 0.0f;
    attackDone_ = 1.0f - std::pow (0.01f, sharpen) / kRemainderMargin;
    decayDone_ = std::pow (0.01f, sharpen) / kRemainderMargin;
    attackCoeff_ = preciseCoefficientFor (attackSeconds_, kAttackTau * sharpen, sampleRate_);
    decayCoeff_ = preciseCoefficientFor (decaySeconds_, kDecayTau * sharpen, sampleRate_);
    releaseCoeff_ = preciseCoefficientFor (releaseSeconds_, kReleaseTau * sharpen, sampleRate_);
}

void ADSREnvelope::enterSustain() noexcept
{
    level_ = peak_ * sustain_;

    // Un sustain sotto la soglia di udibilita' non e' un livello da tenere: e' la fine della
    // nota. Senza questo, Stage::sustain non transita mai a idle (solo il release lo fa) e con
    // `sustain = 0` la voce resta occupata a rendere silenzio finche' VoiceManager non gliela
    // ruba con kill() — cioe' un clic a ogni nota di un patch percussivo. Il confronto e' sul
    // livello *effettivo* (peak * sustain), quindi un sustain piccolo ma legittimo (0.01, -40 dB)
    // continua a sostenere fino al note-off.
    if (level_ < kSilence)
    {
        level_ = 0.0f;
        stage_ = Stage::idle;
        return;
    }

    stage_ = Stage::sustain;
}

void ADSREnvelope::noteOn (float peak) noexcept
{
    peak_ = std::clamp (peak, 0.0f, 1.0f);
    stage_ = Stage::attack;
}

void ADSREnvelope::noteOff() noexcept
{
    if (stage_ != Stage::idle)
    {
        releaseStart_ = level_;
        stage_ = Stage::release;
    }
}

float ADSREnvelope::getNextSample() noexcept
{
    switch (stage_)
    {
        case Stage::idle:
            return 0.0f;

        case Stage::attack:
            level_ += attackCoeff_ * (peak_ * (1.0f + overshoot_) - level_);
            if (level_ >= peak_ * attackDone_)
            {
                level_ = peak_;
                const auto target = peak_ * sustain_;
                decayDistance_ = std::abs (peak_ - target);
                if (decayDistance_ <= 0.0f)
                {
                    enterSustain();
                }
                else
                {
                    stage_ = Stage::decay;
                }
            }
            break;

        case Stage::decay:
        {
            const auto target = peak_ * sustain_;
            level_ += decayCoeff_ * ((target - overshoot_ * decayDistance_) - level_);
            if (std::abs (level_ - target) <= decayDone_ * decayDistance_ || level_ < target)
                enterSustain();

            break;
        }

        case Stage::sustain:
            // Rivalutato a ogni campione: cosi' anche portare a zero la manopola `sus` mentre
            // la nota suona libera la voce, invece di lasciarla appesa a un livello nullo.
            enterSustain();
            break;

        case Stage::release:
            level_ += releaseCoeff_ * (-(overshoot_ * releaseStart_) - level_);
            if (level_ < kSilence)
            {
                level_ = 0.0f;
                stage_ = Stage::idle;
            }
            break;
    }

    return level_;
}
} // namespace dsp
