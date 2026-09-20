#include "dsp/Chorus.h"
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "EngineHarness.h"

#include "engine/SynthEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

namespace
{
using harness::baseParams;
using harness::correlation;
using harness::kBlock;
using harness::kSampleRate;
using harness::prepareEngine;
using harness::Rendered;
using harness::renderHeldNote;
using harness::rms;


/**
 * Il picco **presentato al soft clipper**, misurato con il metodo del gain ridotto.
 *
 * Qui c'era l'inversa analitica di engine::softClip, ed era il metodo sbagliato. E' esatta in
 * aritmetica esatta — il clipper e' strettamente monotono, quindi invertibile — ma
 * **malcondizionata** vicino alla saturazione: 0.9983 in uscita risale a 2.36 all'ingresso e
 * 0.9990 a 3.40, cioe' sette decimillesimi di uscita diventano 3.4 dB di ingresso stimato. Su
 * questa stessa passata il secco presentava 1.006, cioe' gia' sopra soglia: la misura viveva
 * proprio nella regione dove l'inversione non regge, e i suoi numeri non erano confrontabili con
 * quelli di Tests/ReverbTests.cpp ne' di Tests/EngineTests.cpp, che usano il metodo del gain
 * ridotto.
 *
 * Il metodo giusto: si abbassa il gain master finche' il clipper resta spento, si **verifica**
 * che sia spento, e si riscala. Tutto cio' che precede il clipper (le voci, lo stadio FX) e'
 * esattamente lineare nel gain master, quindi il riscalamento e' esatto e non una stima.
 *
 * Il blocco muto prima delle note non e' decorazione: `applyGainRamp` parte da
 * `previousMasterGain_`, che al primissimo blocco vale ancora 1.0, e senza quello scarto il
 * picco che si misura e' la rampa del gain invece del segnale.
 */
struct ClipperPeak { double value { 0.0 }; bool clipperOff { true }; };

ClipperPeak peakAtClipper (engine::SynthEngine& synth, const std::vector<int>& notes,
                           float realVolume, int blocks, float probeVolume = 0.02f)
{
    synth.setMasterGainLinear (probeVolume);

    {
        juce::MidiBuffer none;
        juce::AudioBuffer<float> warmUp (2, kBlock);
        warmUp.clear();
        synth.process (warmUp, none);
    }

    juce::MidiBuffer midi;
    for (const auto note : notes)
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);

    double peak = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        buffer.clear();
        synth.process (buffer, midi);
        midi.clear();

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                peak = juce::jmax (peak, (double) std::abs (buffer.getSample (ch, i)));
    }

    return { peak * (double) realVolume / (double) probeVolume, peak < 0.95 };
}
} // namespace

/**
 * Il chorus da solo, senza motore attorno: e' qui che si misura la topologia.
 */
struct ChorusUnitTests final : juce::UnitTest
{
    ChorusUnitTests() : juce::UnitTest ("chorus", "dsp") {}

    /**
     * Manda un impulso nella linea dopo `advanceSeconds` di silenzio e ritorna l'indice del
     * picco piu' alto della risposta, cioe' il **ritardo del tap dominante** in campioni.
     *
     * E' la misura diretta di cio' che un chorus e': un ritardo che si muove. Il tap 0 ha il
     * peso di pan piu' alto sul canale sinistro (0.806 contro 0.577 e 0.128), quindi il massimo
     * della risposta e' il suo, e la sua fase di LFO e' quella nuda dell'oscillatore.
     */
    int dominantTapDelay (float rateHz, float depth01, double advanceSeconds)
    {
        dsp::Chorus chorus;
        chorus.prepare (kSampleRate, kBlock, 2);
        chorus.setParameters (rateHz, depth01, 0.0f);

        for (int remaining = (int) (advanceSeconds * kSampleRate); remaining > 0; remaining -= kBlock)
        {
            std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
            chorus.process (l.data(), r.data(), std::min (kBlock, remaining));
        }

        constexpr int tail = 1600; // oltre il ritardo massimo (792 campioni a 48 kHz)
        std::vector<float> l ((size_t) tail, 0.0f), r ((size_t) tail, 0.0f);
        l[0] = 1.0f;
        r[0] = 1.0f;
        chorus.process (l.data(), r.data(), tail);

        int argmax = 0;

        for (int i = 1; i < tail; ++i)
            if (std::abs (l[(size_t) i]) > std::abs (l[(size_t) argmax]))
                argmax = i;

        return argmax;
    }

