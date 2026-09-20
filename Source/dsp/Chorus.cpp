#include "dsp/Chorus.h"

#include "dsp/Constants.h"
#include "dsp/Saturation.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
/**
 * La fase dell'LFO del tap `i`, in frazione di ciclo: `i / N`, **non** `i / (N - 1)`.
 *
 * Con la seconda forma il tap N-1 avrebbe fase 1.0, che e' la fase 0.0 del tap 0: due tap
 * modulati in modo identico e un settore di ciclo scoperto. E' il difetto che Surge XT ha in
 * produzione nel suo chorus. Con `i / N` le N fasi partizionano il ciclo in parti uguali senza
 * che i due estremi coincidano — che e' la definizione di "equidistanti su un cerchio".
 */
constexpr float tapPhase (int i) noexcept { return (float) i / (float) Chorus::kNumTaps; }

/**
 * La posizione di pan del tap `i` sull'asse -1..+1, larghezza kPanWidth.
 *
 * Qui la divisione e' per `N - 1`, ed e' giusta: il pan vive su un **segmento**, non su un
 * cerchio, quindi i due estremi sono punti distinti e il primo e l'ultimo tap devono starci
 * sopra. Confondere i due casi e' l'errore che rende la nota di tapPhase necessaria.
 */
constexpr float tapPan (int i) noexcept
{
    return Chorus::kNumTaps > 1
               ? Chorus::kPanWidth * (2.0f * (float) i / (float) (Chorus::kNumTaps - 1) - 1.0f)
               : 0.0f;
}
} // namespace

void Chorus::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    numChannels_ = juce::jlimit (1, 2, numChannels);

    baseDelaySamples_ = (float) (kBaseDelayMs * 0.001 * sampleRate_);
    maxDelaySamples_ = baseDelaySamples_ * (1.0f + kMaxDepthFraction);

    /**
     * I pesi dei tap sui due canali, a potenza costante.
     *
     * theta = (pan + 1) * pi/4 manda pan -1 su theta 0 (tutto a sinistra) e pan +1 su pi/2
     * (tutto a destra); il peso sinistro e' cos(theta), il destro sin(theta). Per un insieme di
     * posizioni **simmetrico** attorno a zero — il nostro, per costruzione di tapPan — vale
     * sum_i cos^2(theta_i) = sum_i sin^2(theta_i) = N/2, qualunque sia la larghezza.
     * Normalizzando per sqrt(2/N) la somma dei quadrati di ciascun canale diventa esattamente 1:
     * N tap decorrelati sommati cosi' hanno la potenza di un singolo tap a guadagno unitario.
     *
     * E' la stessa identita' che rende esatta la compensazione 1/sqrt(N) dell'unison, e per lo
     * stesso motivo: nessuna costante da ritarare se un giorno N cambia. In mono gira il solo
     * insieme sinistro, che ha per conto suo somma dei quadrati 1: anche li' niente da
     * compensare.
     */
    const auto norm = std::sqrt (2.0f / (float) kNumTaps);

    for (int i = 0; i < kNumTaps; ++i)
    {
        const auto theta = (tapPan (i) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        tapGainLeft_[(size_t) i] = std::cos (theta) * norm;
        tapGainRight_[(size_t) i] = std::sin (theta) * norm;
    }

    // +4: Lagrange3rd legge tre campioni oltre l'indice intero del ritardo, e juce::dsp::
    // DelayLine mette una jassert su setDelay() quando il valore chiesto supera il massimo
    // dichiarato. Il margine tiene il ritardo massimo raggiungibile strettamente sotto il tetto.
    line_.setMaximumDelayInSamples ((int) std::ceil (maxDelaySamples_) + 4);

    juce::dsp::ProcessSpec spec {};
    spec.sampleRate = sampleRate_;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maximumBlockSize);
    spec.numChannels = (juce::uint32) 2; // sempre due linee: il mono usa solo la prima
    line_.prepare (spec);

    // Polo singolo di emivita kDelaySmoothingHalfLifeSeconds: dopo h secondi la distanza dal
    // bersaglio si e' dimezzata. Il coefficiente per campione e' 1 - 2^(-1/(h*fs)).
    smoothingCoeff_ = halfLifeCoefficient (kDelaySmoothingHalfLifeSeconds, sampleRate_);

    reset();
}

void Chorus::reset() noexcept
{
    line_.reset();
    lfoPhase_ = 0.0f;
    delayPrimed_ = false;
    delaySamples_.fill (baseDelaySamples_);
}

