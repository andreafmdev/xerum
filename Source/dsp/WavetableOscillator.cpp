#include "dsp/WavetableOscillator.h"

#include <cmath>

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

float levelForFrequency (float frequencyHz, double sampleRate, int frameSize) noexcept
{
    // Confronti scritti in positivo apposta: un NaN li fa fallire tutti e finisce qui,
    // invece di propagarsi fino al cast a int in updateLevel() — che su un NaN sarebbe
    // undefined behaviour. Il vecchio ciclo while era immune per caso, questo no.
    if (! (frequencyHz > 0.0f) || ! (sampleRate > 0.0) || frameSize <= 1)
        return (float) MipTable::kMaxLevel;

    // Quante armoniche stanno sotto Nyquist a questa frequenza.
    const double maxHarmonics = sampleRate / (2.0 * (double) frequencyHz);

    // Il livello k conserva (frameSize >> k) / 2 armoniche, cioè (frameSize / 2) / 2^k:
    // ne bastano `t` per scendere da frameSize / 2 a maxHarmonics, e `ceil(t)` è il primo
    // livello intero che ci arriva. Si restituisce t + 1 e non ceil(t) perché la parte
    // frazionaria deve portare il peso del crossfade (vedi l'header): floor(t + 1) è
    // proprio ceil(t), e frac(t + 1) va da 0 a 1 mentre la frequenza attraversa l'ottava
    // in cui quel livello resta il primo sicuro. Al confine successivo il livello sicuro
    // diventa quello dopo e il peso riparte da 0 — cioè esattamente dal suono su cui si
    // era arrivati un istante prima: nessun salto.
    const double t = std::log2 ((double) (frameSize / 2) / maxHarmonics);

    return (float) juce::jlimit (0.0, (double) MipTable::kMaxLevel, t + 1.0);
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
    if (table_ == nullptr)
    {
        levelLo_ = levelHi_ = 0;
        levelMix_ = 0.0f;
        return;
    }

    const auto level = levelForFrequency (frequencyHz_, sampleRate_, table_->getFrameSize());

    // levelForFrequency è già limitata a [0, kMaxLevel], quindi il troncamento sta in
    // range; jlimit è solo un paio di confronti e mette al riparo da un NaN che arrivasse
    // da una sample rate assurda.
    levelLo_ = juce::jlimit (0, MipTable::kMaxLevel, (int) level);
    levelHi_ = juce::jmin (levelLo_ + 1, MipTable::kMaxLevel);
    levelMix_ = juce::jlimit (0.0f, 1.0f, level - (float) levelLo_);
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

    // Morph fra i due frame, fatto una volta per livello; poi crossfade fra i due livelli.
    // L'ordine non conta (è un'interpolazione bilineare), conta che siano 4 letture e due
    // sole moltiplicazioni in più rispetto a prima.
    const auto atLevel = [this, size, index, fraction] (int level)
    {
        const float lo = sampleAt (table_->samples (frameLo_, level), size, index, fraction);
        const float hi = sampleAt (table_->samples (frameHi_, level), size, index, fraction);
        return lo + (hi - lo) * frameMix_;
    };

    const float bright = atLevel (levelLo_);
    const float dark = levelHi_ != levelLo_ ? atLevel (levelHi_) : bright;

    // |phaseIncrement_| <= 1 (garantito in setFrequencyHz): un solo passo in ciascuna
    // direzione riporta sempre phase_ in [0, 1), che è ciò che rende sicuro il cast
    // sopra a ogni chiamata successiva.
    phase_ += phaseIncrement_;
    if (phase_ >= 1.0)
        phase_ -= 1.0;
    else if (phase_ < 0.0)
        phase_ += 1.0;

    return bright + (dark - bright) * levelMix_;
}
} // namespace dsp