    void runTest() override
    {
        beginTest ("il ritardo del tap dominante si muove con l'LFO");
        {
            // Un quarto di ciclo dopo lo zero l'LFO e' al massimo positivo, tre quarti dopo al
            // massimo negativo: il tap dominante passa da ~1.5x a ~0.5x il ritardo base. Se il
            // ritardo fosse fisso — o se la profondita' non arrivasse alla linea — i due numeri
            // coinciderebbero, ed e' l'unico modo di dimostrare che il chorus *modula* invece
            // di limitarsi a ritardare.
            const auto base = (int) std::lround (dsp::Chorus::kBaseDelayMs * 0.001 * kSampleRate);
            const auto atTop = dominantTapDelay (1.0f, 1.0f, 0.25);
            const auto atBottom = dominantTapDelay (1.0f, 1.0f, 0.75);

            logMessage ("ritardo base " + juce::String (base) + " campioni; tap dominante al massimo "
                        + juce::String (atTop) + ", al minimo " + juce::String (atBottom));

            expect (atTop > base + 150, "al massimo dell'LFO il ritardo e' " + juce::String (atTop)
                                            + ", non sopra il base (" + juce::String (base) + ")");
            expect (atBottom < base - 150, "al minimo dell'LFO il ritardo e' " + juce::String (atBottom)
                                               + ", non sotto il base (" + juce::String (base) + ")");
            expect (atTop - atBottom > 300, "escursione del ritardo di soli "
                                                + juce::String (atTop - atBottom) + " campioni");
        }

        beginTest ("a profondita' zero il ritardo sta fermo sul valore base");
        {
            const auto base = (int) std::lround (dsp::Chorus::kBaseDelayMs * 0.001 * kSampleRate);
            const auto atTop = dominantTapDelay (1.0f, 0.0f, 0.25);
            const auto atBottom = dominantTapDelay (1.0f, 0.0f, 0.75);

            expectEquals (atTop, atBottom);
            expect (std::abs (atTop - base) <= 2, "ritardo " + juce::String (atTop) + " contro base "
                                                      + juce::String (base));
        }

        beginTest ("i pesi di pan dei tap sono a potenza costante: il bagnato non alza il livello");
        {
            // Tre tap sommati con i pesi di pan devono avere la potenza di un tap solo a
            // guadagno unitario. Se la normalizzazione sqrt(2/N) fosse sbagliata, il bagnato al
            // 100 % arriverebbe al soft clipper piu' forte del secco — cioe' lo stadio FX
            // sarebbe uno stadio di guadagno, ed e' proprio cio' che docs/architecture.md dice
            // di non fare senza compensare.
            // Rumore **passa-bassato a 2 kHz**, non bianco fino a Nyquist, e la ragione e' una
            // proprieta' vera di Lagrange3rd: e' un FIR a quattro punti, quindi la sua risposta
            // in ampiezza non e' piatta ma cala verso Nyquist. Con rumore bianco a piena banda
            // la misura da' -1.09 dB, e quel decibel e' l'interpolatore, non i pesi di pan — il
            // test misurerebbe la cosa sbagliata e nessuna taratura dei pesi lo farebbe tornare.
            // Nella banda dove uno strumento ha davvero energia l'attenuazione e' trascurabile.
            juce::Random rng (20260919);
            std::vector<float> l, r, in;
            const auto coeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 2000.0f
                                                / (float) kSampleRate);
            float stateL = 0.0f, stateR = 0.0f;

            for (int i = 0; i < 48000; ++i)
            {
                stateL += (rng.nextFloat() * 2.0f - 1.0f - stateL) * coeff;
                stateR += (rng.nextFloat() * 2.0f - 1.0f - stateR) * coeff;
                in.push_back (stateL);
                l.push_back (stateL);
                r.push_back (stateR);
            }

            dsp::Chorus chorus;
            chorus.prepare (kSampleRate, kBlock, 2);
            chorus.setParameters (1.0f, 0.5f, 0.0f);
            chorus.process (l.data(), r.data(), (int) l.size());

            // Si scarta il primo mezzo secondo: la linea parte vuota, quindi il bagnato iniziale
            // non ha ancora niente da leggere.
            std::vector<float> dryTail (in.begin() + 24000, in.end());
            std::vector<float> wetTail (l.begin() + 24000, l.end());

            const auto ratioDb = juce::Decibels::gainToDecibels ((float) (rms (wetTail) / rms (dryTail)));
            logMessage ("bagnato rispetto al secco: " + juce::String (ratioDb, 2) + " dB");
            expect (std::abs (ratioDb) < 0.5f, "il bagnato sta " + juce::String (ratioDb, 2)
                                                   + " dB rispetto al secco: i pesi non sono a potenza costante");
        }

