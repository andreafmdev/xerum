#include "dsp/ADSREnvelope.h"
#include "dsp/StateVariableFilter.h"

#include <juce_core/juce_core.h>
#include <cmath>

struct ADSRTests final : juce::UnitTest
{
    ADSRTests() : juce::UnitTest ("ADSREnvelope", "dsp") {}

    static float runFor (dsp::ADSREnvelope& env, int samples)
    {
        float last = 0.0f;
        for (int i = 0; i < samples; ++i)
            last = env.getNextSample();
        return last;
    }

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("attack reaches peak within stated time, not before");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            const auto halfway = runFor (env, 2400);  // 50 ms
            expect (halfway < 0.99f, "at midpoint attack not yet complete");

            const auto atEnd = runFor (env, 2400);    // 100 ms total
            expect (atEnd >= 0.99f, "level at attack end " + juce::String (atEnd));
        }

        beginTest ("decay reaches sustain level and holds");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.4f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            runFor (env, 48);                          // attack
            const auto afterDecay = runFor (env, 2400); // 50 ms
            expectWithinAbsoluteError (afterDecay, 0.4f, 0.02f);

            const auto later = runFor (env, 48000);     // one second of sustain
            expectWithinAbsoluteError (later, 0.4f, 0.001f);
        }

        beginTest ("decay timing correct with high sustain level");
        {
            // This test pins the decay distance fix: sustain close to peak
            // should not cause immediate transition to sustain stage.
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.995f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            runFor (env, 48);                          // attack
            const auto halfway = runFor (env, 1200);   // 25 ms into decay
            expect (halfway > 0.995f, "partway through decay level above sustain");

            const auto atEnd = runFor (env, 1200);     // 50 ms total decay
            expectWithinAbsoluteError (atEnd, 0.995f, 0.005f);
        }

        beginTest ("release falls below -80 dB and envelope goes inactive");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.001f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.05f);
            env.noteOn (1.0f);
            runFor (env, 480);

            env.noteOff();
            expect (env.isActive(), "during release voice is still active");

            const auto tail = runFor (env, 2400); // 50 ms
            expect (tail < 1.0e-4f, "tail level " + juce::String (tail));
            expect (! env.isActive(), "after release voice is freed");
        }

        beginTest ("velocity parameter scales the peak");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (10.0f);
            env.setSustainLevel (1.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (0.5f);

            const auto peak = runFor (env, 480);
            expectWithinAbsoluteError (peak, 0.5f, 0.02f);
        }

        beginTest ("sustain a zero: l'inviluppo si spegne da solo, senza note-off");
        {
            // Il difetto che questo test blocca: con sustain 0 il decay scendeva a zero e
            // parcheggiava in Stage::sustain, che non transita mai a idle. La voce restava
            // occupata a rendere silenzio finche' VoiceManager non gliela rubava con kill()
            // — un clic a ogni nota di un patch percussivo.
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.0f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);

            const auto tail = runFor (env, (int) sampleRate); // un secondo, nota ancora premuta
            expect (tail <= 1.0e-4f, "livello dopo il decay " + juce::String (tail));
            expect (! env.isActive(), "con sustain 0 la voce deve liberarsi senza note-off");
        }

        beginTest ("sustain piccolo ma legittimo continua a sostenere");
        {
            // L'altra faccia: 0.01 e' -40 dB, ben sopra la soglia di silenzio, e deve restare
            // in piedi finche' non arriva il note-off.
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.05f);
            env.setSustainLevel (0.01f);
            env.setReleaseSeconds (0.01f);
            env.noteOn (1.0f);

            const auto held = runFor (env, (int) sampleRate);
            expectWithinAbsoluteError (held, 0.01f, 0.001f);
            expect (env.isActive(), "sustain 0.01 deve restare attivo");

            env.noteOff();
            runFor (env, 4800); // 100 ms, dieci volte il release
            expect (! env.isActive(), "dopo il note-off deve comunque liberarsi");
        }

        beginTest ("sustain sotto la soglia per via della velocity: si spegne");
        {
            // peak 0.005 * sustain 0.01 = -86 dB: inudibile, la voce va liberata lo stesso.
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.001f);
            env.setDecaySeconds (0.01f);
            env.setSustainLevel (0.01f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (0.005f);

            runFor (env, (int) sampleRate);
            expect (! env.isActive(), "sustain effettivo sotto -80 dB deve spegnere la voce");
        }

        beginTest ("getLevel() ritorna il livello corrente senza avanzare l'inviluppo");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (0.1f);
            env.setSustainLevel (0.5f);
            env.setReleaseSeconds (0.1f);

            expectWithinAbsoluteError (env.getLevel(), 0.0f, 1.0e-6f);

            env.noteOn (1.0f);
            runFor (env, (int) (sampleRate * 0.05));   // meta' dell'attacco

            const auto snapshot = env.getLevel();
            expect (snapshot > 0.0f && snapshot < 1.0f, "l'inviluppo dovrebbe essere a meta' attacco");

            // E' una lettura, non un passo: chiamarla due volte di fila non cambia niente.
            expectWithinAbsoluteError (env.getLevel(), snapshot, 1.0e-9f);
        }

        beginTest ("reset clears all state");
        {
            dsp::ADSREnvelope env;
            env.prepare (sampleRate);
            env.setAttackSeconds (0.1f);
            env.setDecaySeconds (0.1f);
            env.setSustainLevel (0.5f);
            env.setReleaseSeconds (0.1f);
            env.noteOn (1.0f);
            runFor (env, 480);
            env.reset();
            expect (! env.isActive());
            expectWithinAbsoluteError (env.getNextSample(), 0.0f, 0.0f);
        }
    }
};

