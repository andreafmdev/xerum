#include "dsp/StereoDelay.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <vector>

/** Il delay stereo: eco, feedback, damping, ping-pong, coda. Solo bagnato in uscita. */
struct DelayTests final : public juce::UnitTest
{
    DelayTests() : juce::UnitTest ("StereoDelay", "dsp") {}

    static constexpr double kSr = 48000.0;

    struct Out { std::vector<float> l, r; };

    /** Un impulso a sinistra (e a destra se `stereoImpulse`) al campione 0, poi `n` campioni di bagnato. */
    static Out impulse (dsp::StereoDelay& d, int n, bool stereoImpulse = false)
    {
        Out o { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
        o.l[0] = 1.0f;
        if (stereoImpulse) o.r[0] = 1.0f;
        for (int done = 0; done < n; done += 64)
            d.process (o.l.data() + done, o.r.data() + done, std::min (64, n - done));
        return o;
    }

    static double sumAbs (const std::vector<float>& x, int from, int to)
    {
        double s = 0.0;
        for (int i = std::max (0, from); i < std::min ((int) x.size(), to); ++i) s += std::abs ((double) x[(size_t) i]);
        return s;
    }

    static int firstNonZero (const std::vector<float>& x, int from = 0)
    {
        for (int i = from; i < (int) x.size(); ++i) if (std::abs (x[(size_t) i]) > 1.0e-6f) return i;
        return -1;
    }

    /** Centroide spettrale (in bin) di una finestra, per confrontare la brillantezza di due echi. */
    static double centroid (const std::vector<float>& x, int from, int len)
    {
        constexpr int order = 10; // 1024
        std::vector<float> data (2048, 0.0f);
        for (int i = 0; i < std::min (len, 1024); ++i) data[(size_t) i] = x[(size_t) (from + i)];
        juce::dsp::FFT fft (order);
        fft.performFrequencyOnlyForwardTransform (data.data(), true);
        double num = 0.0, den = 0.0;
        for (int i = 1; i <= 512; ++i) { const auto e = (double) data[(size_t) i] * data[(size_t) i]; num += i * e; den += e; }
        return den > 0.0 ? num / den : 0.0;
    }

    static dsp::StereoDelay make (float time, float fb, float damp, bool pp)
    {
        dsp::StereoDelay d;
        d.prepare (kSr, 64);
        d.setParameters (time, fb, damp, pp);
        return d;
    }

    void runTest() override
    {
        beginTest ("un impulso torna dopo esattamente il tempo impostato, ad ampiezza 1");
        {
            auto d = make (0.1f, 0.0f, 0.0f, false);
            const auto o = impulse (d, 6000);
            expectEquals (firstNonZero (o.l, 1), 4800);
            expectWithinAbsoluteError (o.l[4800], 1.0f, 1.0e-4f);
            expectEquals (firstNonZero (o.r, 0), -1, "senza ingresso a destra, niente a destra");
        }

        beginTest ("il secondo eco vale feedback volte il primo (damp a 20 kHz)");
        {
            auto d = make (0.05f, 0.5f, 0.0f, false);   // 2400 campioni; 0.5 -> 45 % di feedback
            const auto o = impulse (d, 8000);
            const auto first = sumAbs (o.l, 2390, 2440);
            const auto second = sumAbs (o.l, 4790, 4840);
            expectWithinAbsoluteError ((float) (second / juce::jmax (1.0e-9, first)), 0.5f * dsp::StereoDelay::kMaxFeedback, 0.02f);
        }

        beginTest ("il damping scurisce gli echi successivi");
        {
            auto bright = make (0.05f, 0.5f, 0.0f, false);
            auto dark = make (0.05f, 0.5f, 1.0f, false);
            const auto ob = impulse (bright, 8000);
            const auto od = impulse (dark, 8000);
            expect (centroid (od.l, 4790, 512) < 0.5 * centroid (ob.l, 4790, 512), "con damp a fondo corsa il secondo eco deve essere molto piu' scuro");
        }

        beginTest ("ping-pong: gli echi alternano sinistra e destra");
        {
            auto d = make (0.05f, 0.5f, 0.0f, true);
            const auto o = impulse (d, 8000, true);
            expect (sumAbs (o.l, 2390, 2440) > 0.9, "primo eco a sinistra");
            expect (sumAbs (o.r, 2390, 2440) < 1.0e-3, "niente a destra al primo eco");
            expect (sumAbs (o.r, 4790, 4840) > 0.3, "secondo eco a destra");
            expect (sumAbs (o.l, 4790, 4840) < 1.0e-3, "niente a sinistra al secondo eco");
        }

        beginTest ("cambiare il tempo scivola senza gradini");
        {
            auto d = make (0.1f, 0.5f, 0.0f, false);
            std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
            // Una sinusoide continua in ingresso, e a meta' il tempo salta da 100 a 200 ms.
            for (int i = 0; i < 48000; ++i) l[(size_t) i] = 0.5f * std::sin (0.02f * (float) i);
            for (int done = 0; done < 48000; done += 64)
            {
                if (done == 24000) d.setParameters (0.2f, 0.5f, 0.0f, false);
                d.process (l.data() + done, r.data() + done, 64);
            }
            float maxStep = 0.0f;
            for (int i = 24001; i < 48000; ++i) maxStep = std::max (maxStep, std::abs (l[(size_t) i] - l[(size_t) i - 1]));
            expect (maxStep < 0.05f, "passo massimo " + juce::String (maxStep) + ": il salto di tempo ha prodotto un gradino");
        }

        beginTest ("tailSeconds: time con feedback 0, 12 volte time con 0.5, limitata a 60 s");
        {
            expectWithinAbsoluteError (dsp::StereoDelay::tailSeconds (0.3f, 0.0f), 0.3f, 1.0e-6f);
            // feedback01 0.5 -> 0.45 di guadagno: ln(1e-4)/ln(0.45) = 11.53 -> 12 ripetizioni
            expectWithinAbsoluteError (dsp::StereoDelay::tailSeconds (0.3f, 0.5f), 12.0f * 0.3f, 1.0e-4f);
            expectWithinAbsoluteError (dsp::StereoDelay::tailSeconds (2.0f, 1.0f), dsp::StereoDelay::kMaxTailSeconds, 1.0e-6f);
        }

        beginTest ("reset azzera la linea e il feedback");
        {
            auto d = make (0.05f, 0.9f, 0.0f, false);
            impulse (d, 3000);
            d.reset();
            std::vector<float> l (6000, 0.0f), r (6000, 0.0f);
            d.process (l.data(), r.data(), 6000);
            expectEquals (firstNonZero (l), -1);
        }
    }
};

static DelayTests delayTests;
