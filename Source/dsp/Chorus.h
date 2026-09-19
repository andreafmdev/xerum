#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

namespace dsp
{
/**
 * Chorus stereo a tre tap su linea di ritardo modulata.
 *
 * Produce **solo il segnale bagnato**: il dry/wet, il bypass e il ringout non stanno qui ma
 * nello stadio FX di SynthEngine, perche' sono le parti che il riverbero riusera' identiche.
 * Questa classe sa fare una cosa sola, e la interfaccia e' quella di un effetto al 100 % di wet:
 * `process()` legge i campioni dai due canali e ci riscrive sopra il bagnato.
 *
 * ## La topologia, con i numeri
 *
 * **Tre tap** e non quattro, e la ragione e' aritmetica, non estetica. Le fasi dell'LFO sono
 * equidistanti a `i / N` (vedi tapPhase() in Chorus.cpp) e i due canali sono sfasati di 90 gradi, cioe' di
 * 0.25 di ciclo, come fa Vital. Con N = 4 le fasi del canale destro — 0.25, 0.50, 0.75, 1.00 —
 * sarebbero **lo stesso insieme** di quelle del sinistro (0, 0.25, 0.50, 0.75), solo permutate:
 * i due canali modulerebbero i ritardi sugli stessi istanti e l'immagine stereo collasserebbe
 * a una permutazione dei pesi di pan. Con N = 3 le fasi sono 0, 1/3, 2/3 a sinistra e 1/4,
 * 7/12, 11/12 a destra: sei valori distinti, nessuna coincidenza, sfasamento di canale che
 * resta un vero sfasamento. Vale per ogni N multiplo di 4, quindi anche l'unica alternativa
 * "piu' ricca" al nostro tre e' proprio quella da non prendere.
 *
 * **`i / N` e non `i / (N - 1)`**: con il secondo l'ultimo tap tornerebbe a fase 1.0, che e'
 * la fase 0.0 del primo — due tap identici e un buco nella distribuzione. E' un difetto che
 * Surge ha in produzione. Attenzione a non confondere questa divisione con quella del **pan**
 * dei tap (tapPan()), dove `i / (N - 1)` e' invece giusto: li' gli estremi -1 e +1 vanno
 * inclusi entrambi, e il primo e l'ultimo tap *devono* stare ai due bordi.
 *
 * **Ritardo base 11 ms**, al centro della finestra che rende un chorus un chorus: sotto i 5 ms
 * il battimento diventa un flanger (il pettine cade in banda udibile), sopra i 30 si sente
 * come uno slapback distinto invece che come un ispessimento.
 *
 * **Profondita' come frazione del ritardo base**, fino a +-50 %: a fondo corsa il ritardo
 * oscilla fra 5.5 e 16.5 ms, cioe' resta dentro la finestra sopra a entrambi gli estremi. Una
 * profondita' assoluta in millisecondi avrebbe dovuto essere limitata al ritardo base per non
 * andare negativa, quindi sarebbe stata comunque una frazione, scritta peggio.
 *
 * **Pan a potenza costante dei tap.** Il tap i sta in posizione `kTapPan[i]` sull'asse -1..+1
 * e contribuisce al canale sinistro con cos(theta) e al destro con sin(theta). La somma dei
 * quadrati dei pesi di ciascun canale e' esattamente 1 (vedi il commento in prepare()): tre
 * tap decorrelati sommati con questi pesi hanno la stessa potenza di uno solo a guadagno
 * unitario, che e' cio' che tiene il bagnato allo stesso livello del secco senza una
 * compensazione da ritarare a ogni cambio di N.
 *
 * ## Interpolazione
 *
 * `Lagrange3rd`, non `Thiran`. Thiran e' un passa-tutto del primo ordine: ampiezza piatta, ma
 * fase non lineare e — soprattutto — **stato interno**, quindi ogni volta che il ritardo cambia
 * produce un transitorio. In un delay modulato il ritardo cambia a ogni campione, quindi il
 * transitorio e' permanente. Lagrange e' senza stato: legge quattro campioni e pesa.
 *
 * ## Anti-zipper
 *
 * Il ritardo bersaglio dei sei tap (tre per canale) si calcola **una volta per fetta**, e ogni
 * campione ci si avvicina con un polo singolo di emivita 20 ms. Senza, alla velocita' massima
 * (5.1 Hz, profondita' a fondo corsa) il bersaglio salterebbe di 5.6 campioni fra una fetta e
 * l'altra: una discontinuita' di valore sul ritardo, che si sente come clic. Surge usa una
 * costante di tempo di 22.7 ms, Vital un'emivita di 20: concordano, e questo e' il secondo
 * numero.
 *
 * Il prezzo, che e' bene sapere: il polo e' un passa-basso a `ln2 / (2*pi*0.02)` = 5.5 Hz
 * sulla modulazione, quindi alla velocita' massima la profondita' effettiva scende di 2.7 dB.
 * Al valore di default (1.6 Hz) la perdita e' dello 0.4 dB, cioe' niente.
 *
 * ## Feedback
 *
 * Il bagnato di ogni canale rientra nella linea attraverso `engine::saturateCurve`, il Pade di
 * tanh gia' usato dal drive del filtro. Non e' un vezzo: senza una nonlinearita' in mezzo, con
 * feedback e mix alti il chorus da solo si mangerebbe il margine al soft clipper d'uscita. Con
 * la saturazione il contributo di ritorno e' **limitato per costruzione** a kMaxFeedback,
 * qualunque cosa ci sia nella linea, quindi il contenuto della linea non puo' superare
 * |secco| + kMaxFeedback e l'anello e' incondizionatamente stabile. Sia Surge (`hardclip_block`) sia
 * Vital (`saturateLarge`) mettono qualcosa nello stesso identico punto.
 */
class Chorus
{
public:
    /** Numero di tap per canale. Vedi il commento della classe: tre, e non quattro, perche' a
        quattro le fasi del canale destro coinciderebbero con quelle del sinistro. */
    static constexpr int kNumTaps = 3;

