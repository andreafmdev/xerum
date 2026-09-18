#include "dsp/StateVariableFilter.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
} // namespace

void StateVariableFilter::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateCoefficients();
    reset();
}

void StateVariableFilter::reset() noexcept
{
    for (auto& stage : stages_)
        stage = {};
}

void StateVariableFilter::setCutoffHz (float hz) noexcept
{
    cutoffHz_ = hz;
    updateCoefficients();
}

void StateVariableFilter::setResonance (float q) noexcept
{
    resonance_ = std::max (kButterworthQ, q);
    updateCoefficients();
}

void StateVariableFilter::setNumStages (int stages) noexcept
{
    const auto clamped = std::clamp (stages, 1, 2);

    // Il secondo stadio, mentre e' spento, non viene mai processato: puo' restare a covare
    // lo stato lasciato dall'ultima volta che era attivo. Se non lo si azzera qui, riaccenderlo
    // (12 -> 24 dB a metà nota) reinietta quello stato vecchio nel segnale, udibile come un clic.
    if (clamped > numStages_)
        for (int i = numStages_; i < clamped; ++i)
            stages_[i] = {};

    numStages_ = clamped;
}

void StateVariableFilter::updateCoefficients() noexcept
{
    // Oltre 0.49 · sr la prewarp manda tan() all'infinito e il filtro diverge.
    const auto limit = (float) (sampleRate_ * 0.49);
    const auto cutoff = std::clamp (cutoffHz_, 10.0f, limit);

    g_ = std::tan (kPi * cutoff / (float) sampleRate_);

    const auto denominatorFor = [this] (float twoR) noexcept
    {
        return 1.0f + twoR * g_ + g_ * g_;
    };

    butterworth_.twoR = 1.0f / kButterworthQ;
    butterworth_.denominator = denominatorFor (butterworth_.twoR);

    resonant_.twoR = 1.0f / resonance_;
    resonant_.denominator = denominatorFor (resonant_.twoR);

    // Compensazione: il picco di uno stadio risonante vale ~Q. Attenuando l'ingresso di
    // sqrt(Qbutter/Q) il picco diventa ~sqrt(Q·Qbutter) — a Q 12, +9 dB invece di +22 dB —
    // e la banda passante scende dello stesso fattore. std::sqrt gira solo qui, una volta
    // per blocco, mai per campione.
    inputGain_ = std::sqrt (kButterworthQ / resonance_);
}

float StateVariableFilter::processStage (Stage& stage, const Coefficients& c, float input) const noexcept
{
    const auto highPass = (input - (c.twoR + g_) * stage.s1 - stage.s2) / c.denominator;
    const auto bandPass = g_ * highPass + stage.s1;
    stage.s1 = g_ * highPass + bandPass;
    const auto lowPass = g_ * bandPass + stage.s2;
    stage.s2 = g_ * bandPass + lowPass;

    switch (type_)
    {
        case Type::highPass: return highPass;
        case Type::bandPass: return bandPass;
        case Type::lowPass:  break;
    }

    return lowPass;
}

float StateVariableFilter::processSample (float input) noexcept
{
    // La risonanza sta sull'ultimo stadio, il primo (se c'è) resta Butterworth: vedi il
    // commento di classe. L'attenuazione d'ingresso si applica una volta sola, all'inizio
    // della catena, non per stadio.
    if (numStages_ > 1)
        return processStage (stages_[1], resonant_, processStage (stages_[0], butterworth_, input * inputGain_));

    return processStage (stages_[0], resonant_, input * inputGain_);
}
} // namespace dsp
