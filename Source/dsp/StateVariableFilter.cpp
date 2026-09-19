#include "dsp/StateVariableFilter.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

/**
 * Esponente della compensazione d'ingresso, scelto sulle misure (seno, RMS a regime,
 * 44.1/48/96 kHz, cutoff da 100 Hz a 12 kHz, 12 e 24 dB).
 *
 * La banda passante di un passa-basso non dipende da Q, quindi *qualunque* attenuazione
 * d'ingresso la fa scendere: il costo in banda passante e' esattamente 20·p·log10(Q/Qbutter),
 * cioe' 30.6·p dB all'estremo della corsa (Q 24). Il budget e' 4 dB, e 1/8 e' il piu' grande
 * esponente che ci sta dentro: misura -3.51 dB nel caso peggiore (contro -4.78 a 1/6, che lo
 * sfonda, e -0.64 a 1/32, che era il valore di prima).
 *
 * Perche' spingere fin li' invece di restare a 1/32: la stessa attenuazione toglie gli stessi
 * decibel al picco risonante, e il picco risonante e' la sorgente di livello piu' violenta
 * dello strumento. Passare da 1/32 a 1/8 costa 2.9 dB di picco a Q 24 (da +29.7 a +26.8 sopra
 * il Butterworth) e 2.2 dB a Q 12, che e' il tetto della corsa di `res`: sono i decibel che
 * pagano, in parte, l'alzata di kVoiceHeadroomGain. A Q 4 il conto in banda passante e' -1.57 dB.
 *
 * Il picco resta quindi ~Q^(7/8): a Q 24 vale 15.4 invece di 24. Il vecchio sqrt(Qbutter/Q)
 * (p = 1/2) costava -14.7 dB di banda passante a Q 24: alzare la risonanza rendeva lo strumento
 * piu' piano, che e' il difetto opposto e molto peggiore.
 */
constexpr float kResonanceCompensation = 1.0f / 8.0f;
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

    // Compensazione: il picco di uno stadio risonante vale ~Q, e l'attenuazione d'ingresso e'
    // l'unica leva che abbiamo per tenerlo a bada. Ma agisce su tutto il segnale, banda passante
    // compresa, quindi va dosata: l'esponente e' 1/8 (vedi kResonanceCompensation), il massimo
    // che sta dentro il budget di 4 dB in banda passante a Q 24. std::pow gira solo qui, una
    // volta per cambio di parametro, mai per campione: nel loop audio non entra nessuna
    // chiamata a libm.
    inputGain_ = std::pow (kButterworthQ / resonance_, kResonanceCompensation);
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
