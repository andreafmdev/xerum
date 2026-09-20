/*
  Questo file e' un adattamento di `modules/gin_dsp/dsp/gin_platereverb.h` di FigBug/Gin.
  Quel file porta una licenza MIT propria, distinta dalla BSD-3 del modulo che lo contiene, e
  la sua intestazione va conservata: e' un obbligo della licenza, non una cortesia.

  ---------------------------------------------------------------------------------------------

  MIT License

  Copyright (c) 2023 Mike Jarmy

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ---------------------------------------------------------------------------------------------

  L'algoritmo e' il plate di Dattorro:

      Dattorro, J. 1997. "Effect Design Part 1: Reverberators and Other Filters."
      Journal of the Audio Engineering Society, Vol. 45, No. 9
      https://ccrma.stanford.edu/~dattorro/EffectDesignPart1.pdf
*/

#pragma once

#include "dsp/Constants.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace dsp
{
/**
 * Riverbero a piastra di Dattorro, adattato da `gin_platereverb.h` (MIT, (c) 2023 Mike Jarmy).
 *
 * La catena e' quella del paper, invariata: predelay -> passa-basso d'ingresso -> quattro
 * diffusori allpass -> due tank incrociati (allpass modulato da un LFO, delay, damping a un
 * polo, secondo allpass) -> sette tap d'uscita per canale a segni alternati, presi **dalle
 * linee del tank opposto**, che e' cio' che rende i due canali decorrelati partendo da una
 * somma mono.
 *
 * Come il chorus, questa classe produce **solo il segnale bagnato**: dry/wet, bypass e ringout
 * stanno nello stadio FX di SynthEngine e sono gli stessi per i due effetti.
 *
 * ## Cosa e' stato cambiato rispetto all'originale, e perche'
 *
 * **Non e' piu' un template `<F, I>`.** Gin lo istanzia su `<float, int>`; noi lo fissiamo li'.
 * La ragione non e' estetica: il modulo `gin_dsp` che conterrebbe il template richiede C++20 e
 * noi siamo a C++17, quindi portare il singolo header era comunque la strada, e un template a
 * una sola istanziazione in un header e' solo tempo di compilazione in piu'.
 *
 * **`FastMath<F>::fastSin` -> `std::sin` una volta per fetta.** `FastMath` e' di Gin e non
 * viene con l'header. L'LFO dei due tank si valuta ora una volta ogni kModulationSliceSamples
 * campioni e il ritardo lo insegue con un polo singolo, esattamente come i sei tap del chorus:
 * nel ciclo per campione non resta una sola chiamata a libm, e il tasso di modulazione e' una
 * proprieta' dell'effetto invece che della dimensione del buffer dell'host.
 *
 * **Le tre avvertenze del codice originale, una per una.**
 *
 * 1. *`setSize()` sposta i tap istantaneamente.* Con il parametro automatizzato quello e' uno
 *    zipper udibile: il tap piu' lungo sta a ~4400 campioni e un salto di corsa piena lo
 *    sposterebbe di migliaia di campioni fra due campioni adiacenti. Qui `sizeRatio` e' un
 *    unico scalare smussato **per campione** con un polo singolo (kSizeSmoothingHalfLife), e
 *    tutti i ritardi — i quattro del tank e i quattordici tap — sono prodotti da lui al
 *    momento dell'uso. Il puntatore di lettura si muove con continuita', quindi il salto
 *    diventa lo spostamento Doppler che qualunque riverbero fa su una sweep di size: un
 *    "whoosh", non un clic. Non esiste una taratura che tolga anche quello — muovere un tap
 *    *e'* cambiare la velocita' di lettura — e lo stesso vale per il predelay.
 * 2. *`setSampleRate()` rialloca con `new`.* Qui l'unico punto che alloca e' `prepare()`, che
 *    dimensiona i `std::vector` al massimo (kMaxSizeRatio, predelay massimo) una volta sola.
 *    `setParameters()` e `process()` non chiedono memoria, e `size` non rialloca niente perche'
 *    le linee sono gia' lunghe quanto serve alla dimensione massima.
 * 3. *`juce::MathConstants` e il `FastMath` di Gin.* Il primo resta (e' in juce_core, che
 *    linkiamo gia'), il secondo e' sparito con il punto sopra.
 *
 * **Tre difetti dell'originale corretti.** `DelayLine::reset()` di Gin azzera anche i
 * coefficienti del filtro a un polo, che dopo un reset resta muto finche' qualcuno non
 * richiama `setCutoff` — qui `reset()` azzera solo lo stato. `ceilPowerOfTwo` passava da
 * `std::pow(2, ceil(log(n)/log(2)))`, cioe' decideva la dimensione di un buffer con due
 * logaritmi in virgola mobile; qui e' `juce::nextPowerOfTwo`. E il buffer e' dimensionato su
 * `size + 2`, non su `size`: `tap()` legge fino a `size + 2` campioni indietro (l'indice
 * intero, piu' uno per l'interpolazione lineare, piu' quello gia' scritto), quindi con una
 * lunghezza esattamente potenza di due l'originale avvolgerebbe nel futuro.
 *
 * **`mix` e' sparito.** Lo stadio FX ha gia' il suo juce::dsp::DryWetMixer con regola sin3dB;
 * un secondo mix qui dentro sarebbe una seconda taratura da tenere allineata.
 *
 * ## I numeri scelti da noi
 *
 * `kMinSizeRatio`/`kMaxSizeRatio`, `kMinDecay`/`kMaxDecay`, `kDampingMinHz`/`kDampingMaxHz`,
 * `kInputLowpassHz` e `kWetGain` hanno ciascuno il proprio commento qui sotto. Il riassunto:
 * la piastra di Dattorro sta a meta' della corsa di `size`, il decadimento si ferma molto
 * prima del `0.9999999` di Gin perche' sopra 0.8 il tempo di riverbero esce dalla scala
 * musicale prima ancora che dal buon senso, e il passa-basso d'ingresso e' fisso perche'
 * `rvDamp` governa gia' la brillantezza percepita della coda.
 */
class PlateReverb
{
public:
    /** Predelay massimo, in secondi: e' il valore di Gin e dimensiona la sua linea. */
    static constexpr float kMaxPredelaySeconds = 0.1f;

    /**
     * La corsa di `size`, in unita' di `sizeRatio` — che e' il `size / kMaxSize` di Gin, con
     * kMaxSize = 2.
     *
     * **sizeRatio 0.5 e' esattamente la piastra del paper**: i ritardi del tank valgono
     * `2 * base * 0.5 = base` campioni alla sample rate di Dattorro. La corsa 0.25..0.75 va
     * quindi da meta' a una volta e mezzo la piastra originale, e il default (`rvSize` 60 %)
     * cade a 0.55, cioe' appena sopra la piastra canonica.
     *
     * Il limite basso non e' scelto per gusto: a sizeRatio 0 tutti i ritardi del tank
     * collassano a zero e l'anello diventa un pettine strettissimo con guadagno `decay^4` —
     * limitato, ma metallico e senza niente a che vedere con un riverbero. A 0.25 il giro
     * completo della figura a otto dura ancora 363 ms, che e' una stanza piccola vera.
     *
     * Il limite alto e' quello che fissa la coda dichiarata all'host: vedi tailSeconds().
     */
    static constexpr float kMinSizeRatio = 0.25f;
    static constexpr float kMaxSizeRatio = 0.75f;

    /**
     * La corsa del coefficiente di decadimento del tank.
     *
     * Gin arriva a **0.9999999**, che non e' un tempo di riverbero ma un congelatore: il
     * segnale perde `d^4` per ogni giro della figura a otto, quindi il tempo di -60 dB va come
     * `T_loop * ln(1e-3) / (4 ln d)` e a 0.9999999 fa qualche milione di secondi. Anche 0.9,
     * che a occhio sembra un valore normale per un coefficiente, da' 23 secondi al massimo
     * della size. Sopra 0.8 il knob smette di essere una scelta musicale e diventa una scelta
     * fra "lungo" e "infinito", con tutta la parte interessante schiacciata sull'ultimo
     * millimetro di corsa.
     *
     * Con 0.10..0.80 la coda **misurata** — l'ultimo campione sopra -60 dB di una risposta
     * all'impulso — copre da **0.35 s** (decay 0, size 0) a **6.42 s** (decay 100 %, size 100 %),
     * e al default — decay 50 %, size 60 % — vale 1.5 s: una sala media. La corsa e' lineare nel
     * coefficiente e quindi grosso modo esponenziale nel tempo, che e' il verso giusto, perche'
     * il tempo di riverbero si giudica in rapporti. La tabella completa dei nove angoli sta in
     * "la formula della coda copre quella misurata", Tests/ReverbTests.cpp.
     */
    static constexpr float kMinDecay = 0.10f;
    static constexpr float kMaxDecay = 0.80f;

    /**
     * La corsa del damping, in Hz, percorsa logaritmicamente da `rvDamp`.
     *
     * A 0 % il filtro dentro il tank sta a 20 kHz, cioe' praticamente non c'e'; a 100 % a
     * 500 Hz, che e' una coda molto scura ma ancora una coda. Logaritmica e non lineare per la
     * stessa ragione di `res`: l'orecchio giudica la frequenza in rapporti, e una corsa lineare
     * avrebbe tenuto tutta la differenza udibile nell'ultimo decimo.
     */
    static constexpr float kDampingMaxHz = 20000.0f;
    static constexpr float kDampingMinHz = 500.0f;

    /**
     * Il passa-basso d'ingresso, fisso.
     *
     * Dattorro lo chiama `bandwidth` e lo espone; noi no, e 10 kHz e' il valore. Tre ragioni.
     * Una piastra vera non irradia sopra i 10 kHz, quindi il taglio *e'* parte del timbro che
     * si sta imitando. Il tank contiene un allpass il cui ritardo oscilla di +-19 campioni: su
     * materiale vicino a Nyquist quella modulazione e' un chirp udibile, e l'interpolazione
     * lineare delle linee lassu' e' comunque gia' sporca — tenerla fuori e' gratis. E soprattutto
     * la brillantezza della coda la governa gia' `rvDamp`: due controlli per una sola
     * percezione sono un controllo di troppo, e questo e' quello che non serve muovere.
     *
     * Non scende piu' in basso perche' sotto i ~6 kHz il riverbero comincia a incupire anche i
     * suoni che dovrebbero restare brillanti, e a quel punto il difetto si sentirebbe come
     * "questo synth ha un riverbero spento" invece che come una scelta. Resta sotto Nyquist a
     * ogni sample rate che ha senso supportare (a 44.1 kHz Nyquist e' 22.05 kHz).
     */
    static constexpr float kInputLowpassHz = 10000.0f;

    /**
     * Compensazione di livello del bagnato.
     *
     * I sette tap si sommano senza normalizzazione, come nel paper: il bagnato che ne esce non
     * ha nessuna ragione di stare al livello del secco, e se non sta li' il mix equal-power
     * dello stadio FX diventa un controllo di volume mascherato da controllo di
     * proporzione. Misurato con rumore passa-bassato ai default (size 60 %, decay 50 %, damp
     * 40 %), il bagnato grezzo sta **+4.7 dB** sopra il secco; questa costante lo riporta a
     * zero. Vedi "il bagnato del riverbero sta al livello del secco" in Tests/ReverbTests.cpp,
     * che e' la misura, non una stima.
     *
     * Non e' un livello costante su tutta la corsa — con decay a fondo corsa l'energia che si
     * accumula nel tank e' maggiore — e non puo' esserlo: normalizzare istante per istante
     * vorrebbe dire comprimere la coda. E' tarata al centro, e gli estremi stanno dentro un
     * paio di decibel.
     */
    static constexpr float kWetGain = 0.58f;

    /** La lunghezza massima di una fetta di modulazione, in campioni. Come il chorus. */
    static constexpr int kModulationSliceSamples = kControlRateSamples;

    /**
     * Emivita dei poli che inseguono i parametri di posizione — `sizeRatio` e il predelay.
     *
     * Cinquanta millisecondi e non venti come nel chorus: qui il salto da smussare e' molto
     * piu' grande (il tap piu' lungo si sposta di migliaia di campioni fra i due estremi di
     * `size`, contro i cinque e mezzo del chorus), e l'emivita fissa la velocita' massima a cui
     * il puntatore di lettura si muove, cioe' quanto diventa acuto il Doppler durante la
     * transizione. A 50 ms una corsa piena di `size` produce un glissando di circa un'ottava e
     * mezza che dura un decimo di secondo; a 20 ms sarebbero tre ottave.
     */
    static constexpr float kSizeSmoothingHalfLifeSeconds = 0.050f;

    /** Emivita dei poli che inseguono i parametri di guadagno e l'LFO. Come il chorus. */
    static constexpr float kSmoothingHalfLifeSeconds = 0.020f;

    /**
     * Dimensiona tutte le linee e azzera lo stato. **Alloca**: solo da prepareToPlay.
     *
     * E' l'unica funzione della classe che chiede memoria, ed e' il punto 2 delle avvertenze
     * sul codice originale: li' la riallocazione stava dentro `setSampleRate`, che e' un nome
     * che invita a chiamarlo da dove non si deve.
     */
    void prepare (double sampleRate);

    /** Azzera linee, filtri e LFO. Non alloca — e' un `fill` su ~440 KB. */
    void reset() noexcept;

    /**
     * I quattro parametri, normalizzati 0..1 tranne il predelay. Una volta per blocco.
     *
     * `size01`, `decay01` e `damp01` percorrono le corse dichiarate sopra; `predelaySeconds` e'
     * in secondi e viene limitato a kMaxPredelaySeconds, che e' la lunghezza della linea.
     */
    void setParameters (float size01, float decay01, float damp01, float predelaySeconds) noexcept;

    /**
     * Riscrive `left` e `right` con il **solo** segnale bagnato. `right` puo' essere nullptr:
     * in mono entra il solo canale sinistro ed esce il tap set sinistro.
     *
     * Nessuna allocazione, nessun lock, nessuna libm nel ciclo per campione.
     */
    void process (float* left, float* right, int numSamples) noexcept;

    /**
     * Il tempo di -60 dB della coda ai parametri correnti, piu' il predelay, in secondi.
     *
     * Serve a due cose che devono dire la stessa cosa: il ringout dello stadio FX (che non deve
     * spegnere l'effetto mentre la coda suona ancora) e — nel caso peggiore, vedi
     * tailSecondsAtExtremes() — `getTailLengthSeconds()` del plugin.
     *
     * E' la formula, non una misura: il segnale perde `decay^4` per ogni giro della figura a
     * otto (una moltiplicazione dentro ciascun tank piu' una su ciascuna delle due diagonali
     * incrociate), e il giro dura `kFigureEightSeconds * sizeRatio`. Le due catene di allpass
     * non aggiungono perdita — sono passa-tutto — e il damping a un polo ha guadagno unitario
     * in continua, quindi e' la componente grave a fissare la coda, ed e' questa.
     *
     * Con una correzione, che e' una misura e non una cautela: gli allpass non ritardano quanto
     * la loro linea — il ritardo di gruppo di un allpass di Schroeder dipende dalla frequenza —
     * quindi il giro vero e' un po' piu' lungo di quello nominale. Confrontando la formula con
     * l'ultimo campione sopra -60 dB della risposta all'impulso, il rapporto peggiore su tutta
     * la corsa di size e decay e' 1.18. kTailMargin porta la formula sopra quel rapporto a ogni
     * combinazione provata (vedi "la formula della coda copre quella misurata" in
     * Tests/ReverbTests.cpp): sovrastimare la coda costa qualche secondo di render offline,
     * sottostimarla la fa tagliare.
     */
    float tailSeconds() const noexcept;

    /** La coda nel caso peggiore che i parametri permettono: decay e size a fondo corsa. */
    static float tailSecondsAtExtremes() noexcept;

    /**
     * La durata del giro completo della figura a otto a `sizeRatio` 1, in secondi.
     *
     * (672 + 4453 + 1800 + 3720) + (908 + 4217 + 2656 + 3163) = 21589 campioni alla sample rate
     * di Dattorro (29761 Hz), moltiplicati per il kMaxSize = 2 di Gin: 1.4508 s. E' indipendente
     * dalla sample rate perche' tutte le linee sono scalate per `fs / 29761`.
     */
    static constexpr float kFigureEightSeconds = 2.0f * 21589.0f / 29761.0f;

    /** Il margine della formula sulla coda misurata. Vedi tailSeconds(). */
    static constexpr float kTailMargin = 1.25f;

    /**
     * Il tap d'uscita piu' lontano, a sizeRatio 1, in secondi: 3627 campioni alla sample rate di
     * Dattorro.
     *
     * Entra in tailSeconds() come addendo e non come fattore, perche' e' un ritardo fisso e non
     * un decadimento: anche con `decay` a zero — nessuna ricircolazione, un solo passaggio nella
     * figura a otto — quel tap continua a far uscire segnale per il tempo che gli serve ad
     * arrivare. Senza questo termine la formula sta **sotto** la coda misurata all'estremo corto
     * della corsa (0.340 s contro 0.354), che e' l'unico verso in cui sbagliare fa danno.
     */
    static constexpr float kLongestTapSeconds = 3627.0f / 29761.0f;

private:
    //--------------------------------------------------------------------------------------
    // Le quattro classi che seguono sono quelle dell'originale, con i cambiamenti descritti
    // nel commento della classe: niente template, niente `new`, buffer su std::vector.
    //--------------------------------------------------------------------------------------

    /** Linea di ritardo circolare con tap interpolato linearmente. */
    class DelayLine
    {
    public:
        /** **Alloca.** `size` e' il ritardo massimo richiedibile, in campioni. */
        void prepare (int size);

        void reset() noexcept;

        void push (float value) noexcept
        {
            buffer_[(size_t) writeIndex_] = value;
            writeIndex_ = (writeIndex_ + 1) & mask_;
        }

        float tap (float delay) const noexcept
        {
            // Il limite non e' cosmetico e non e' l'assert dell'originale: qui un valore fuori
            // scala non e' un bug da segnalare in debug, e' una lettura fuori dal buffer in
            // release. Il costo e' un min e un max per tap.
            const auto clamped = juce::jlimit (0.0f, (float) size_, delay);
            const auto whole = (int) clamped;
            const auto frac = 1.0f - (clamped - (float) whole);

            // Aritmetica senza segno: `writeIndex_ - 1 - whole` e' quasi sempre negativo, e
            // l'AND con la maschera su un intero con segno e' comportamento definito
            // dall'implementazione fino a C++20.
            const auto readIndex = (unsigned) (writeIndex_ - 1 - whole);
            const auto umask = (unsigned) mask_;

            const auto a = buffer_[(size_t) ((readIndex - 1u) & umask)];
            const auto b = buffer_[(size_t) (readIndex & umask)];

            return a + (b - a) * frac;
        }

        /** Legge prima di scrivere: e' l'ordine che rende un ritardo di zero campioni legale. */
        float tapAndPush (float delay, float value) noexcept
        {
            const auto out = tap (delay);
            push (value);
            return out;
        }

    private:
        std::vector<float> buffer_;
        int size_ { 1 };
        int mask_ { 0 };
        int writeIndex_ { 0 };
    };

    /** Allpass di Schroeder su una linea di ritardo, con ritardo passato a ogni campione. */
    class DelayAllpass
    {
    public:
        void prepare (int size, float gain);
        void reset() noexcept { line_.reset(); }

        void setGain (float gain) noexcept { gain_ = gain; }

        float process (float x, float delay) noexcept
        {
            const auto delayed = line_.tap (delay);
            const auto w = x + gain_ * delayed;
            line_.push (w);
            return -gain_ * w + delayed;
        }

        float tap (float delay) const noexcept { return line_.tap (delay); }

    private:
        DelayLine line_;
        float gain_ { 0.0f };
    };

    /** Passa-basso a un polo, guadagno unitario in continua. */
    class OnePole
    {
    public:
        void setCutoff (float hz, double sampleRate) noexcept;

        float process (float x) noexcept
        {
            state_ = x * a_ + state_ * b_;
            return state_;
        }

        /** Azzera **solo** lo stato: i coefficienti sopravvivono (vedi il commento di classe). */
        void reset() noexcept { state_ = 0.0f; }

    private:
        float a_ { 1.0f };
        float b_ { 0.0f };
        float state_ { 0.0f };
    };

    /** Mezza figura a otto: allpass modulato, delay, damping, secondo allpass. */
    struct Tank
    {
        void prepare (double sampleRate, float apf1Base, float apf1Gain, float delay1Base,
                      float apf2Base, float apf2Gain, float delay2Base, float maxMod);
        void reset() noexcept;

        /** `sizeRatio` scala i quattro ritardi; `lfo` e' gia' smussato, in -1..+1. */
        void process (float input, float sizeRatio, float lfo, float decay) noexcept;

        DelayAllpass apf1, apf2;
        DelayLine del1, del2;
        OnePole damping;

        float out { 0.0f };

        float apf1Size { 0.0f };
        float apf2Size { 0.0f };
        float del1Size { 0.0f };
        float del2Size { 0.0f };
        float maxModDepth { 0.0f };
    };

    void processSlice (float* left, float* right, int numSamples) noexcept;

    static constexpr int kNumTaps = 7;

    double sampleRate_ { 44100.0 };

    DelayLine predelayLine_;
    OnePole inputLowpass_;
    std::array<DelayAllpass, 4> diffusers_;

    /** Il ritardo di ciascun diffusore, in campioni. Non scala con `size`: vedi prepare(). */
    std::array<float, 4> diffuserDelays_ {};

    Tank leftTank_, rightTank_;

    /** I tap d'uscita a sizeRatio 1, in campioni. Scalati al momento dell'uso. */
    std::array<float, kNumTaps> leftTaps_ {};
    std::array<float, kNumTaps> rightTaps_ {};

    // --- parametri: bersaglio (da setParameters) e valore corrente (smussato per campione) ---

    float sizeRatioTarget_ { kMinSizeRatio };
    float sizeRatio_ { kMinSizeRatio };
    float decayTarget_ { kMinDecay };
    float decay_ { kMinDecay };
    float predelayTarget_ { 0.0f };  // campioni
    float predelaySamples_ { 0.0f };
    float dampingHz_ { kDampingMaxHz };

    float sizeCoeff_ { 1.0f };
    float smoothingCoeff_ { 1.0f };

    /** Fase dei due LFO del tank, in cicli 0..1, e il valore smussato che arriva agli allpass. */
    float lfoPhaseLeft_ { 0.0f };
    float lfoPhaseRight_ { 0.0f };
    float lfoLeft_ { 0.0f };
    float lfoRight_ { 0.0f };

    bool primed_ { false };
};
} // namespace dsp
