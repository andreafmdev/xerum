#include "EngineTestHelpers.h"

#include "dsp/MipTable.h"
#include "dsp/Saturation.h"
#include "dsp/PlateReverb.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"
#include "engine/ParamCollect.h"
#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"
#include "parameters/PresetValue.h"
#include "state/StateToEngine.h"
#include "state/StateTree.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace
{
using harness::defaultParams;
using harness::measureFundamentalHz;
using harness::prepareEngine;
using harness::renderPeak;
using harness::renderRms;
using harness::denormaliseLinear;
using harness::ClipperProbe;
using harness::measureAtClipper;
using harness::rawFromNormalised;
using harness::PresetPatch;
using harness::patchFromPreset;
} // namespace

/**
 * `engine::saturate` misurata da sola, fuori dal motore.
 *
 * E' l'unica nonlinearita' della catena, quindi e' l'unica cosa che puo' produrre alias: ogni
 * altro stadio (filtro, inviluppo, pan, somma delle voci) e' lineare tempo-variante e non crea
 * frequenze che non c'erano. Misurarla attraverso SynthEngine significherebbe misurare anche
 * l'interpolazione della wavetable, che ha un pavimento suo a circa -73 dB: sessanta decibel
 * sopra a quello che c'e' da guardare qui. Da sola, invece, il pavimento della misura sta a
 * -185 dB e la curva si vede per quello che e'.
 */
struct SaturationCurveTests final : juce::UnitTest
{
    SaturationCurveTests() : juce::UnitTest ("saturate", "engine") {}

    /**
     * Blackman-Harris a 7 termini. La finestra **non** e' un dettaglio di comodo, e' la
     * differenza fra un test che misura e uno che non misura niente: con una finestra di Hann
     * la dispersione dei lobi laterali mette un pavimento a circa -44 dB, cioe' trenta decibel
     * sopra l'alias della curva vecchia e centodieci sopra quello della nuova — tutte le curve
     * risulterebbero identiche e perfette. Con questi sette coefficienti il primo lobo laterale
     * sta a -180 dB e quello che si legge e' il segnale, non la finestra.
     */
    static constexpr double kBlackmanHarris7[7] = {
        0.27105140069342, -0.43329793923448, 0.21812299954311, -0.06592544638803,
        0.01081174209837, -0.00077658482522, 0.00001388721735
    };

    /** 65536 punti a 48 kHz: 0.73 Hz per bin, e una potenza di due, che serve sotto. */
    static constexpr size_t kWindowSize = 65536;

    /**
     * Tabelle precalcolate per una DFT valutata solo dove serve. Il seno di prova si genera
     * **dalla stessa tabella** con cui poi lo si misura, indicizzata modulo N: cosi' il segnale
     * e' periodico di periodo N *esattamente*, senza l'errore di riduzione d'argomento che
     * std::sin accumula su k*i grandi. E' quell'errore, non la curva, a fissare il pavimento
     * di misura se lo si lascia entrare: settanta decibel di differenza.
     */
    struct Tables
    {
        std::vector<double> window, cosine, sine;

        Tables()
        {
            window.resize (kWindowSize);
            cosine.resize (kWindowSize);
            sine.resize (kWindowSize);

            for (size_t i = 0; i < kWindowSize; ++i)
            {
                const auto t = 2.0 * juce::MathConstants<double>::pi * (double) i / (double) kWindowSize;
                double w = 0.0;

                for (int m = 0; m < 7; ++m)
                    w += kBlackmanHarris7[(size_t) m] * std::cos ((double) m * t);

                window[i] = w;
                cosine[i] = std::cos (t);
                sine[i] = std::sin (t);
            }
        }

        /** Ampiezza al bin intero `bin`. L'indice del fasore avanza di `bin` e si avvolge:
            nessuna chiamata trigonometrica nel ciclo, nessun errore che cresca con i. */
        double magnitudeAtBin (const std::vector<double>& windowed, size_t bin) const
        {
            double re = 0.0, im = 0.0;
            size_t index = 0;

            for (size_t i = 0; i < kWindowSize; ++i)
            {
                re += windowed[i] * cosine[index];
                im -= windowed[i] * sine[index];
                index += bin;

                if (index >= kWindowSize)
                    index -= kWindowSize;
            }

            return std::sqrt (re * re + im * im) / (double) kWindowSize;
        }
    };

    struct AliasMeasurement
    {
        double aliasDb;   ///< il partial ripiegato piu' forte, rispetto alla fondamentale
        double floorDb;   ///< quanto misura la stessa DFT dove non c'e' niente
        int worstPartial; ///< l'ordine dell'armonica che ha prodotto quell'alias
    };

