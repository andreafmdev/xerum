#include "dsp/StateVariableFilter.h"

#include <algorithm>
#include <cmath>

namespace dsp
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

/*
 * Non c'e' nessun `kResonanceCompensation`, e la sua assenza e' una misura, non una svista:
 * con la saturazione nell'anello il picco a Q 24 sta a +9.5 dB sopra il Butterworth, quindi
 * l'attenuazione d'ingresso non ha piu' niente da domare — mentre continuerebbe a costare in
 * banda passante esattamente i suoi 20*p*log10(Q/Qbutter) dB, cioe' 3.8 con l'esponente 1/8
 * che c'era prima e 14.7 con il vecchio 1/2. Sarebbe un costo senza contropartita.
 * Rimetterne una, di qualunque esponente, fa fallire "la banda passante non paga niente alla
 * risonanza" in Tests/EnvelopeFilterTests.cpp.
 */
/**
 * Forza della saturazione dell'integratore del bandpass, scelta sulle misure (seno, RMS a
 * regime, 44.1/48/96 kHz, cutoff da 50 Hz a 10 kHz, 12 e 24 dB, ingresso da 0.003 a 3).
 *
 * A 0.8 il picco a Q 24 scende da +23.8 a +9.5 dB sopra il Butterworth (ingresso 0.3) senza
 * togliere niente alla banda passante. Piu' piccolo non basta (a 0.4 il picco resta a +12.1, e
 * il margine sul tetto dei 10 dB di riduzione si assottiglia a 1.7 dB); piu' grande accorcia la
 * corsa di `res` senza che serva — a 2.0 il picco a Q 24 e' +5.6 e i gradini fra un Q e il
 * successivo si schiacciano sotto i 0.2 dB.
 *
 * Il compromesso vero non e' il picco, e' la **corsa**: comprimendo, i decibel fra Butterworth e
 * fondo corsa passano da 26.8 a 12.5 (ingresso 0.3) e a 7.5 (ingresso a fondo scala). Alzare
 * `res` continua a sentirsi, ma meno violentemente di prima, ed e' il prezzo dichiarato per non
 * pagare piu' niente in banda passante.
 */
constexpr float kIntegratorSaturation = 0.8f;
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

    // Il reciproco, non il denominatore: e' costante per blocco, mentre la divisione stava
    // **sul cammino critico** del loop audio (stage.s1 -> highPass -> stage.s1), dove conta la
    // latenza e non il throughput. Misurato in Release (clang -O3 -flto, Apple Silicon):
    // 10.9 -> 9.6 ns per campione, cioe' **1.3 ns**. Una versione semplificata, scritta a mano
    // per stimare il guadagno prima di farlo, ne prometteva 3.7: la stima era sbagliata e la
    // causa non e' stata trovata (passare i Coefficients per valore invece che per riferimento,
    // l'ipotesi piu' ovvia, non cambia niente — provato). Resta un guadagno vero e gratuito, ma
    // non ripaga la saturazione: il filtro sta comunque a +78% su HEAD.
    //
    // Il prezzo e' un ulp: `x * (1/d)` arrotonda due volte dove `x / d` arrotondava una sola.
    // Misurato su tutto quello che i test guardano — banda passante contro la risposta lineare,
    // picco per Q a otto valori, indipendenza da cutoff e sample rate, dipendenza dal livello —
    // lo scarto fra le due versioni e' **0.0000 dB**: sta sotto il pavimento della misura.
    const auto inverseDenominatorFor = [this] (float twoR) noexcept
    {
        return 1.0f / (1.0f + twoR * g_ + g_ * g_);
    };

    butterworth_.twoR = 1.0f / kButterworthQ;
    butterworth_.inverseDenominator = inverseDenominatorFor (butterworth_.twoR);

    resonant_.twoR = 1.0f / resonance_;
    resonant_.inverseDenominator = inverseDenominatorFor (resonant_.twoR);

    // La forza della saturazione, precalcolata qui: nel loop audio non entra nessuna divisione
    // in piu' rispetto a questa, e nessuna chiamata a libm.
    //
    // Due normalizzazioni, e nessuna delle due e' cosmetica.
    //
    // 1) `g / denominatore` e' il guadagno per campione con cui l'ingresso entra nell'anello
    //    discreto. Senza di esso la saturazione, che agisce una volta per campione, morderebbe
    //    tanto piu' quanto piu' l'anello ricircola: misurato, a coefficiente fisso il picco a
    //    Q 24 andrebbe da +19.7 dB a cutoff 50 Hz a +2.5 dB a 16 kHz, e cambierebbe di 3 dB fra
    //    44.1 e 96 kHz. Cioe' la risonanza svanirebbe in cima alla corsa del cutoff. Con questa
    //    normalizzazione resta entro 0.2 dB da 50 Hz a 6 kHz e a tutti e tre i sample rate.
    //    (E' lo stesso motivo per cui il `ClipDamp` di Surge contiene il coefficiente di cutoff.)
    //
    // 2) `1 - Qbutter/Q` e' la frazione dello smorzamento di Butterworth che la risonanza ha
    //    tolto. La saturazione lo restituisce in proporzione al segnale, e al minimo della corsa
    //    di `res` vale **esattamente** zero: a Q di Butterworth il filtro e' quello lineare di
    //    sempre, campione per campione, a qualunque livello. Senza questo fattore il filtro
    //    neutro perderebbe 1.1 dB al cutoff a fondo scala e ne perderebbe 0.4 a ingresso 0.3,
    //    cioe' un fade-in gli cambierebbe timbro.
    resonant_.saturation = kIntegratorSaturation
                               * (1.0f - kButterworthQ / resonance_) * g_ * resonant_.inverseDenominator;
}

float StateVariableFilter::processStage (Stage& stage, const Coefficients& c, float input) const noexcept
{
    const auto highPass = (input - (c.twoR + g_) * stage.s1 - stage.s2) * c.inverseDenominator;
    const auto bandPass = g_ * highPass + stage.s1;

    // Il punto in cui la risonanza viene domata: l'integratore del bandpass non e' piu' lineare.
    //
    // `v / (1 + a|v|)` e' l'identita' sul piccolo segnale (derivata 1 nell'origine, nessuna
    // colorazione di cio' che non risuona) e comprime progressivamente quando l'anello squilla.
    // Non ha ne' spigoli ne' clamp: |sat(v)| < 1/a per costruzione, quindi l'anello non puo'
    // divergere nemmeno a Q massimo. Rispetto alla forma quadratica alla Surge
    // (R = max(0.1, 1 - k*B^2)) e' monotona con la stessa forza di compressione e non ha bisogno
    // del max(); la variante di Surge che fa dipendere k anche da sqrt(Q) e' stata misurata e
    // **scartata**: rende il picco non monotono in Q (sopra Q 8 alzare `res` lo abbassa).
    //
    // `c.saturation` vale 0 sullo stadio Butterworth e a Q di Butterworth: li' la divisione e'
    // per 1.0f esatto, cioe' l'identita' bit per bit.
    const auto integrated = g_ * highPass + bandPass;
    stage.s1 = integrated / (1.0f + c.saturation * std::abs (integrated));

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
    // commento di classe. Il segnale entra com'e': non c'e' nessuna attenuazione d'ingresso.
    if (numStages_ > 1)
        return processStage (stages_[1], resonant_, processStage (stages_[0], butterworth_, input));

    return processStage (stages_[0], resonant_, input);
}
} // namespace dsp
