/*
  Adattamento di `modules/gin_dsp/dsp/gin_platereverb.h` di FigBug/Gin — MIT, (c) 2023 Mike
  Jarmy. L'intestazione completa della licenza sta in PlateReverb.h, insieme all'elenco di cio'
  che e' stato cambiato e perche'.
*/

#include "dsp/PlateReverb.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
/**
 * La sample rate a cui Dattorro da' i suoi ritardi. Tutte le lunghezze sono in campioni **a
 * quella frequenza** e vanno scalate per `fs / kDattorroSampleRate`: e' cio' che rende il
 * riverbero lo stesso oggetto a 44.1, 48 o 96 kHz invece che una piastra piu' piccola.
 */
constexpr double kDattorroSampleRate = 29761.0;

/** Il `kMaxSize` di Gin: a sizeRatio 1 i ritardi del tank valgono il doppio di quelli del paper. */
constexpr float kMaxSize = 2.0f;

/** ln(1e-3): la caduta di 60 dB, in neper. */
constexpr float kMinus60dB = 6.90775528f;
} // namespace

// ---------------------------------------------------------------------------------------------
// DelayLine
// ---------------------------------------------------------------------------------------------

void PlateReverb::DelayLine::prepare (int size)
{
    size_ = juce::jmax (1, size);

    // `+ 2`, e non `size` come nell'originale: tap() legge l'indice intero, il campione prima
    // per l'interpolazione lineare, e parte da writeIndex_ - 1. Con un buffer lungo esattamente
    // `size` — cioe' ogni volta che `size` capita di essere una potenza di due — le due letture
    // piu' vecchie avvolgerebbero sul campione appena scritto.
    const auto bufferSize = juce::nextPowerOfTwo (size_ + 2);

    buffer_.assign ((size_t) bufferSize, 0.0f);
    mask_ = bufferSize - 1;
    writeIndex_ = 0;
}

