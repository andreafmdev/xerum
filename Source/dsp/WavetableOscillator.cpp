#include "dsp/WavetableOscillator.h"

namespace dsp
{
namespace
{
/** Interpolazione lineare dentro un frame, con wrap sull'ultimo campione. */
float sampleAt (const float* frame, int size, int index, float fraction) noexcept
{
    const float a = frame[index];
    const float b = frame[index + 1 < size ? index + 1 : 0];
    return a + (b - a) * fraction;
}
} // namespace

int levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept
{
    if (frequencyHz <= 0.0f || sampleRate <= 0.0)
        return MipTable::kMaxLevel;

    // Quante armoniche stanno sotto Nyquist a questa frequenza.
    const double maxHarmonics = sampleRate / (2.0 * (double) frequencyHz);

    int level = 0;
    while (level < MipTable::kMaxLevel && (double) ((frameSize >> level) / 2) > maxHarmonics)
        ++level;

    return level;
}

void WavetableOscillator::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
    updateLevel();
}

void WavetableOscillator::reset() noexcept
{
    phase_ = 0.0;
}

void WavetableOscillator::setTable (const MipTable* table) noexcept
{
    table_ = table;
    frameLo_ = frameHi_ = 0;
    frameMix_ = 0.0f;
    updateLevel();
}

void WavetableOscillator::setFrequencyHz (float hz) noexcept
{
    frequencyHz_ = hz;

    // L'avvolgimento per campione in getSample() somma o sottrae 1 una sola volta:
    // basta finché |phaseIncrement_| < 1. Con un incremento più grande (frequenza
    // sopra la sample rate, positiva o negativa) un solo passo non riporterebbe la
    // fase in [0, 1) e il cast a intero su una fase fuori range sarebbe undefined
    // behaviour. juce::jlimit è solo un confronto: niente libm.
    const double increment = (double) hz / sampleRate_;
    phaseIncrement_ = juce::jlimit (-1.0, 1.0, increment);
    updateLevel();
}

void WavetableOscillator::setFramePosition (float normalised) noexcept
{
    if (table_ == nullptr)
        return;

    const int lastFrame = table_->getNumFrames() - 1;
    const float position = juce::jlimit (0.0f, 1.0f, normalised) * (float) lastFrame;

    frameLo_ = (int) position;
    frameHi_ = frameLo_ < lastFrame ? frameLo_ + 1 : lastFrame;
    frameMix_ = position - (float) frameLo_;
}

void WavetableOscillator::updateLevel() noexcept
{
    level_ = table_ == nullptr ? 0 : levelForFrequency (frequencyHz_, sampleRate_, table_->getFrameSize());
}

float WavetableOscillator::getSample() noexcept
{
    if (table_ == nullptr)
        return 0.0f;

    // Tutti i livelli sono lunghi frameSize: cambia il contenuto (armoniche), non la
    // lunghezza del buffer, quindi l'interpolazione lineare lavora sempre su una
    // tavola largamente sovracampionata.
    const int size = table_->getFrameSize();
    const double position = phase_ * (double) size;
    const int index = juce::jlimit (0, size - 1, (int) position);
    const auto fraction = (float) (position - (double) index);

    const float lo = sampleAt (table_->samples (frameLo_, level_), size, index, fraction);
    const float hi = sampleAt (table_->samples (frameHi_, level_), size, index, fraction);

    // |phaseIncrement_| <= 1 (garantito in setFrequencyHz): un solo passo in ciascuna
    // direzione riporta sempre phase_ in [0, 1), che è ciò che rende sicuro il cast
    // sopra a ogni chiamata successiva.
    phase_ += phaseIncrement_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;
    else if (phase_ < 0.0)
        phase_ += 1.0;

    return lo + (hi - lo) * frameMix_;
}
} // namespace dsp