void Chorus::setParameters (float rateHz, float depth01, float feedback01) noexcept
{
    rateHz_ = juce::jmax (0.0f, rateHz);
    depth01_ = juce::jlimit (0.0f, 1.0f, depth01);
    feedbackGain_ = juce::jlimit (0.0f, 1.0f, feedback01) * kMaxFeedback;
}

void Chorus::process (float* left, float* right, int numSamples) noexcept
{
    forEachSlice (numSamples, kModulationSliceSamples, [&] (int offset, int slice)
    {
        processSlice (left + offset, right != nullptr ? right + offset : nullptr, slice);
    });
}

void Chorus::processSlice (float* left, float* right, int numSamples) noexcept
{
    const auto stereo = right != nullptr && numChannels_ > 1;
    const auto numLines = stereo ? 2 : 1;
    const auto depthFraction = kMaxDepthFraction * depth01_;

    // Il bersaglio dei sei ritardi, calcolato una volta per fetta. Il ciclo per campione non
    // chiama nessuna funzione di libm: qui ci sono tutti e sei i std::sin di questa fetta.
    float target[2 * kNumTaps] {};

    for (int ch = 0; ch < numLines; ++ch)
    {
        for (int i = 0; i < kNumTaps; ++i)
        {
            const auto phase = lfoPhase_ + tapPhase (i) + (float) ch * kChannelPhaseOffset;
            const auto lfo = std::sin (juce::MathConstants<float>::twoPi * phase);

            // Il limite basso non e' cosmetico: Lagrange3rd vuole almeno un campione intero di
            // ritardo (updateInternalVariables sposta la parte frazionaria solo se delayInt >= 1).
            target[ch * kNumTaps + i] =
                juce::jlimit (4.0f, maxDelaySamples_, baseDelaySamples_ * (1.0f + depthFraction * lfo));
        }
    }

    // Alla prima fetta dopo un reset i poli partono **sul** bersaglio invece che dal ritardo
    // base: altrimenti l'accensione dell'effetto comincerebbe con una scivolata di ritardo che
    // non ha niente a che vedere con l'LFO, udibile come un glissando.
    if (! delayPrimed_)
    {
        for (int k = 0; k < numLines * kNumTaps; ++k)
            delaySamples_[(size_t) k] = target[k];

        delayPrimed_ = true;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        const float dry[2] = { left[n], stereo ? right[n] : 0.0f };
        float wet[2] = { 0.0f, 0.0f };

        for (int ch = 0; ch < numLines; ++ch)
        {
            const auto* gains = ch == 0 ? tapGainLeft_.data() : tapGainRight_.data();
            auto sum = 0.0f;

            for (int i = 0; i < kNumTaps; ++i)
            {
                const auto k = (size_t) (ch * kNumTaps + i);
                delaySamples_[k] += (target[ch * kNumTaps + i] - delaySamples_[k]) * smoothingCoeff_;

                // updateReadPointer solo sull'ultimo tap del canale: il puntatore di lettura
                // avanza di un campione per canale per campione, non una volta per tap.
                sum += gains[i] * line_.popSample (ch, delaySamples_[k], i == kNumTaps - 1);
            }

            wet[ch] = sum;
        }

        for (int ch = 0; ch < numLines; ++ch)
        {
            // Il ritorno passa dal Pade di tanh **prima** di rientrare: |saturate| <= 1 sempre,
            // quindi il contributo di feedback e' limitato a kMaxFeedback qualunque cosa ci sia
            // nella linea, e il contenuto della linea a |secco| + kMaxFeedback. L'anello e'
            // limitato per costruzione, non per taratura del guadagno. A feedback zero il ramo
            // e' esattamente zero e non tocca un solo campione.
            const auto feedback = feedbackGain_ > 0.0f
                                      ? feedbackGain_ * saturateCurve (wet[ch])
                                      : 0.0f;

            line_.pushSample (ch, dry[ch] + feedback);
        }

        left[n] = wet[0];

        if (right != nullptr)
            right[n] = stereo ? wet[1] : wet[0];
    }

    // L'LFO avanza una volta per fetta, della durata della fetta: il tasso di modulazione e'
    // quindi kModulationSliceSamples campioni, non il blocco dell'host.
    lfoPhase_ += (float) (rateHz_ * (double) numSamples / sampleRate_);
    lfoPhase_ -= std::floor (lfoPhase_);
}
} // namespace dsp