void PlateReverb::DelayLine::reset() noexcept
{
    std::fill (buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

// ---------------------------------------------------------------------------------------------
// DelayAllpass, OnePole
// ---------------------------------------------------------------------------------------------

void PlateReverb::DelayAllpass::prepare (int size, float gain)
{
    line_.prepare (size);
    gain_ = gain;
}

void PlateReverb::OnePole::setCutoff (float hz, double sampleRate) noexcept
{
    b_ = std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sampleRate);
    a_ = 1.0f - b_;
}

// ---------------------------------------------------------------------------------------------
// Tank
// ---------------------------------------------------------------------------------------------

void PlateReverb::Tank::prepare (double sampleRate, float apf1Base, float apf1Gain, float delay1Base,
                                 float apf2Base, float apf2Gain, float delay2Base, float maxMod)
{
    apf1Size = apf1Base;
    apf2Size = apf2Base;
    del1Size = delay1Base;
    del2Size = delay2Base;
    maxModDepth = maxMod;

    // L'allpass modulato va dimensionato sul ritardo massimo **piu' l'escursione dell'LFO**: e'
    // l'unica linea il cui ritardo richiesto puo' superare la sua lunghezza nominale.
    apf1.prepare ((int) std::ceil (apf1Size + maxModDepth) + 1, apf1Gain);
    apf2.prepare ((int) std::ceil (apf2Size) + 1, apf2Gain);
    del1.prepare ((int) std::ceil (del1Size) + 1);
    del2.prepare ((int) std::ceil (del2Size) + 1);

    damping.setCutoff (kDampingMaxHz, sampleRate);
    reset();
}

void PlateReverb::Tank::reset() noexcept
{
    apf1.reset();
    apf2.reset();
    del1.reset();
    del2.reset();
    damping.reset();
    out = 0.0f;
}

void PlateReverb::Tank::process (float input, float sizeRatio, float lfo, float decay) noexcept
{
    // APF1, con le parole di Dattorro: "controls density of tail". E' il solo punto modulato
    // della catena, ed e' cio' che tiene la coda lunga fuori dal timbro metallico dei modi
    // fissi — l'assenza che si sente in juce::dsp::Reverb.
    auto value = apf1.process (input, (apf1Size + lfo * maxModDepth) * sizeRatio);
    value = del1.tapAndPush (del1Size * sizeRatio, value);

    value = damping.process (value);
    value *= decay;

    // APF2: "decorrelates tank signals".
    value = apf2.process (value, apf2Size * sizeRatio);
    value = del2.tapAndPush (del2Size * sizeRatio, value);

    out = value;
}

// ---------------------------------------------------------------------------------------------
// PlateReverb
// ---------------------------------------------------------------------------------------------

void PlateReverb::prepare (double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;

    const auto r = (float) (sampleRate_ / kDattorroSampleRate);

    predelayLine_.prepare ((int) std::ceil (sampleRate_ * (double) kMaxPredelaySeconds) + 1);
    inputLowpass_.setCutoff (kInputLowpassHz, sampleRate_);

    // I quattro diffusori: lunghezze e guadagni del paper, non scalati da `size`. Diffondono
    // l'ingresso prima che entri nell'anello, quindi la loro lunghezza e' una proprieta'
    // dell'attacco, non della stanza.
    diffusers_[0].prepare ((int) std::ceil (142.0f * r) + 1, 0.75f);
    diffusers_[1].prepare ((int) std::ceil (107.0f * r) + 1, 0.75f);
    diffusers_[2].prepare ((int) std::ceil (379.0f * r) + 1, 0.625f);
    diffusers_[3].prepare ((int) std::ceil (277.0f * r) + 1, 0.625f);

    diffuserDelays_ = { 142.0f * r, 107.0f * r, 379.0f * r, 277.0f * r };

    const auto maxModDepth = 8.0f * kMaxSize * r;

    leftTank_.prepare (sampleRate_, kMaxSize * 672.0f * r, -0.7f, kMaxSize * 4453.0f * r,
                       kMaxSize * 1800.0f * r, 0.5f, kMaxSize * 3720.0f * r, maxModDepth);
    rightTank_.prepare (sampleRate_, kMaxSize * 908.0f * r, -0.7f, kMaxSize * 4217.0f * r,
                        kMaxSize * 2656.0f * r, 0.5f, kMaxSize * 3163.0f * r, maxModDepth);

    // I sette tap per canale, presi dalle linee del tank **opposto** (i primi quattro) e dal
    // proprio (gli ultimi tre), con i segni alternati del paper. Scalano con `sizeRatio` ma non
    // con kMaxSize: e' la scelta di Gin, tenuta com'e' perche' i tap servono solo a pescare
    // punti decorrelati e il paper non fissa quali.
    leftTaps_ = { 266.0f * r, 2974.0f * r, 1913.0f * r, 1996.0f * r, 1990.0f * r, 187.0f * r, 1066.0f * r };
    rightTaps_ = { 353.0f * r, 3627.0f * r, 1228.0f * r, 2673.0f * r, 2111.0f * r, 335.0f * r, 121.0f * r };

    sizeCoeff_ = halfLifeCoefficient (kSizeSmoothingHalfLifeSeconds, sampleRate_);
    smoothingCoeff_ = halfLifeCoefficient (kSmoothingHalfLifeSeconds, sampleRate_);

    reset();
}

void PlateReverb::reset() noexcept
{
    predelayLine_.reset();
    inputLowpass_.reset();

    for (auto& d : diffusers_)
        d.reset();

    leftTank_.reset();
    rightTank_.reset();

    lfoPhaseLeft_ = 0.0f;
    lfoPhaseRight_ = 0.0f;
    lfoLeft_ = 0.0f;
    lfoRight_ = 0.0f;

    // I valori smussati ripartono **sul** bersaglio alla prima fetta dopo un reset, non dal
    // punto in cui li aveva lasciati la volta scorsa: altrimenti riaccendere l'effetto
    // comincerebbe con una scivolata di size che non ha niente a che vedere con i parametri.
    // Stessa ragione, e stesso meccanismo, del delayPrimed_ del chorus.
    primed_ = false;
}

void PlateReverb::setParameters (float size01, float decay01, float damp01, float predelaySeconds) noexcept
{
    sizeRatioTarget_ = juce::jmap (juce::jlimit (0.0f, 1.0f, size01), kMinSizeRatio, kMaxSizeRatio);
    decayTarget_ = juce::jmap (juce::jlimit (0.0f, 1.0f, decay01), kMinDecay, kMaxDecay);
    predelayTarget_ = juce::jlimit (0.0f, kMaxPredelaySeconds, predelaySeconds) * (float) sampleRate_;

    // Il guadagno del secondo allpass sale con il decadimento: e' la riga di Gin, che viene dal
    // paper. E' un coefficiente, non un segnale: cambiarlo a gradino non produce un gradino
    // all'uscita, quindi non passa dal polo di smussamento come fanno gli altri tre.
    const auto apf2Gain = juce::jlimit (0.25f, 0.5f, decayTarget_ + 0.15f);
    leftTank_.apf2.setGain (apf2Gain);
    rightTank_.apf2.setGain (apf2Gain);

    // Corsa logaritmica, e un exp() solo quando il valore cambia davvero: setParameters gira
    // una volta per blocco, ma con un knob fermo anche quella volta non serve.
    const auto hz = kDampingMaxHz
                    * std::pow (kDampingMinHz / kDampingMaxHz, juce::jlimit (0.0f, 1.0f, damp01));

    if (! juce::approximatelyEqual (hz, dampingHz_))
    {
        dampingHz_ = hz;
        leftTank_.damping.setCutoff (hz, sampleRate_);
        rightTank_.damping.setCutoff (hz, sampleRate_);
    }
}

float PlateReverb::tailSeconds() const noexcept
{
    const auto decay = juce::jlimit (kMinDecay, kMaxDecay, decayTarget_);
    const auto loop = kFigureEightSeconds * sizeRatioTarget_;

    return kTailMargin * loop * kMinus60dB / (4.0f * -std::log (decay))
           + kLongestTapSeconds * sizeRatioTarget_ + predelayTarget_ / (float) sampleRate_;
}

float PlateReverb::tailSecondsAtExtremes() noexcept
{
    return kTailMargin * kFigureEightSeconds * kMaxSizeRatio * kMinus60dB
               / (4.0f * -std::log (kMaxDecay))
           + kLongestTapSeconds * kMaxSizeRatio + kMaxPredelaySeconds;
}

void PlateReverb::process (float* left, float* right, int numSamples) noexcept
{
    forEachSlice (numSamples, kModulationSliceSamples, [&] (int offset, int slice)
    {
        processSlice (left + offset, right != nullptr ? right + offset : nullptr, slice);
    });
}

void PlateReverb::processSlice (float* left, float* right, int numSamples) noexcept
{
    // I due soli std::sin della fetta. Le frequenze sono quelle di Gin, 1 Hz e 0.95: vicine ma
    // non uguali, cosi' i due tank non modulano all'unisono e la coda non batte.
    const auto targetLfoLeft = std::sin (juce::MathConstants<float>::twoPi * lfoPhaseLeft_);
    const auto targetLfoRight = std::sin (juce::MathConstants<float>::twoPi * lfoPhaseRight_);

    if (! primed_)
    {
        sizeRatio_ = sizeRatioTarget_;
        decay_ = decayTarget_;
        predelaySamples_ = predelayTarget_;
        lfoLeft_ = targetLfoLeft;
        lfoRight_ = targetLfoRight;
        primed_ = true;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        // I cinque poli. Tre sono i parametri (size, decay, predelay) e due sono l'LFO: e' il
        // secondo caveat del codice adottato, risolto nello stesso posto del primo. Senza,
        // l'LFO sarebbe un bersaglio a gradini ogni 32 campioni su un ritardo di allpass, cioe'
        // esattamente lo zipper che il polo del chorus esiste per togliere.
        sizeRatio_ += (sizeRatioTarget_ - sizeRatio_) * sizeCoeff_;
        decay_ += (decayTarget_ - decay_) * smoothingCoeff_;
        predelaySamples_ += (predelayTarget_ - predelaySamples_) * sizeCoeff_;
        lfoLeft_ += (targetLfoLeft - lfoLeft_) * smoothingCoeff_;
        lfoRight_ += (targetLfoRight - lfoRight_) * smoothingCoeff_;

        // "Synthetic stereo": il tank sente la somma mono e produce due uscite decorrelate dai
        // tap. La media e non la somma di Gin, cosi' il livello che entra nel riverbero non
        // dipende da quanti canali porta lo stesso segnale.
        auto sum = right != nullptr ? 0.5f * (left[n] + right[n]) : left[n];

        sum = predelayLine_.tapAndPush (predelaySamples_, sum);
        sum = inputLowpass_.process (sum);

        for (int i = 0; i < 4; ++i)
            sum = diffusers_[(size_t) i].process (sum, diffuserDelays_[(size_t) i]);

        // Le due diagonali della figura a otto: ciascun tank riceve la somma diffusa piu'
        // l'uscita dell'**altro** tank, attenuata. E' il quarto e ultimo punto in cui il
        // segnale incontra `decay` in un giro completo, da cui il decay^4 di tailSeconds().
        const auto leftIn = sum + rightTank_.out * decay_;
        const auto rightIn = sum + leftTank_.out * decay_;

        leftTank_.process (leftIn, sizeRatio_, lfoLeft_, decay_);
        rightTank_.process (rightIn, sizeRatio_, lfoRight_, decay_);

        const auto s = sizeRatio_;

        const auto wetLeft = rightTank_.del1.tap (leftTaps_[0] * s)
                             + rightTank_.del1.tap (leftTaps_[1] * s)
                             - rightTank_.apf2.tap (leftTaps_[2] * s)
                             + rightTank_.del2.tap (leftTaps_[3] * s)
                             - leftTank_.del1.tap (leftTaps_[4] * s)
                             - leftTank_.apf2.tap (leftTaps_[5] * s)
                             - leftTank_.del2.tap (leftTaps_[6] * s);

        const auto wetRight = leftTank_.del1.tap (rightTaps_[0] * s)
                              + leftTank_.del1.tap (rightTaps_[1] * s)
                              - leftTank_.apf2.tap (rightTaps_[2] * s)
                              + leftTank_.del2.tap (rightTaps_[3] * s)
                              - rightTank_.del1.tap (rightTaps_[4] * s)
                              - rightTank_.apf2.tap (rightTaps_[5] * s)
                              - rightTank_.del2.tap (rightTaps_[6] * s);

        left[n] = wetLeft * kWetGain;

        if (right != nullptr)
            right[n] = wetRight * kWetGain;
    }

    // Le due fasi avanzano una volta per fetta, della durata della fetta.
    lfoPhaseLeft_ += (float) (1.00 * (double) numSamples / sampleRate_);
    lfoPhaseRight_ += (float) (0.95 * (double) numSamples / sampleRate_);
    lfoPhaseLeft_ -= std::floor (lfoPhaseLeft_);
    lfoPhaseRight_ -= std::floor (lfoPhaseRight_);
}
} // namespace dsp