    /**
     * Rapporto alias/fondamentale di un seno di ampiezza 1 moltiplicato per `driveDb` e passato
     * per la curva, valutato direttamente a 48 kHz senza oversampling — cioe' esattamente come
     * gira nel motore.
     *
     * Il bin della fondamentale si sceglie **dispari**: con N potenza di due e k dispari,
     * gcd(k, N) = 1, quindi nessun partial di ordine m puo' ripiegarsi sopra il bin di un altro
     * partial di ordine m' (servirebbe (m +- m') multiplo di N). Senza quella precauzione la
     * misura confonderebbe un alias con un'armonica legittima e leggerebbe numeri a caso.
     *
     * `useDouble` sceglie fra la funzione di produzione (float, quella che gira davvero) e la
     * stessa formula in doppia precisione. Le due coincidono finche' l'alias sta sopra il
     * pavimento dei float, circa -185 dB; sotto, il float misura il proprio arrotondamento.
     */
    static AliasMeasurement measureAlias (const Tables& tables, int midiNote, double driveDb, bool useDouble)
    {
        constexpr double sampleRate = 48000.0;
        const auto fundamentalHz = 440.0 * std::pow (2.0, ((double) midiNote - 69.0) / 12.0);

        auto k = (size_t) juce::roundToInt (fundamentalHz * (double) kWindowSize / sampleRate);

        if ((k % 2) == 0)
            ++k;

        const auto drive = std::pow (10.0, driveDb / 20.0);

        std::vector<double> windowed (kWindowSize);

        for (size_t i = 0; i < kWindowSize; ++i)
        {
            const auto x = drive * tables.sine[(k * i) % kWindowSize];
            const auto y = useDouble ? dsp::saturateCurve (x)
                                     : (double) dsp::saturate ((float) x);
            windowed[i] = tables.window[i] * y;
        }

        const auto fundamental = tables.magnitudeAtBin (windowed, k);

        // I partial dispari (la curva e' dispari: quelli pari non esistono) fino al 201esimo.
        // Quelli sotto Nyquist sono armoniche vere e non contano; quelli sopra si ripiegano, ed
        // e' la loro somma a essere l'inarmonicita' che si sente.
        double worst = 0.0;
        int worstPartial = 0;

        for (int m = 3; m <= 201; m += 2)
        {
            const auto position = (size_t) m * k;

            if (position < kWindowSize / 2)
                continue;

            auto bin = position % kWindowSize;

            if (bin > kWindowSize / 2)
                bin = kWindowSize - bin;

            if (bin == k || bin == 0)
                continue;

            const auto amplitude = tables.magnitudeAtBin (windowed, bin);

            if (amplitude > worst)
            {
                worst = amplitude;
                worstPartial = m;
            }
        }

        // Il pavimento: bin **pari**, dove per costruzione non puo' cadere nessun partial (k e'
        // dispari e m e' dispari, quindi m*k e' dispari, e il ripiegamento di un indice dispari
        // modulo una potenza di due resta dispari). Se questo numero non stesse molto sotto la
        // soglia che il test pretende, il test non starebbe misurando la curva ma se stesso.
        double floor = 0.0;

        for (const size_t bin : { kWindowSize / 4, kWindowSize / 4 + 2, kWindowSize / 2 - 4,
                                  (size_t) 1000, (size_t) 20002 })
            floor = juce::jmax (floor, tables.magnitudeAtBin (windowed, bin));

        AliasMeasurement result;
        result.aliasDb = 20.0 * std::log10 (worst / fundamental);
        result.floorDb = 20.0 * std::log10 (floor / fundamental);
        result.worstPartial = worstPartial;
        return result;
    }

