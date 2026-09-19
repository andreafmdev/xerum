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
/** Ampiezza in uscita dal filtro a una data frequenza, misurata a regime. */
float filterGainAt (dsp::StateVariableFilter& filter, float frequencyHz, double sampleRate)
{
    filter.reset();

    const auto increment = 2.0 * juce::MathConstants<double>::pi * (double) frequencyHz / sampleRate;
    const int settle = (int) (sampleRate * 0.2);
    const int measure = (int) (sampleRate * 0.1);

    double phase = 0.0;
    for (int i = 0; i < settle; ++i, phase += increment)
        filter.processSample ((float) std::sin (phase));

    double sumSquares = 0.0;
    for (int i = 0; i < measure; ++i, phase += increment)
    {
        const auto out = filter.processSample ((float) std::sin (phase));
        sumSquares += (double) out * (double) out;
    }

    // RMS in uscita diviso l'RMS di un seno di ampiezza 1 (cioe 1/sqrt(2)).
    return (float) (std::sqrt (sumSquares / measure) * std::sqrt (2.0));
}

float dbRatio (float gain, float reference) noexcept
{
    return 20.0f * std::log10 (gain / reference);
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
            // stadio, quindi il picco vale ~Q (limato di quel poco che fa la compensazione a
            // 1/32, ~1 dB): ~11, non ~144. Il secondo stadio, che a Q 12 resta Butterworth, il
            // picco non lo moltiplica: lo attenua dei suoi 3 dB al cutoff.
            constexpr float q = 12.0f;
            constexpr float expectedPeak = q;

            dsp::StateVariableFilter twelve, twentyFour;
            for (auto* f : { &twelve, &twentyFour })
            {
                f->prepare (sampleRate);
                f->setType (dsp::StateVariableFilter::Type::lowPass);
                f->setResonance (q);
                f->setCutoffHz (1000.0f);
            }
            twelve.setNumStages (1);
            twentyFour.setNumStages (2);

            const auto peak12 = filterGainAt (twelve, 1000.0f, sampleRate);
            const auto peak24 = filterGainAt (twentyFour, 1000.0f, sampleRate);

            expectWithinAbsoluteError (peak12, expectedPeak, expectedPeak * 0.15f,
                    "picco a 12 dB: " + juce::String (peak12) + ", atteso ~" + juce::String (expectedPeak));
            expect (peak24 < expectedPeak * 1.15f,
                    "il secondo stadio non deve moltiplicare il picco: 12 dB " + juce::String (peak12)
                        + ", 24 dB " + juce::String (peak24));

            // E la banda passante non deve pagare il conto della risonanza: alzare `res` fa
            // squillare il filtro, non abbassare il volume dello strumento. Prima, con la
            // compensazione a sqrt, qui si misurava 0.26 — cioe' -11.7 dB.
            const auto passband = filterGainAt (twelve, 100.0f, sampleRate);
            expect (passband > 0.85f && passband < 1.15f,
                    "a Q 12 la banda passante deve restare intorno a 1, misurata " + juce::String (passband));
        }

        beginTest ("a Q di Butterworth la compensazione non tocca niente");
        {
            // La compensazione deve sparire del tutto al minimo della corsa di `res`:
            // altrimenti il filtro attenuerebbe anche quando non risuona.
            dsp::StateVariableFilter filter;
            filter.prepare (sampleRate);
            filter.setType (dsp::StateVariableFilter::Type::lowPass);
            filter.setNumStages (1);
            filter.setResonance (dsp::StateVariableFilter::kButterworthQ);
            filter.setCutoffHz (1000.0f);

            expect (filterGainAt (filter, 100.0f, sampleRate) > 0.95f,
                    "in banda passante deve restare a guadagno unitario");
        }

        beginTest ("la banda passante non si abbassa quando sale la risonanza");
        {
            // Il difetto che questo test blocca: la compensazione d'ingresso valeva
            // sqrt(Qbutter/Q) e attenuava *tutto* il segnale, non solo il picco. A Q 24 lo
            // strumento perdeva ~10 dB e diventava magro. La risposta a un quarto del cutoff
            // (banda passante piena) deve restare la stessa su tutta la corsa di `res`.
            for (int stages : { 1, 2 })
            {
                for (float cutoff : { 200.0f, 1000.0f, 6000.0f })
                {
                    float reference = 0.0f;

                    for (float q : kResonanceSweep)
                    {
                        dsp::StateVariableFilter filter;
                        filter.prepare (sampleRate);
                        filter.setType (dsp::StateVariableFilter::Type::lowPass);
                        filter.setNumStages (stages);
                        filter.setResonance (q);
                        filter.setCutoffHz (cutoff);

                        const auto gain = filterGainAt (filter, cutoff * 0.25f, sampleRate);

                        if (q == kResonanceSweep[0])
                            reference = gain;

                        const auto delta = dbRatio (gain, reference);
                        expect (std::abs (delta) <= 1.5f,
                                "stadi " + juce::String (stages) + ", cutoff " + juce::String (cutoff)
                                    + ", Q " + juce::String (q) + ": banda passante " + juce::String (delta)
                                    + " dB rispetto a Q 0.707");
                    }
                }
            }
        }

        beginTest ("il picco risonante cresce con la risonanza, in modo monotono");
        {
            // L'altro lato della stessa moneta: se la compensazione fosse troppo forte il
            // picco resterebbe schiacciato e alzare `res` non si sentirebbe.
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

                    const auto peak = filterGainAt (filter, 1000.0f, sampleRate);

                    if (q == kResonanceSweep[0])
                        reference = peak;
                    else
                        expect (peak > previous * 1.05f,
                                "stadi " + juce::String (stages) + ": a Q " + juce::String (q)
                                    + " il picco (" + juce::String (peak) + ") non e' salito rispetto al Q precedente ("
                                    + juce::String (previous) + ")");

                    previous = peak;

                    if (q == 24.0f)
                        expect (dbRatio (peak, reference) >= 12.0f,
                                "stadi " + juce::String (stages) + ": a Q 24 il picco e' solo "
                                    + juce::String (dbRatio (peak, reference)) + " dB sopra il Butterworth");
                }
            }
        }

        beginTest ("risonanza massima e cutoff estremi: niente instabilita'");
        {
            for (double rate : { 44100.0, 96000.0 })
            {
                for (float cutoff : { 10.0f, (float) (rate * 0.49) })
                {
                    for (int stages : { 1, 2 })
                    {
                        dsp::StateVariableFilter filter;
                        filter.prepare (rate);
                        filter.setType (dsp::StateVariableFilter::Type::lowPass);
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