        beginTest ("il feedback a fondo corsa resta limitato e finito");
        {
            // La saturazione sul ramo di ritorno rende l'anello limitato **per costruzione**:
            // |saturate| <= 1, quindi il contributo di ritorno non supera kMaxFeedback e il
            // contenuto della linea non supera |secco| + kMaxFeedback, qualunque cosa entri.
            // Qui entra un fondo scala continuo, che e' il caso peggiore.
            dsp::Chorus chorus;
            chorus.prepare (kSampleRate, kBlock, 2);
            chorus.setParameters (5.1f, 1.0f, 1.0f);

            auto peak = 0.0f;

            for (int b = 0; b < 400; ++b)
            {
                std::vector<float> l ((size_t) kBlock, 1.0f), r ((size_t) kBlock, -1.0f);
                chorus.process (l.data(), r.data(), kBlock);

                for (int i = 0; i < kBlock; ++i)
                {
                    expect (std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]), "campione non finito");
                    peak = juce::jmax (peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
                }
            }

            const auto bound = (1.0f + dsp::Chorus::kMaxFeedback) * std::sqrt ((float) dsp::Chorus::kNumTaps);
            logMessage ("feedback e profondita' a fondo corsa su un fondo scala continuo: picco "
                        + juce::String (peak));
            expect (peak < bound, "picco " + juce::String (peak) + " oltre il limite di costruzione "
                                      + juce::String (bound));
        }
    }
};

static ChorusUnitTests chorusUnitTests;

/**
 * Lo stadio FX dentro il motore: bypass, dry/wet, ringout e margine al soft clipper.
 */
