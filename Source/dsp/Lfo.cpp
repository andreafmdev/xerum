#include "dsp/Lfo.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr float kTwoPi = 6.283185307179586f;

/** Quanti quarti dura un ciclo, per ognuna delle sei divisioni di Tabs.tsx. */
constexpr double kBeatsPerCycle[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
} // namespace

float lfoShapeValue (Lfo::Shape shape, float phase01) noexcept
{
    const auto ph = phase01 - std::floor (phase01); // wrap anche per fasi negative

    switch (shape)
    {
        case Lfo::Shape::sine:       return std::sin (ph * kTwoPi);
        case Lfo::Shape::triangle:   return 1.0f - 4.0f * std::abs (ph - 0.5f);
        case Lfo::Shape::saw:        return 1.0f - 2.0f * ph;
        case Lfo::Shape::square:     return ph < 0.5f ? 1.0f : -1.0f;
        case Lfo::Shape::sampleHold: break;
    }

    // Non e' un vero sample & hold: e' il pattern pseudo-casuale deterministico che la UI
    // disegna (mod.ts). Riprodurlo identico e' cio' che tiene il puntino del tab LFO allineato
    // con quello che si sente.
    return std::sin (std::floor (ph * 8.0f) * 7.3f);
}

float syncedRateHz (float raw, double bpm) noexcept
{
    const auto clamped = std::clamp (raw, 0.0f, 1.0f);
    const auto index = std::min (5, (int) std::floor ((double) clamped * 6.0));
    const auto tempo = bpm > 0.0 ? bpm : 120.0;

    return (float) (tempo / 60.0 / kBeatsPerCycle[index]);
}

void Lfo::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    retrigger (0.0f);
}

void Lfo::setFrequencyHz (float hz) noexcept
{
    // Il passo per campione non puo' superare 1: oltre, l'avvolgimento di advance() non
    // riporterebbe la fase in [0, 1) e il livello sarebbe indefinito. Limitarlo qui costa un
    // confronto e rende impossibile ogni sorpresa a valle.
    phaseIncrement_ = std::clamp ((double) hz / sampleRate_, -1.0, 1.0);
}

void Lfo::setFadeSeconds (float seconds) noexcept
{
    fadeSamples_ = std::max (0.0, (double) seconds * sampleRate_);
}

void Lfo::retrigger (float startPhase01) noexcept
{
    phase_ = (double) (startPhase01 - std::floor (startPhase01));
    fadeProgress_ = fadeSamples_ > 0.0 ? 0.0 : 1.0;
    updateLevel();
}

float Lfo::advance (int numSamples) noexcept
{
    if (numSamples <= 0)
        return level_;

    phase_ += phaseIncrement_ * (double) numSamples;
    phase_ -= std::floor (phase_);

    if (fadeSamples_ > 0.0 && fadeProgress_ < 1.0)
        fadeProgress_ = std::min (1.0, fadeProgress_ + (double) numSamples / fadeSamples_);

    updateLevel();
    return level_;
}

void Lfo::updateLevel() noexcept
{
    const auto fade = fadeSamples_ > 0.0 ? (float) fadeProgress_ : 1.0f;
    level_ = lfoShapeValue (shape_, (float) phase_) * fade;
}
} // namespace dsp