static ADSRTests adsrTests;

namespace
{
/**
 * Ampiezza in uscita dal filtro a una data frequenza, misurata a regime, per un seno di
 * ampiezza `amplitude`.
 *
 * L'ampiezza e' un parametro e non piu' una costante perche' il filtro **non e' lineare**:
 * l'integratore del bandpass satura, quindi "il guadagno del filtro" non esiste da solo,
 * esiste solo insieme al livello a cui lo si misura. Ogni misura di risonanza qui sotto
 * dichiara il proprio livello, ed e' il livello a decidere che cosa si sta misurando: sul
 * piccolo segnale il filtro e' quello lineare di sempre, a fondo scala e' domato.
 */
float filterGainAt (dsp::StateVariableFilter& filter, float frequencyHz, double sampleRate,
                    float amplitude = 1.0f)
{
    filter.reset();

    const auto increment = 2.0 * juce::MathConstants<double>::pi * (double) frequencyHz / sampleRate;
    const int settle = (int) (sampleRate * 0.2);
    const int measure = (int) (sampleRate * 0.1);

    double phase = 0.0;
    for (int i = 0; i < settle; ++i, phase += increment)
        filter.processSample ((float) ((double) amplitude * std::sin (phase)));

    double sumSquares = 0.0;
    for (int i = 0; i < measure; ++i, phase += increment)
    {
        const auto out = filter.processSample ((float) ((double) amplitude * std::sin (phase)));
        sumSquares += (double) out * (double) out;
    }

    // RMS in uscita diviso l'RMS del seno d'ingresso (cioe' amplitude/sqrt(2)).
    return (float) (std::sqrt (sumSquares / measure) * std::sqrt (2.0) / (double) amplitude);
}

float dbRatio (float gain, float reference) noexcept
{
    return 20.0f * std::log10 (gain / reference);
}

float dbOf (float gain) noexcept
{
    return 20.0f * std::log10 (gain);
}

/**
 * Il livello a cui si leggono le tabelle di risonanza.
 *
 * 0.3 e non 1.0: e' l'ordine di grandezza con cui una voce arriva davvero al filtro (un
 * oscillatore a fondo scala moltiplicato per l'headroom di voce, -3 dB, e per un `level`
 * qualsiasi), ed e' il livello su cui e' stata scelta la forza della saturazione. Misurare
 * tutto a 1.0 non sarebbe sbagliato, sarebbe un'altra domanda: a fondo scala la compressione
 * dell'anello e' piu' forte e la corsa di `res` si accorcia (misurato: 7.5 dB invece di 12.5).
 */
constexpr float kProbeAmplitude = 0.3f;

/**
 * La risposta del **ramo lineare** dell'SVF TPT in forma chiusa: il prototipo analogico
 * 1/(s^2 + s/Q + 1) valutato in s = tan(pi*f/sr) / g, che e' esattamente la sostituzione che
 * la topologia realizza (g = tan(pi*fc/sr) manda il cutoff su se stesso e il resto lo warpa).
 *
 * Serve come metro di paragone: e' quello che il filtro farebbe se l'integratore non
 * saturasse. In banda passante la saturazione non deve spostare niente, e "niente" qui vuol
 * dire *questa* curva, non una banda larga scelta a occhio.
 */
float linearGainAt (double sampleRate, float cutoffHz, float frequencyHz, float q, int stages)
{
    const auto pi = juce::MathConstants<double>::pi;
    const auto g = std::tan (pi * (double) cutoffHz / sampleRate);
    const auto u = std::tan (pi * (double) frequencyHz / sampleRate) / g;

    const auto magnitude = [u] (double quality)
    {
        const auto real = 1.0 - u * u;
        const auto imaginary = u / quality;
        return 1.0 / std::sqrt (real * real + imaginary * imaginary);
    };

    // A 24 dB la risonanza sta solo sull'ultimo stadio: il primo resta Butterworth.
    return (float) (stages > 1 ? magnitude ((double) q) * magnitude ((double) dsp::StateVariableFilter::kButterworthQ)
                               : magnitude ((double) q));
}

/** L'intera corsa del parametro `res`, dal Butterworth al massimo. */
constexpr float kResonanceSweep[] = { 0.707f, 1.0f, 2.0f, 4.0f, 8.0f, 12.0f, 16.0f, 24.0f };
} // namespace