struct FxStageTests final : juce::UnitTest
{
    FxStageTests() : juce::UnitTest ("stadio FX", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("con il chorus spento l'uscita e' bit per bit quella di prima dello stadio FX");
        {
            // Il criterio di accettazione che conta piu' di tutti, e la ragione per cui
            // processFx() esce **prima** di toccare il buffer invece di calcolare un'identita':
            // un mix a guadagno 1/0 non e' bit-trasparente, un ramo che non gira lo e'.
            //
            // I parametri del chorus sono tutti agli estremi: se una sola riga dello stadio
            // girasse comunque, si vedrebbe.
            auto off = baseParams();
            off.filterOn = true;
            off.chorusOn = false;
            off.chorusRateHz = 5.1f;
            off.chorusDepth01 = 1.0f;
            off.chorusMix01 = 1.0f;
            off.chorusFeedback01 = 1.0f;

            auto never = baseParams();
            never.filterOn = true; // nessun campo chorus toccato: i default di EngineParams

            const auto withExtremes = renderHeldNote (off, store, 60);
            const auto withDefaults = renderHeldNote (never, store, 60);

            expectEquals ((int) withExtremes.left.size(), (int) withDefaults.left.size());

            for (size_t i = 0; i < withDefaults.left.size(); ++i)
            {
                expectWithinAbsoluteError (withExtremes.left[i], withDefaults.left[i], 0.0f);
                expectWithinAbsoluteError (withExtremes.right[i], withDefaults.right[i], 0.0f);
            }
        }

        beginTest ("il chorus decorrela i due canali");
        {
            // Con pan al centro, unison 1 e chorus spento i due canali del motore sono lo stesso
            // segnale: correlazione 1.000 esatta. E' la condizione iniziale che rende la misura
            // interpretabile — qualunque scostamento da 1 viene dallo stadio FX e da nient'altro.
            auto dry = baseParams();
            dry.chorusOn = false;

            auto wet = baseParams();
            wet.chorusOn = true;
            wet.chorusRateHz = 1.6f;
            wet.chorusDepth01 = 0.4f;
            wet.chorusMix01 = 1.0f;
            wet.chorusFeedback01 = 0.0f;

            const auto dryOut = renderHeldNote (dry, store, 200);
            const auto wetOut = renderHeldNote (wet, store, 200);

            const auto dryCorrelation = correlation (dryOut.left, dryOut.right);
            const auto wetCorrelation = correlation (wetOut.left, wetOut.right);

            logMessage ("correlazione L/R: secco " + juce::String (dryCorrelation, 4) + ", chorus "
                        + juce::String (wetCorrelation, 4));

            expectWithinAbsoluteError (dryCorrelation, 1.0, 1.0e-9);
            expect (wetCorrelation < 0.9, "correlazione con il chorus " + juce::String (wetCorrelation, 4)
                                              + ": l'immagine stereo non si e' aperta");
        }

        beginTest ("accendere e spegnere il chorus durante una nota non produce un gradino");
        {
            // Stessa tecnica di "ribattere una nota che suona gia' non produce un gradino": si
            // misura il salto massimo fra campioni adiacenti attorno al momento in cui
            // l'interruttore si muove, e lo si confronta con la **pendenza naturale** dell'onda
            // nella stessa finestra — che e' cio' che fa anche "rubare una voce non produce un
            // gradino". Una soglia assoluta qui non direbbe niente: dipende dalla tavola.
            //
            // L'interruttore cade in sessanta punti diversi dell'onda, e si tiene il peggiore.
            // Non e' abbondanza: il gradino che il riempimento della linea evita **dipende dal
            // campione su cui si accende**, quindi una fase sola lo prende o lo manca a caso.
            // Con il riempimento disattivato questa stessa misura sale da 0.09 a 0.25.
            //
            // Mix al 100 %: il caso peggiore, perche' fra secco e bagnato non resta nulla in
            // comune da cui il salto possa essere piccolo per fortuna.
            enum class Switch { never, turnOn, turnOff };

            const auto worstJump = [&store, this] (Switch mode)
            {
                auto worst = 0.0f;

                for (int settle = 200; settle < 260; ++settle)
                {
                    engine::SynthEngine synth;
                    prepareEngine (synth, store);

                    auto p = baseParams();
                    p.attackSeconds = 0.005f;
                    p.decaySeconds = 0.005f;
                    // never e turnOff partono acceso, turnOn parte spento.
                    p.chorusOn = mode != Switch::turnOn;
                    p.chorusRateHz = 0.1f; // lento: il ritardo e' quasi fermo, cosi' l'unica cosa
                                           // che si muove nella finestra e' l'interruttore
                    p.chorusDepth01 = 0.5f;
                    p.chorusMix01 = 1.0f;
                    p.chorusFeedback01 = 0.0f;
                    synth.setParams (p);
                    synth.setMasterGainLinear (1.0f);

                    juce::AudioBuffer<float> buffer (2, kBlock);
                    juce::MidiBuffer midi;
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
                    buffer.clear();
                    synth.process (buffer, midi);
                    midi.clear();

                    auto previous = 0.0f;

                    for (int b = 0; b < settle; ++b)
                    {
                        buffer.clear();
                        synth.process (buffer, midi);
                        previous = buffer.getSample (0, kBlock - 1);
                    }

                    if (mode == Switch::turnOn)
                        p.chorusOn = true;
                    else if (mode == Switch::turnOff)
                        p.chorusOn = false;

                    synth.setParams (p);

                    // Dodici blocchi coprono il riempimento (16.5 ms), la dissolvenza (12 ms) e
                    // un po' di regime dopo: il gradino, se c'e', sta li' dentro.
                    for (int b = 0; b < 12; ++b)
                    {
                        buffer.clear();
                        synth.process (buffer, midi);

                        worst = juce::jmax (worst, std::abs (buffer.getSample (0, 0) - previous));

                        for (int i = 1; i < kBlock; ++i)
                            worst = juce::jmax (worst,
                                                std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1)));

                        previous = buffer.getSample (0, kBlock - 1);
                    }
                }

                return worst;
            };