    /** Ritardo base, in millisecondi. Al centro della finestra 5..30 ms del chorus. */
    static constexpr float kBaseDelayMs = 11.0f;

    /** Escursione massima del ritardo, come frazione del ritardo base: 5.5..16.5 ms. */
    static constexpr float kMaxDepthFraction = 0.5f;

    /**
     * Guadagno di ritorno a `chFeedback` = 100 %.
     *
     * Mezzo, che e' dove si fermano le unita' classiche: sopra, il pettine smette di ispessire e
     * comincia a intonarsi — l'effetto diventa un risonatore, non un chorus. E' anche il punto
     * in cui il costo sul margine d'uscita smette di ripagare. Misurato sul picco presentato al
     * soft clipper con mix al 100 % su un accordo di sei note, rispetto allo stesso accordo
     * secco:
     *
     *     kMaxFeedback   picco al clipper   rispetto al secco
     *        0.4              1.415              +1.93 dB
     *        0.5              1.454              +2.16 dB
     *        0.6              1.523              +2.56 dB
     *        0.7              1.593              +2.95 dB
     *
     * Il ritorno e' gia' saturato, quindi non e' la stabilita' a fissare il tetto — l'anello e'
     * limitato per costruzione a qualunque guadagno < 1 — ma il margine: docs/architecture.md
     * dice che ne restano 3 dB, e mangiarne quasi tutti per l'ultimo quarto di corsa di un knob
     * non e' uno scambio che si fa.
     */
    static constexpr float kMaxFeedback = 0.5f;

    /** Emivita del polo singolo che insegue il ritardo bersaglio, in secondi. */
    static constexpr float kDelaySmoothingHalfLifeSeconds = 0.020f;

    /**
     * Larghezza del ventaglio di pan dei tap, in unita' di pan (-1..+1).
     *
     * 0.8 e non 1.0 per la stessa ragione di engine::kUnisonSpreadWidth: a larghezza piena il
     * tap esterno finisce **interamente** in un canale, e a quel punto il canale opposto perde
     * un terzo dei tap invece di riceverne una versione attenuata. A 0.8 il tap piu' esterno
     * sta a circa 16 dB di sbilanciamento: largo, ma ancora presente da entrambe le parti.
     */
    static constexpr float kPanWidth = 0.8f;

    /**
     * Sfasamento dell'LFO fra i due canali, in frazione di ciclo. 0.25 = 90 gradi, come Vital.
     * E' cio' che rende il chorus stereo anche con un ingresso perfettamente mono.
     */
    static constexpr float kChannelPhaseOffset = 0.25f;

    /**
     * Dimensiona la linea di ritardo e azzera lo stato. **Alloca**: solo da prepareToPlay.
     *
     * `maxDelaySamples` tiene conto dell'escursione massima piu' i tre campioni che Lagrange
     * legge oltre l'indice intero, piu' uno di margine: il ritardo richiesto non puo' quindi
     * mai arrivare al tetto su cui juce::dsp::DelayLine mette una jassert.
     */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);

    /** Azzera la linea e lo stato dei poli. Non alloca: chiamabile dal thread audio. */
    void reset() noexcept;

    /** Velocita' dell'LFO in Hz, profondita' e feedback normalizzati 0..1. Una volta per blocco. */
    void setParameters (float rateHz, float depth01, float feedback01) noexcept;

    /**
     * Riscrive `left` e `right` con il **solo** segnale bagnato. `right` puo' essere nullptr:
     * in mono gira un solo tap set, quello del canale sinistro, e la linea destra non viene
     * nemmeno toccata.
     *
     * Nessuna allocazione, nessun lock. Il ciclo interno si spezza in fette di al piu'
     * `kModulationSliceSamples` campioni: il bersaglio del ritardo si ricalcola li' dentro,
     * quindi il tasso di modulazione del chorus e' una proprieta' dell'effetto e non della
     * dimensione del buffer che passa l'host — la stessa scelta, e lo stesso numero, delle
     * sotto-fette di controllo del motore.
     */
    void process (float* left, float* right, int numSamples) noexcept;

    /** La lunghezza massima di una fetta di modulazione, in campioni. */
    static constexpr int kModulationSliceSamples = 32;

private:
    void processSlice (float* left, float* right, int numSamples) noexcept;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line_;

    double sampleRate_ { 44100.0 };
    int numChannels_ { 2 };
    float baseDelaySamples_ { 0.0f };
    float maxDelaySamples_ { 0.0f };
    float smoothingCoeff_ { 1.0f };

    float rateHz_ { 1.0f };
    float depth01_ { 0.0f };
    float feedbackGain_ { 0.0f };

    float lfoPhase_ { 0.0f };

    /** Il ritardo corrente dei sei tap (canale x tap), in campioni. Indice: ch * kNumTaps + i. */
    std::array<float, 2 * kNumTaps> delaySamples_ {};
    bool delayPrimed_ { false };

    /** I pesi di pan a potenza costante, calcolati una volta in prepare(). Vedi il commento
        che li costruisce: la somma dei quadrati di ciascun array vale esattamente 1. */
    std::array<float, kNumTaps> tapGainLeft_ {};
    std::array<float, kNumTaps> tapGainRight_ {};
};
} // namespace dsp