struct StateVariableFilterTests final : juce::UnitTest
{
    StateVariableFilterTests() : juce::UnitTest ("StateVariableFilter", "dsp") {}

    void runTest() override
    {
        constexpr double sampleRate = 48000.0;

        beginTest ("passa-basso Butterworth: -3 dB al cutoff");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (1);
            filter.setResonance (juce::MathConstants<float>::sqrt2 / 2.0f); // Q = 0.707
            filter.setCutoffHz (1000.0f);

            expectWithinAbsoluteError (filterGainAt (filter, 1000.0f, sampleRate), 0.707f, 0.03f);
            expect (filterGainAt (filter, 100.0f, sampleRate) > 0.95f, "in banda passante deve passare");
            expect (filterGainAt (filter, 8000.0f, sampleRate) < 0.1f, "tre ottave sopra deve essere spento");
        }

        beginTest ("passa-alto: specchio del passa-basso");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::highPass);
            filter.setNumStages (1);
            filter.setResonance (juce::MathConstants<float>::sqrt2 / 2.0f);
            filter.setCutoffHz (1000.0f);

            expect (filterGainAt (filter, 100.0f, sampleRate) < 0.1f);
            expect (filterGainAt (filter, 8000.0f, sampleRate) > 0.95f);
        }

        beginTest ("24 dB taglia piu ripido di 12 dB");
        {
            dsp::StateVariableFilter gentle, steep;
            for (auto* f : { &gentle, &steep })
            {
                f->prepare (sampleRate);
                f->setType (dsp::StateVariableFilter::Type::lowPass);
                f->setResonance (juce::MathConstants<float>::sqrt2 / 2.0f);
                f->setCutoffHz (1000.0f);
            }
            gentle.setNumStages (1);
            steep.setNumStages (2);

            expect (filterGainAt (steep, 4000.0f, sampleRate) < filterGainAt (gentle, 4000.0f, sampleRate) * 0.5f);
        }

        beginTest ("la risonanza non moltiplica il picco stadio per stadio");
        {
            // Il difetto che questo test blocca: con la risonanza su entrambi gli stadi, a Q 12
            // il passa-basso a 24 dB dava un picco di ~Q^2 (oltre +40 dB) e qualunque preset con
            // `res` alto mandava l'uscita in clipping. Ora la risonanza sta solo sull'ultimo
            // stadio, quindi il secondo non moltiplica il picco: lo attenua dei suoi 3 dB al
            // cutoff.
            //
            // Il valore atteso non e' piu' Q * (Qbutter/Q)^p, perche' non c'e' piu' nessuna
            // attenuazione d'ingresso: il picco lo governa la saturazione dentro l'anello, che
            // dipende dal livello e quindi non ha una forma chiusa. Cio' che resta verificabile
            // in modo esatto — e che e' la proprieta' per cui questo test esiste — e' il
            // **confronto fra i due numeri di stadi**: due stadi devono stare sotto uno.
            constexpr float q = 12.0f;
            constexpr double sampleRate24 = 48000.0;

            dsp::StateVariableFilter twelve, twentyFour;
            for (auto* f : { &twelve, &twentyFour })
            {
                f->prepare (sampleRate24);
                f->setType (dsp::StateVariableFilter::Type::lowPass);
                f->setResonance (q);
                f->setCutoffHz (1000.0f);
            }
            twelve.setNumStages (1);
            twentyFour.setNumStages (2);

            const auto peak12 = filterGainAt (twelve, 1000.0f, sampleRate24, kProbeAmplitude);
            const auto peak24 = filterGainAt (twentyFour, 1000.0f, sampleRate24, kProbeAmplitude);

            expect (peak24 < peak12 * 1.15f,
                    "il secondo stadio non deve moltiplicare il picco: 12 dB " + juce::String (peak12)
                        + ", 24 dB " + juce::String (peak24));

            // Il picco misurato a Q 12 e' +9.0 dB a 12 dB/ottava e +7.3 a 24 (livello 0.3).
            // La banda accettata e' +-3 dB attorno a quei numeri: larga abbastanza da non
            // inseguire il terzo decimale della saturazione, stretta abbastanza da escludere sia
            // il picco di prima (+18.5 dB: lo sfonda di 6 dB) sia un filtro che non risuona.
            expectWithinAbsoluteError (dbOf (peak12), 9.0f, 3.0f,
                    "picco a 12 dB: " + juce::String (dbOf (peak12)) + " dB");
            expectWithinAbsoluteError (dbOf (peak24), 7.3f, 3.0f,
                    "picco a 24 dB: " + juce::String (dbOf (peak24)) + " dB");

            // E la banda passante non paga niente: a Q 12 valeva 0.702 (-3.1 dB) con la
            // compensazione a 1/8, adesso deve valere quello che vale la risposta lineare del
            // filtro e basta. Il confronto e' contro la previsione in forma chiusa, non contro
            // una banda larga: e' questa asserzione a rendere impossibile reintrodurre
            // un'attenuazione d'ingresso di qualunque esponente.
            const auto passband = filterGainAt (twelve, 100.0f, sampleRate24, kProbeAmplitude);
            const auto expectedPassband = linearGainAt (sampleRate24, 1000.0f, 100.0f, q, 1);
            expectWithinAbsoluteError (dbRatio (passband, expectedPassband), 0.0f, 0.2f,
                    "a Q 12 la banda passante deve valere " + juce::String (expectedPassband)
                        + ", misurata " + juce::String (passband));
        }

        beginTest ("a Q di Butterworth la saturazione dell'anello e' spenta, a ogni livello");
        {
            // La saturazione e' proporzionale a (1 - Qbutter/Q), cioe' alla frazione dello
            // smorzamento di Butterworth che la risonanza ha tolto: al minimo della corsa di
            // `res` quel fattore e' **esattamente** zero e il filtro torna quello lineare di
            // prima, campione per campione. Senza questa proprieta' il filtro "neutro"
            // dipenderebbe dal livello, cioe' un fade-in cambierebbe timbro.
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (1);
            filter.setResonance (dsp::StateVariableFilter::kButterworthQ);
            filter.setCutoffHz (1000.0f);

            expect (filterGainAt (filter, 100.0f, sampleRate) > 0.95f,
                    "in banda passante deve restare a guadagno unitario");

            // Lo stesso guadagno a due livelli distanti 40 dB: se la saturazione fosse ancora
            // attiva qui, i due numeri divergerebbero (a Q 24 divergono di 12 dB).
            const auto quiet = filterGainAt (filter, 1000.0f, sampleRate, 0.01f);
            const auto loud = filterGainAt (filter, 1000.0f, sampleRate, 1.0f);
            expectWithinAbsoluteError (dbRatio (loud, quiet), 0.0f, 0.05f,
                    "a Butterworth il guadagno al cutoff non deve dipendere dal livello: "
                        + juce::String (dbOf (quiet)) + " dB a 0.01, " + juce::String (dbOf (loud))
                        + " dB a 1.0");
        }

        beginTest ("la banda passante non paga niente alla risonanza");
        {
            // Il difetto che questo test blocca, nelle sue due versioni successive: la
            // compensazione d'ingresso — sqrt(Qbutter/Q) prima, (Qbutter/Q)^(1/8) poi —
            // attenuava *tutto* il segnale, non solo il picco. A Q 24 lo strumento perdeva
            // 14.7 dB con la prima e 3.8 con la seconda, ed era aritmetica, non taratura: ogni
            // decibel tolto al picco ne toglieva uno alla banda passante.
            //
            // La soglia non e' piu' "la perdita prevista dalla formula", perche' non c'e' piu'
            // nessuna formula da prevedere: la perdita ammessa e' **zero**. Il confronto e'
            // contro la risposta lineare esatta dell'SVF alla stessa frequenza (linearGainAt),
            // con +-0.5 dB di tolleranza — misurato 0.02 dB nel caso peggiore, e la vecchia
            // compensazione a 1/8 lo sfonderebbe di 3.3 dB a Q 24. Confrontare contro la curva
            // vera invece che contro il Q 0.707 e' anche piu' severo: nemmeno il rialzo che la
            // risonanza porta gia' di suo a 0.25*fc puo' nascondere un'attenuazione.
            constexpr float kPassbandToleranceDb = 0.5f;

            for (int stages : { 1, 2 })
            {
                for (float cutoff : { 200.0f, 1000.0f, 6000.0f })
                {
                    for (float q : kResonanceSweep)
                    {
                        dsp::StateVariableFilter filter;
                        filter.prepare (sampleRate);
                        filter.setType (dsp::StateVariableFilter::Type::lowPass);
                        filter.setNumStages (stages);
                        filter.setResonance (q);
                        filter.setCutoffHz (cutoff);

                        const auto probe = cutoff * 0.25f;
                        const auto gain = filterGainAt (filter, probe, sampleRate, kProbeAmplitude);
                        const auto predicted = linearGainAt (sampleRate, cutoff, probe, q, stages);
                        const auto delta = dbRatio (gain, predicted);

                        expectWithinAbsoluteError (delta, 0.0f, kPassbandToleranceDb,
                                "stadi " + juce::String (stages) + ", cutoff " + juce::String (cutoff)
                                    + ", Q " + juce::String (q) + ": banda passante "
                                    + juce::String (delta) + " dB rispetto alla risposta lineare");
                    }
                }
            }
        }

        beginTest ("il picco risonante cresce con la risonanza, in modo monotono, ma resta domato");
        {
            // Le due facce della stessa moneta, e servono insieme:
            //  - se la saturazione fosse troppo debole il picco tornerebbe a +23.8 dB a Q 24,
            //    cioe' la sorgente di livello che costringeva ad alzare la compensazione;
            //  - se fosse troppo forte il picco resterebbe piatto e alzare `res` non si
            //    sentirebbe.
            //
            // Il tetto e' la soglia che il codice di prima non puo' superare: +13 dB e' 10.8 dB
            // sotto il +23.78 misurato con la compensazione a 1/8, quindi nessuna taratura
            // dell'attenuazione d'ingresso lo soddisfa senza pagare gli stessi 10.8 dB in banda
            // passante — che il test precedente vieta.
            //
            // Il passo minimo scende da 1.05x (+0.42 dB) a 1.012x (+0.10 dB) perche' la
            // compressione, per definizione, avvicina i gradini in cima alla corsa: misurato
            // +0.19 dB fra Q 16 e Q 24 nel caso peggiore (6 kHz, un solo stadio). Non e' un
            // allargamento che lascia passare il comportamento vecchio — quello il tetto lo
            // sfonda di 10 dB — ed entro la corsa vera di `res` (Q 0.707..12) i gradini restano
            // fra 0.5 e 4.4 dB.
            constexpr float kPeakCeilingDb = 13.0f;
            constexpr float kMinimumRunDb = 10.0f;

            for (int stages : { 1, 2 })
            {
                float previous = 0.0f;
                float reference = 0.0f;

                for (float q : kResonanceSweep)
                {
                    dsp::StateVariableFilter filter;
                    filter.prepare (sampleRate);
                    filter.setType (dsp::StateVariableFilter::Type::lowPass);
                    filter.setNumStages (stages);
                    filter.setResonance (q);
                    filter.setCutoffHz (1000.0f);

                    const auto peak = filterGainAt (filter, 1000.0f, sampleRate, kProbeAmplitude);

                    if (q == kResonanceSweep[0])
                        reference = peak;
                    else
                        expect (peak > previous * 1.012f,
                                "stadi " + juce::String (stages) + ": a Q " + juce::String (q)
                                    + " il picco (" + juce::String (peak) + ") non e' salito rispetto al Q precedente ("
                                    + juce::String (previous) + ")");

                    previous = peak;

                    if (q == 24.0f)
                    {
                        const auto run = dbRatio (peak, reference);
                        expect (run >= kMinimumRunDb,
                                "stadi " + juce::String (stages) + ": a Q 24 il picco e' solo "
                                    + juce::String (run) + " dB sopra il Butterworth");
                        expect (dbOf (peak) <= kPeakCeilingDb,
                                "stadi " + juce::String (stages) + ": a Q 24 il picco arriva a "
                                    + juce::String (dbOf (peak)) + " dB, oltre il tetto di "
                                    + juce::String (kPeakCeilingDb));
                    }
                }
            }
        }

        beginTest ("la saturazione dell'anello agisce solo quando l'anello risuona forte");
        {
            // E' l'invariante che nessun guadagno statico puo' soddisfare, ed e' la ragione
            // dell'intero cambiamento: l'attenuazione d'ingresso toglieva gli stessi decibel a
            // qualunque livello, mentre qui il filtro e' **lineare sul piccolo segnale** e
            // comprime solo quando l'anello e' eccitato davvero.
            //
            // Le due asserzioni si contendono il campo da lati opposti: la prima vieta che la
            // saturazione morda quando non serve (sul piccolo segnale il picco deve essere
            // quello analitico, +18.1 dB a Q 8, entro 1 dB), la seconda pretende che morda
            // quando serve (a fondo scala almeno 8 dB in meno; misurati 13.4). Un guadagno
            // statico, di qualunque esponente, fallisce la prima o la seconda.
            constexpr float q = 8.0f;

            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (1);
            filter.setResonance (q);
            filter.setCutoffHz (1000.0f);

            const auto tiny = filterGainAt (filter, 1000.0f, sampleRate, 0.003f);
            const auto full = filterGainAt (filter, 1000.0f, sampleRate, 1.0f);
            const auto predicted = linearGainAt (sampleRate, 1000.0f, 1000.0f, q, 1);

            expectWithinAbsoluteError (dbRatio (tiny, predicted), 0.0f, 1.0f,
                    "sul piccolo segnale il picco deve essere quello lineare ("
                        + juce::String (dbOf (predicted)) + " dB), misurato "
                        + juce::String (dbOf (tiny)));

            expect (dbRatio (tiny, full) >= 8.0f,
                    "a fondo scala l'anello deve comprimere: piccolo segnale "
                        + juce::String (dbOf (tiny)) + " dB, fondo scala " + juce::String (dbOf (full))
                        + " dB");
        }

        beginTest ("il picco domato non dipende dal cutoff ne' dal sample rate");
        {
            // La saturazione agisce una volta per campione, ma l'anello ricircola un numero di
            // campioni per ciclo che vale ~1/g: senza normalizzare, la stessa curva toglierebbe
            // 20 dB di picco a cutoff 50 Hz e 7 a 12 kHz — cioe' la risonanza sparirebbe in
            // fondo alla corsa del cutoff e cambierebbe suono fra 44.1 e 96 kHz. Il
            // coefficiente e' percio' proporzionale a g/denominatore, che e' il guadagno per
            // campione dell'anello discreto.
            //
            // Senza quella normalizzazione (saturazione a coefficiente fisso, che e' la forma
            // piu' ovvia) qui si misurerebbero +19.7 dB a 50 Hz contro +2.5 a 16 kHz: questo
            // test e' l'unico posto che lo vieta.
            constexpr float q = 12.0f;
            float reference = 0.0f;

            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                for (float cutoff : { 100.0f, 500.0f, 1000.0f, 3000.0f, 6000.0f })
                {
                    dsp::StateVariableFilter filter;
                    filter.prepare (rate);
                    filter.setType (dsp::StateVariableFilter::Type::lowPass);
                    filter.setNumStages (1);
                    filter.setResonance (q);
                    filter.setCutoffHz (cutoff);

                    const auto peak = dbOf (filterGainAt (filter, cutoff, rate, kProbeAmplitude));

                    if (reference == 0.0f)
                        reference = peak;

                    expectWithinAbsoluteError (peak, reference, 0.5f,
                            "a " + juce::String (rate) + " Hz, cutoff " + juce::String (cutoff)
                                + ": picco " + juce::String (peak) + " dB invece di "
                                + juce::String (reference));
                }
            }
        }

        beginTest ("risonanza massima e cutoff estremi: niente instabilita'");
        {
            // Matrice completa: tre sample rate, i due estremi del cutoff, uno e due stadi, e
            // tutti e tre i tipi. I tipi contano ora piu' di prima: la saturazione sta
            // sull'integratore del bandpass, quindi il passa-banda la vede in uscita diretta e
            // il passa-alto la vede rientrare nell'anello senza il passa-basso davanti.
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                for (float cutoff : { 10.0f, (float) (rate * 0.49) })
                {
                    for (int stages : { 1, 2 })
                    for (auto type : { dsp::StateVariableFilter::Type::lowPass,
                                       dsp::StateVariableFilter::Type::highPass,
                                       dsp::StateVariableFilter::Type::bandPass })
                    {
                        dsp::StateVariableFilter filter;
                        filter.prepare (rate);
                        filter.setType (type);
                        filter.setNumStages (stages);
                        filter.setResonance (24.0f);
                        filter.setCutoffHz (cutoff);
                        filter.reset();

                        // Seno esattamente sul cutoff: il caso peggiore, eccita il picco in pieno.
                        const auto increment = 2.0 * juce::MathConstants<double>::pi * (double) cutoff / rate;
                        double phase = 0.0;
                        float peak = 0.0f;

                        for (int i = 0; i < (int) (rate * 2.0); ++i, phase += increment)
                        {
                            const auto out = filter.processSample ((float) std::sin (phase));
                            expect (std::isfinite (out),
                                    "uscita non finita a " + juce::String (rate) + " Hz, cutoff "
                                        + juce::String (cutoff) + ", stadi " + juce::String (stages));
                            peak = juce::jmax (peak, std::abs (out));
                        }

                        expect (peak < 100.0f,
                                "picco " + juce::String (peak) + " a " + juce::String (rate) + " Hz, cutoff "
                                    + juce::String (cutoff) + ", stadi " + juce::String (stages));
                    }
                }
            }
        }

        beginTest ("risonanza alta su tutto il range: nessun NaN, nessuna esplosione");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (2);
            filter.setResonance (20.0f);

            for (float cutoff : { 20.0f, 200.0f, 2000.0f, 19000.0f, 40000.0f })
            {
                filter.setCutoffHz (cutoff);
                filter.reset();

                float peak = 0.0f;
                for (int i = 0; i < 48000; ++i)
                {
                    const auto out = filter.processSample (i == 0 ? 1.0f : 0.0f);
                    expect (std::isfinite (out), "uscita non finita a cutoff " + juce::String (cutoff));
                    peak = juce::jmax (peak, std::abs (out));
                }

                expect (peak < 100.0f, "picco " + juce::String (peak) + " a cutoff " + juce::String (cutoff));
            }
        }

        beginTest ("switching stages back on does not resurrect old state");
        {
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setResonance (12.0f); // alta risonanza: lo stadio 2 accumula parecchia energia
            filter.setCutoffHz (300.0f);
            filter.setNumStages (2);

            // Fa girare un segnale attraverso due stadi cosi' lo stadio 2 accumula stato.
            double phase = 0.0;
            const auto increment = 2.0 * juce::MathConstants<double>::pi * 300.0 / sampleRate;
            for (int i = 0; i < 4800; ++i, phase += increment)
                filter.processSample ((float) std::sin (phase));

            // Passa a un solo stadio: lo stadio 2 smette di essere processato ma il suo stato resta.
            filter.setNumStages (1);

            // Silenzio con un solo stadio attivo, abbastanza a lungo da lasciare che anche lo
            // stadio 1 (che a questa risonanza continua a squillare) si spenga completamente:
            // quello che sopravvive dopo e' solo lo stato congelato dello stadio 2, non ancora
            // riattivato.
            for (int i = 0; i < 20000; ++i)
                filter.processSample (0.0f);

            // Riaccende lo stadio 2: se non e' stato azzerato, la vecchia energia rientra nel segnale.
            filter.setNumStages (2);

            float peak = 0.0f;
            for (int i = 0; i < 480; ++i)
                peak = juce::jmax (peak, std::abs (filter.processSample (0.0f)));

            expect (peak < 1.0e-4f, "picco dopo il riavvio dello stadio 2 " + juce::String (peak));
        }
    }
};

static StateVariableFilterTests stateVariableFilterTests;