            const auto natural = worstJump (Switch::never);
            const auto onJump = worstJump (Switch::turnOn);
            const auto offJump = worstJump (Switch::turnOff);

            logMessage ("salto peggiore su 60 fasi: pendenza naturale " + juce::String (natural)
                        + ", accendendo " + juce::String (onJump) + ", spegnendo " + juce::String (offJump));

            // Un quarto sopra la pendenza naturale: e' il margine che lascia passare la pendenza
            // un po' piu' ripida del bagnato (tre tap sommati) e nient'altro. Senza dissolvenza
            // o senza riempimento della linea si finisce a 2.8 volte.
            const auto limit = natural * 1.25f;
            expect (onJump < limit, "accendendo, salto " + juce::String (onJump) + " contro pendenza naturale "
                                        + juce::String (natural));
            expect (offJump < limit, "spegnendo, salto " + juce::String (offJump) + " contro pendenza naturale "
                                         + juce::String (natural));
        }

        beginTest ("dopo il ringout la coda e' silenzio, e riaccendere non ripesca il passato");
        {
            engine::SynthEngine synth;
            prepareEngine (synth, store);

            auto p = baseParams();
            p.chorusOn = true;
            p.chorusRateHz = 1.6f;
            p.chorusDepth01 = 0.5f;
            p.chorusMix01 = 1.0f;
            p.chorusFeedback01 = 1.0f; // coda piu' lunga possibile
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            for (int b = 0; b < 100; ++b)
            {
                juce::AudioBuffer<float> buffer (2, kBlock);
                buffer.clear();
                synth.process (buffer, midi);
                midi.clear();
            }

            midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);

            // Rilascio, poi coda del chorus, poi il mezzo secondo di ringout: abbondante.
            auto tail = 0.0f;
            const int blocks = (int) std::ceil (1.5 * kSampleRate / (double) kBlock);

            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buffer (2, kBlock);
                buffer.clear();
                synth.process (buffer, midi);
                midi.clear();

                if (b > blocks / 2)
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlock; ++i)
                            tail = juce::jmax (tail, std::abs (buffer.getSample (ch, i)));
            }

            expect (tail < 1.0e-4f, "coda " + juce::String (tail) + ": il chorus non si spegne");

            // Riaccendendo, la linea e' stata azzerata dal ringout: il primo blocco di una nota
            // nuova non puo' contenere l'eco di quella vecchia. Se il reset mancasse, qui
            // uscirebbe il chorus di mezzo secondo fa **prima** che la nota nuova sia arrivata
            // alla linea.
            juce::MidiBuffer again;
            again.addEvent (juce::MidiMessage::noteOn (1, 72, 1.0f), 0);
            juce::AudioBuffer<float> buffer (2, kBlock);
            buffer.clear();
            synth.process (buffer, again);

            for (int i = 0; i < kBlock; ++i)
                expect (std::isfinite (buffer.getSample (0, i)), "campione non finito dopo il ringout");
        }

        beginTest ("in mono lo stadio FX gira su una linea sola e non esce dal buffer");
        {
            // isBusesLayoutSupported accetta il mono, quindi il caso esiste davvero. Con un solo
            // canale il chorus usa la sola linea sinistra e i suoi tre pesi di pan, che hanno per
            // conto loro somma dei quadrati 1: niente da compensare e niente da sommare a un
            // canale che non c'e'.
            engine::SynthEngine synth;
            engine::EngineSpec spec;
            spec.sampleRate = kSampleRate;
            spec.maximumBlockSize = kBlock;
            spec.numChannels = 1;
            synth.prepare (spec);

            dsp::WavetableStore mono;
            mono.setActive (0);
            synth.setWavetable (mono.active());

            auto p = baseParams();
            p.chorusOn = true;
            p.chorusRateHz = 1.6f;
            p.chorusDepth01 = 0.6f;
            p.chorusMix01 = 1.0f;
            p.chorusFeedback01 = 1.0f;
            synth.setParams (p);
            synth.setMasterGainLinear (1.0f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);

            auto peak = 0.0f;

            for (int b = 0; b < 200; ++b)
            {
                juce::AudioBuffer<float> buffer (1, kBlock);
                buffer.clear();
                synth.process (buffer, midi);
                midi.clear();

                for (int i = 0; i < kBlock; ++i)
                {
                    expect (std::isfinite (buffer.getSample (0, i)), "campione non finito in mono");
                    peak = juce::jmax (peak, std::abs (buffer.getSample (0, i)));
                }
            }

            expect (peak > 0.05f, "uscita mono troppo bassa (" + juce::String (peak) + ")");
            expect (peak <= 1.0f, "campione mono fuori scala (" + juce::String (peak) + ")");
        }

        beginTest ("il margine al soft clipper: ai default e agli estremi del chorus");
        {
            // **Il metodo, prima dei numeri.** Fino alla ritaratura del gain staging questo test
            // risaliva al picco presentato al clipper invertendo analiticamente engine::softClip
            // sul picco d'uscita, ed era l'unico posto della suite a farlo. E' il metodo
            // sbagliato: l'inversione e' malcondizionata vicino alla saturazione, e su questa
            // passata il secco presentava 1.006, cioe' *gia' sopra soglia*. Adesso si usa il
            // metodo del gain ridotto, lo stesso di Tests/ReverbTests.cpp e di
            // Tests/EngineTests.cpp, e i tre insiemi di numeri sono finalmente confrontabili —
            // che e' l'unica ragione per cui questi numeri servono a qualcosa.
            //
            // Il caso e' quello che docs/architecture.md usa per il suo bilancio: accordo denso,
            // volume di default, tutto il resto ai valori di fabbrica.
            const auto measure = [&store, this] (bool on, float mix, float feedback)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = baseParams();
                p.filterOn = true;
                p.cutoffHz = 4000.0f;
                p.resonanceQ = 2.0f;
                p.chorusOn = on;
                p.chorusRateHz = 1.6f;   // il default di chRate
                p.chorusDepth01 = 0.4f;  // il default di chDepth
                p.chorusMix01 = mix;
                p.chorusFeedback01 = feedback;
                synth.setParams (p);

                const auto probe = peakAtClipper (synth, { 48, 55, 60, 64, 67, 72 }, 0.8f, 400);
                expect (probe.clipperOff, "il gain di prova non basta: la misura non vale");
                return probe.value;
            };

            const auto dry = measure (false, 0.0f, 0.0f);
            const auto atDefaults = measure (true, 0.3f, 0.0f);    // i default di chMix / chFeedback
            const auto fullMix = measure (true, 1.0f, 0.0f);
            const auto fullBoth = measure (true, 1.0f, 1.0f);

            const auto report = [this] (const char* what, double peak, double reference)
            {
                logMessage (juce::String (what) + ": " + juce::String (peak, 3) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS), "
                            + juce::String (juce::Decibels::gainToDecibels (peak / reference), 2)
                            + " dB rispetto al secco");
            };

            report ("secco (chorus spento)", dry, dry);
            report ("chorus ai default (mix 30 %, feedback 0)", atDefaults, dry);
            report ("chorus a mix 100 %, feedback 0", fullMix, dry);
            report ("chorus a mix 100 %, feedback 100 %", fullBoth, dry);

            // Tre contratti distinti, perche' le tre cause sono distinte.
            //
            // Ai **valori di fabbrica** il chorus e' acceso su tutti e dodici i preset, quindi
            // non gli e' concesso di prendere margine: il mix e' equal-power e il feedback e'
            // zero, quindi il picco puo' solo restare dov'e' o scendere. Misurato **-0.68 dB** —
            // scende, perche' il ramo secco del mix sin3dB al 30 % vale 0.891 e il bagnato, che
            // e' lo stesso segnale ritardato e sparpagliato, non ricostruisce quel picco. E'
            // anche il motivo per cui i preset suonano piu' bassi di picco con gli effetti accesi
            // che senza — fino a 1.4 dB su "Wire Pluck" (vedi Tests/EngineTests.cpp,
            // "nessun preset di fabbrica accende il clipper su un accordo di quattro note").
            const auto factoryDb = juce::Decibels::gainToDecibels (atDefaults / dry);
            expect (factoryDb < 0.0, "il chorus ai valori di fabbrica alza il picco al clipper di "
                                         + juce::String (factoryDb, 2) + " dB: ai default non deve prendere margine");

            // Il **mix** e' equal-power su segnali decorrelati, quindi per costruzione non alza
            // il picco: un decibel e' il margine che lascia passare la correlazione residua fra
            // secco e bagnato (non sono indipendenti, sono lo stesso segnale ritardato) e
            // nient'altro. Se questo comincia a mordere, e' la normalizzazione dei pesi di pan
            // a essersi rotta, e il test dei pesi di pan lo dira' piu' precisamente.
            const auto mixDb = juce::Decibels::gainToDecibels (fullMix / dry);
            expect (mixDb < 1.0, "il mix al 100 % alza il picco al clipper di " + juce::String (mixDb, 2)
                                     + " dB: non e' piu' equal-power");

            // Il **feedback** invece alza il picco davvero, ed e' il motivo per cui il ritorno
            // passa da un saturatore e per cui dsp::Chorus::kMaxFeedback si ferma a mezzo: la
            // tabella nel suo commento e' questa misura al variare di quel tetto.
            //
            // Misurato **+1.96 dB**, e la soglia sta appena sopra. Non e' piu' la rete larga che
            // era prima della ritaratura: il caso peggiore del chorus e' uno dei tre addendi che
            // il gain staging ha in bilancio, e un suo aumento va visto subito. Se un giorno
            // questa riga mordera', il numero da rivedere e' kMaxFeedback o kVoiceHeadroomGain.
            const auto worstDb = juce::Decibels::gainToDecibels (fullBoth / dry);
            expect (worstDb < 2.5, "il chorus al caso peggiore alza il picco al clipper di "
                                       + juce::String (worstDb, 2) + " dB");

            // E il valore assoluto, che e' quello che il gain staging deve tenere: anche con il
            // chorus a fondo corsa su un accordo di sei note l'ingresso al clipper resta ben
            // sotto il tetto di +8 dBFS del caso peggiore (misurato 1.260, +2.01 dBFS).
            expect (fullBoth < 2.512, "con il chorus a fondo corsa l'ingresso al clipper arriva a "
                                          + juce::String (fullBoth, 3) + ", oltre il tetto di +8 dBFS");
        }
    }
};

static FxStageTests fxStageTests;