    void runTest() override
    {
        beginTest ("la curva e' dispari, monotona e non esce mai da +-1");
        {
            // f(0) = 0 esatto, non "vicino a zero": e' cio' che garantisce che il silenzio
            // resti silenzio quando drive e' acceso, e che un DC offset non nasca dal nulla.
            expectEquals (dsp::saturate (0.0f), 0.0f);

            // Dispari, e come uguaglianza **esatta** in float: la formula nega solo il
            // numeratore (x2 e il denominatore non cambiano) e IEEE garantisce (-a)/b == -(a/b).
            // Se un domani qualcuno introducesse un termine pari — una polarizzazione, un
            // arrotondamento asimmetrico — la seconda armonica comparirebbe e questo lo prende.
            for (float x = 0.0f; x <= 8.0f; x += 0.0009765625f) // passo 2^-10: esatto in binario
                expectEquals (dsp::saturate (-x), -dsp::saturate (x),
                              "simmetria a x = " + juce::String (x));

            // Monotona crescente su tutta la corsa utile e oltre il ginocchio: una curva che
            // ripiegasse (come farebbe la cubica senza clamp sopra 1.5) produrrebbe un
            // wavefolder, non un saturatore. In esatta, la derivata e'
            // f'(x) = 9(x^2-9)^2 / (27+9x^2)^2, un quadrato: non negativa ovunque, quindi la
            // monotonia e' una proprieta' della formula e qui si chiede **stretta**.
            {
                double previous = dsp::saturateCurve (-8.0);

                for (double x = -8.0; x <= 8.0; x += 0.0009765625) // passo 2^-10: esatto in binario
                {
                    const auto y = dsp::saturateCurve (x);
                    expect (y >= previous, "non monotona in doppia precisione a x = " + juce::String (x));
                    previous = y;
                }
            }

            // In float la stessa cosa, ma con un ULP di tolleranza, ed e' una concessione
            // all'aritmetica, non alla curva: vicino al ginocchio la pendenza vera scende sotto
            // 1e-4, quindi fra due punti adiacenti la funzione cresce di meno di quanto valga
            // l'ultimo bit del risultato, e l'arrotondamento della divisione puo' far scendere il
            // valore di un ULP. Misurato: esattamente 5.96e-8, cioe' un ULP a 1.0, a x = -2.986.
            // Quattro ULP di soglia lasciano passare quello e fermano qualunque ripiegamento
            // vero, che sarebbe milioni di volte piu' grande.
            {
                constexpr float tolerance = 4.0f * 5.9604645e-8f;
                float previous = dsp::saturate (-8.0f);
                float worstDrop = 0.0f;

                for (float x = -8.0f; x <= 8.0f; x += 0.0009765625f)
                {
                    const auto y = dsp::saturate (x);
                    worstDrop = juce::jmax (worstDrop, previous - y);
                    expect (y >= previous - tolerance, "non monotona a x = " + juce::String (x));
                    previous = y;
                }

                logMessage ("monotonia in float: calo massimo fra due punti adiacenti "
                            + juce::String (worstDrop, 12) + " (un ULP vale 5.96e-8)");
            }

            // Limitata, compresi i casi che un mod matrix impazzito o un denormale possono
            // davvero produrre. L'infinito e' il caso che distingue la forma a rami
            // (if x > 3 -> 1) da un clamp applicato al risultato: quest'ultimo calcolerebbe
            // inf/inf = NaN e lo lascerebbe passare, perche' ogni confronto con NaN e' falso.
            for (const float x : { 0.5f, 1.0f, 2.0f, 3.0f, 10.0f, 1.0e9f, 1.0e30f,
                                   std::numeric_limits<float>::infinity() })
            {
                expect (std::abs (dsp::saturate (x)) <= 1.0f, "|f(x)| > 1 a x = " + juce::String (x));
                expect (std::abs (dsp::saturate (-x)) <= 1.0f, "|f(-x)| > 1 a x = " + juce::String (x));
                expect (std::isfinite (dsp::saturate (x)), "f(x) non finita a x = " + juce::String (x));
                expect (std::isfinite (dsp::saturate (-x)), "f(-x) non finita a x = " + juce::String (x));
            }
        }

        beginTest ("il raccordo con il tratto piatto e' continuo in derivata prima e seconda");
        {
            // Il difetto della curva precedente, e la ragione del cambio. `clamp(x, +-1.5)` +
            // cubica raccorda con derivata prima nulla (C1) ma lascia la derivata seconda che
            // salta da -1.333 a 0: uno spigolo che la serie di Fourier paga con armoniche che
            // decadono come 1/n^3 invece che esponenzialmente. Il Pade di tanh ha invece una
            // radice **tripla** del denominatore in 3 (x^3-9x^2+27x-27 = (x-3)^3), quindi
            // f(3)=1, f'(3)=0 e f''(3)=0 tutte insieme.
            //
            // Si misura in doppia precisione perche' una differenza seconda divisa per h^2 con
            // h = 1e-3 amplifica l'errore di un milione di volte: in float il rumore di
            // arrotondamento coprirebbe il salto che si vuole vedere.
            constexpr double h = 1.0e-3;

            double maxFirstJump = 0.0, maxSecondJump = 0.0;
            double previousFirst = 0.0, previousSecond = 0.0;
            bool first = true;

            for (double x = -6.0; x <= 6.0; x += h)
            {
                const auto f = [] (double v) { return dsp::saturateCurve (v); };
                const auto d1 = (f (x + h) - f (x - h)) / (2.0 * h);
                const auto d2 = (f (x + h) - 2.0 * f (x) + f (x - h)) / (h * h);

                if (! first)
                {
                    maxFirstJump = juce::jmax (maxFirstJump, std::abs (d1 - previousFirst));
                    maxSecondJump = juce::jmax (maxSecondJump, std::abs (d2 - previousSecond));
                }

                previousFirst = d1;
                previousSecond = d2;
                first = false;
            }

            // Le soglie non sono arrotondamenti prudenti, sono due popolazioni separate da tre
            // ordini di grandezza. Misurato: f' salta al massimo di 7.5e-4 fra due punti
            // adiacenti (con la curva vecchia 1.3e-3, anche lei continua), f'' di 1.8e-3 —
            // contro i 6.7e-1 della curva vecchia, che e' lo spigolo. Una curva che tornasse
            // C1-e-basta finirebbe di nuovo a ~1e-1 e questo test la fermerebbe.
            expect (maxFirstJump < 5.0e-3,
                    "la derivata prima salta di " + juce::String (maxFirstJump, 6));
            expect (maxSecondJump < 2.0e-2,
                    "la derivata seconda salta di " + juce::String (maxSecondJump, 6)
                        + ": il raccordo e' solo C1");

            logMessage ("salto massimo fra punti adiacenti: f' " + juce::String (maxFirstJump, 6)
                        + ", f'' " + juce::String (maxSecondJump, 6));
        }

        beginTest ("a MIDI 84 e drive 6 dB l'alias resta sotto -120 dB dalla fondamentale");
        {
            const Tables tables;

            struct Case { int note; double driveDb; double limitDb; };

            // I limiti sono il criterio, non la misura: stanno venti-trenta decibel sopra il
            // numero misurato con questa curva e molto sotto quello della curva precedente,
            // che e' riportato accanto. Un test che chiedesse esattamente il valore misurato
            // fallirebbe al primo cambio di compilatore; uno che chiedesse -40 dB sarebbe
            // passato anche prima.
            const Case cases[] = {
                { 60, 6.0, -160.0 },  // prima: -111.7
                { 84, 4.8, -140.0 },  // prima:  -85.2
                { 84, 6.0, -120.0 },  // prima:  -75.5   <- il criterio del brief
                { 84, 12.0, -78.0 },  // prima:  -57.3
            };

            // A MIDI 60 il limite non e' -160 perche' la curva non sappia fare meglio: in doppia
            // precisione la stessa misura da' -334 dB. E' che il percorso di produzione e' in
            // float, e a quel punto quello che la DFT legge non e' piu' l'alias della curva ma
            // l'arrotondamento dei float, che sta attorno a -180 dB e non scende oltre. Il
            // numero double, che il log riporta accanto, e' la proprieta' della curva; quello
            // float e' la proprieta' del motore. Il limite si riferisce al secondo.

            for (const auto& c : cases)
            {
                const auto measured = measureAlias (tables, c.note, c.driveDb, false);
                const auto exact = measureAlias (tables, c.note, c.driveDb, true);

                // Prima di credere alla misura: la stessa DFT, letta dove non c'e' niente, deve
                // stare a -170 dB o meno. E' il controllo che con una finestra di Hann
                // (pavimento a -44 dB) fallirebbe subito, invece di lasciar passare un test che
                // non misura piu' niente.
                expect (measured.floorDb < -170.0,
                        "MIDI " + juce::String (c.note) + ": il pavimento della misura e' a "
                            + juce::String (measured.floorDb, 1)
                            + " dB invece dei -185 attesi; la misura non prova piu' niente");

                expect (measured.aliasDb < c.limitDb,
                        "MIDI " + juce::String (c.note) + " drive " + juce::String (c.driveDb, 1)
                            + " dB: alias a " + juce::String (measured.aliasDb, 1)
                            + " dB, sopra il limite di " + juce::String (c.limitDb, 1));

                logMessage ("MIDI " + juce::String (c.note) + " drive " + juce::String (c.driveDb, 1)
                            + " dB -> alias " + juce::String (measured.aliasDb, 1) + " dB (float), "
                            + juce::String (exact.aliasDb, 1) + " dB (double), partial "
                            + juce::String (measured.worstPartial) + ", pavimento "
                            + juce::String (measured.floorDb, 1) + " dB");
            }

            // Il contrappeso onesto: sopra MIDI ~100 la fondamentale e' a 4 kHz, sotto Nyquist
            // ci stanno cinque armoniche e **qualunque** saturazione alias. Non e' una
            // regressione da correggere, e' il limite fisico della valutazione diretta; e
            // pretendere qui una soglia bassa vorrebbe dire pretendere l'oversampling, che e'
            // una decisione diversa e non e' stata presa. Il test lo fissa come fatto noto,
            // cosi' nessuno lo riscopre come se fosse un bug.
            const auto high = measureAlias (tables, 108, 6.0, false);
            expect (high.aliasDb > -50.0 && high.aliasDb < -35.0,
                    "MIDI 108: alias a " + juce::String (high.aliasDb, 1)
                        + " dB, atteso circa -43 (prima: -43.4, le curve pareggiano)");
            logMessage ("MIDI 108 drive 6.0 dB -> alias " + juce::String (high.aliasDb, 1)
                        + " dB: qui nessuna curva puo' fare meglio senza oversampling");
        }
    }
};

static SaturationCurveTests saturationCurveTests;
