#pragma once

#include "dsp/ADSREnvelope.h"
#include "dsp/Lfo.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstddef>

namespace engine
{
/**
 * Soft clipper: guadagno unitario sul piccolo segnale, satura dolcemente e si ferma a 1.0
 * quando l'ingresso raggiunge 3. Niente tanh() per campione — costa tre volte tanto e da' meno.
 *
 * E' l'approssimante di Pade [3/2] di tanh, `x(27 + x^2) / (27 + 9x^2)`. Il denominatore ha una
 * radice **tripla** in 3 (x^3 - 9x^2 + 27x - 27 = (x-3)^3), quindi nel punto in cui la curva
 * tocca 1 sono nulle sia la derivata prima sia la seconda: il raccordo con il tratto piatto e'
 * C2. La derivata, in forma chiusa, e' f'(x) = 9(x^2-9)^2 / (27+9x^2)^2 — un quadrato, quindi
 * non negativa ovunque: la funzione e' monotona per costruzione, non per taratura.
 *
 * Provenienza: la stessa formula sta in `DaisySP/Source/Filters/ladder.cpp` come `fast_tanh`
 * (licenza **MIT**, (c) Richard van Hoesel / Infrasonic Audio), e in Surge XT come `wst_soft`.
 * E' comunque matematica pubblica: l'approssimante di Pade di tanh non e' l'invenzione di
 * nessuno. Nessun codice GPL e' entrato qui.
 *
 * Cosa sostituisce, e perche'. La curva precedente era `clamp(x, +-1.5)` seguito da
 * `c - c^3/6.75`: raccordava con derivata prima nulla ma lasciava la **seconda** che saltava da
 * -1.333 a 0, e sopra 1.5 appiattiva del tutto — il 24.8 % del ciclo a drive 6 dB, il 90.5 % a
 * 24 dB. Quello spigolo e quel tratto piatto sono la sorgente dell'alias: le armoniche decadono
 * come 1/n^3 invece che esponenzialmente, e tutto cio' che sta sopra Nyquist si ripiega in
 * banda su frequenze che non hanno relazione armonica con la nota. Misurato a 48 kHz, alias
 * rispetto alla fondamentale: a MIDI 84 e drive 6 dB da -75.4 a -151.9 dB, a MIDI 60 da -111.7
 * a -334 dB (in doppia precisione; il percorso float si ferma sul proprio arrotondamento,
 * -180). Sopra MIDI ~100 le due curve pareggiano e nessuna delle due puo' fare meglio: con la
 * fondamentale a 4 kHz ci stanno cinque armoniche sotto Nyquist, e da li' in poi servirebbe
 * l'oversampling, che e' una decisione diversa.
 *
 * **Non e' neutra sul suono**: a x = 1 vale 0.778 contro 0.852, e il ginocchio sta a 3.0 invece
 * che a 1.5. Satura prima e piu' dolcemente, e a parita' di `drive` l'uscita e' fra 0.1 e 0.8 dB
 * piu' bassa in RMS. Niente e' stato ritarato per compensare: ne' `drive`, ne' i preset, ne'
 * kVoiceHeadroomGain. E' una decisione che richiede di riascoltare, non di ricalcolare.
 *
 * I due rami sono il clamp, e non un `std::clamp` applicato al risultato: con x infinito la
 * forma razionale calcolerebbe inf/inf = NaN, e ogni confronto con NaN e' falso, quindi il
 * clamp lo lascerebbe passare intatto fino all'uscita. Cosi' invece l'infinito esce 1.
 *
 * Sta nell'header, e non piu' nel namespace anonimo di SynthVoice.cpp, perche' e' l'unica
 * nonlinearita' della catena: l'alias che genera e le sue proprieta' (dispari, monotona,
 * limitata) sono verificabili solo misurando *questa* funzione, non il motore intero. Il
 * template serve a quello: la produzione la istanzia a `float`, la misura a `double`, dove il
 * pavimento numerico sta centocinquanta decibel piu' in basso e l'alias vero resta visibile.
 */
template <typename T>
constexpr T saturateCurve (T x) noexcept
{
    if (x > T (3))
        return T (1);

    if (x < T (-3))
        return T (-1);

    const auto x2 = x * x;
    return x * (T (27) + x2) / (T (27) + T (9) * x2);
}

/**
 * L'istanza usata dal thread audio. Nessuna chiamata a libm: due confronti, quattro
 * moltiplicazioni, una divisione.
 *
 * Costo misurato in Release (clang -O3, Apple Silicon), al netto del ciclo a vuoto e con la
 * vettorizzazione spenta, che e' il caso vero — nel ciclo di render() il filtro introduce una
 * dipendenza seriale fra un campione e il successivo: **0.75 ns/campione contro 0.63** della
 * curva precedente. Sono 0.12 ns in piu', che a 16 voci per 8 copie di unison fanno 6.1 milioni
 * di chiamate al secondo, cioe' 0.7 ms per secondo di audio: lo 0.07 % di un core.
 *
 * `std::tanh` costerebbe 1.14 ns — una volta e mezza — e darebbe **meno**, non di piu': misurato
 * con la stessa DFT, a MIDI 84 alias -141.4 dB a drive 6 dB e -158.1 a 4.8, cioe' dieci
 * decibel peggio del Pade a ogni drive usabile. Il Pade non e' un'approssimazione economica di
 * tanh: sopra 3 e' esattamente piatto, mentre tanh continua a curvare all'infinito, e sono
 * quelle curvature residue a generare le armoniche alte che si ripiegano.
 */
inline float saturate (float x) noexcept { return saturateCurve (x); }

/** Copie dell'oscillatore per voce al massimo dell'unison. Array a dimensione fissa, come il
    pool di voci: nessuna allocazione sul thread audio, mai. */
inline constexpr int kMaxUnison = 8;

/**
 * Posizione della copia `index` fra `voices` sull'asse -1..+1: -1 la prima, +1 l'ultima, e per
 * costruzione la somma su tutte le copie e' zero. Con una copia sola vale **esattamente** 0.
 *
 * E' la distribuzione usata sia per il detune (moltiplicata per i cent) sia per lo spread
 * stereo (moltiplicata per kUnisonSpreadWidth): una sola formula, cosi' le due immagini —
 * quella tonale e quella spaziale — non possono scivolare l'una rispetto all'altra.
 *
 * Lo zero esatto a `voices == 1` non e' un dettaglio: da li' discende che il rapporto di
 * frequenza sia exp2(0) = 1.0f e l'offset di pan 0.0f, cioe' che con unison 1 il segnale resti
 * identico campione per campione a quello di prima dell'unison.
 */
constexpr float unisonSpread (int index, int voices) noexcept
{
    return voices > 1 ? 2.0f * (float) index / (float) (voices - 1) - 1.0f : 0.0f;
}

/**
 * Larghezza dello spread stereo delle copie, in unita' di pan (-1..+1), attorno al pan della
 * voce. Fissa: non esiste un parametro `width` e non va aggiunto.
 *
 * 0.6 e non 1.0 per due ragioni. La prima e' che a larghezza piena le due copie estreme
 * finiscono **completamente** in un canale solo, e un orecchio ne perde meta' della stirpe;
 * qui la copia piu' esterna sta a circa 10 dB di sbilanciamento L/R, larga senza diventare una
 * sorgente puntiforme sull'altoparlante. La seconda e' che lo spread si somma al pan della voce
 * e poi si limita a +-1: con larghezza piena il grappolo sarebbe gia' attaccato a entrambi i
 * bordi al centro della corsa, e il knob `pan` smetterebbe di fare qualcosa. Con 0.6 restano
 * 0.4 di corsa per lato prima che il limite morda.
 *
 * Il guadagno non ne risente: con pan a potenza costante la somma dei cos^2 su un insieme
 * simmetrico di posizioni vale N*cos^2(pan), quindi la compensazione 1/sqrt(N) resta esatta
 * canale per canale qualunque sia la larghezza (finche' il limite non interviene).
 */
inline constexpr float kUnisonSpreadWidth = 0.6f;

/**
 * Quanto dura la dissolvenza con cui una voce rubata lascia il posto, dal livello a cui si
 * trovava fino a -80 dB.
 *
 * Il riferimento e' l'`uber_release` di Surge XT, che sta attorno agli 11 ms; la ricerca
 * (docs/research/2026-09-19-confronto-synth-open-source.md, punto 10) indica 5-10 ms. Otto e'
 * il compromesso fra le due pressioni che tirano in direzioni opposte: piu' lunga e' la
 * dissolvenza, piu' dolce e' la transizione ma piu' a lungo la voce rubata continua a suonare
 * *sopra* la nota nuova — a 8 ms sono 384 campioni a 48 kHz, meno di un ciclo di un LA basso, e
 * non c'e' tempo perche' la coda si riconosca come una nota a se'.
 *
 * L'altro vincolo e' il margine del pool (VoiceManager::stealFadeMargin): quante voci possano
 * trovarsi in dissolvenza insieme dipende da quanti furti entrano in questa finestra, quindi
 * allungarla vuol dire allargare il pool. Otto millisecondi e tre slot di margine sono la
 * stessa decisione presa da due lati.
 *
 * La forma e' esponenziale, cioe' lineare in decibel, e non lineare in ampiezza: e' la stessa
 * curva del release di dsp::ADSREnvelope, ed e' quella che non lascia uno spigolo nella
 * derivata quando la dissolvenza comincia su una voce che stava gia' calando.
 */
inline constexpr float kStealFadeSeconds = 0.008f;

/**
 * Il livello sotto il quale la dissolvenza si considera finita e la voce viene davvero azzerata:
 * -80 dB, la stessa soglia di silenzio che usa dsp::ADSREnvelope per dichiarare spento un
 * release. Quello che resta da azzerare li' vale al massimo un decimillesimo dell'ampiezza di
 * partenza — sotto il pavimento a 16 bit, e quattro ordini di grandezza sotto la pendenza
 * naturale di qualunque onda.
 */
inline constexpr float kStealFadeFloor = 1.0e-4f;

/** Single synth voice: legge una EngineParams per blocco e sintetizza. */
class SynthVoice
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /**
     * `startOrder` e' un numero che cresce a ogni nota per l'intero pool: serve a VoiceManager
     * per sapere quale voce sta suonando da piu' tempo quando deve sceglierne una da rubare.
     * Lo assegna il pool e non la voce, perche' e' un ordinamento *fra* voci: se ognuna se lo
     * incrementasse da sola i numeri non sarebbero confrontabili.
     */
    void start (int midiNote, float velocity, unsigned long long startOrder) noexcept;

    /** Ribattuta della nota gia' assegnata a questa voce: fa ripartire il solo inviluppo.
        Fase dell'oscillatore e stato del filtro restano dove sono — vedi il commento
        nell'implementazione: e' quello che tiene continuo il segnale. */
    void retrigger (float velocity) noexcept;
    void stop() noexcept;
    void kill() noexcept;

    /**
     * La voce e' stata rubata: smette di contare per la polifonia e scende a zero in
     * kStealFadeSeconds invece di essere azzerata.
     *
     * E' l'altra meta' del furto, quella che riguarda la *transizione*: la nota nuova non
     * riusa questo slot: ne prende uno davvero libero (vedi VoiceManager::stealFadeMargin), e
     * qui resta solo una coda che si spegne. E' il pattern di Gin (`setFastKill`, BSD-3) e di
     * Surge XT (`uber_release`).
     *
     * Chiamarla due volte sulla stessa voce non riavvia la dissolvenza: il livello da cui si
     * scende e' quello raggiunto, non quello di partenza.
     */
    void beginStealFade() noexcept;

    bool isActive() const noexcept;
    int getMidiNote() const noexcept;

    /** Vero fra beginStealFade() e la fine della dissolvenza: la voce suona ancora ma non
        occupa piu' un posto nella polifonia e non risponde piu' a note-off ne' a ribattute. */
    bool isFading() const noexcept { return fading_; }

    /** Vero da stop() fino alla nota successiva: il tasto e' stato lasciato e l'inviluppo sta
        rilasciando. Non lo si chiede a dsp::ADSREnvelope perche' non espone lo stadio, e
        aggiungerglielo vorrebbe dire toccare Source/dsp per una domanda che nasce qui. */
    bool isReleasing() const noexcept { return released_; }

    /** Il livello dell'inviluppo d'ampiezza, dissolvenza del furto compresa: e' il numero su
        cui VoiceManager decide quale voce sia la piu' silenziosa. */
    float getAmplitudeLevel() const noexcept { return envelope_.getLevel() * fadeGain_; }

    /** L'ordine in cui questa voce e' stata avviata, confrontabile con quello delle altre. */
    unsigned long long getStartOrder() const noexcept { return startOrder_; }

    /** Mix into stereo buffers (additive). Real-time safe. */
    void render (float* outL, float* outR, int numSamples) noexcept;

    /** La tavola attiva, o nullptr. Chiamata dal thread audio (SynthEngine::process). */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /** Applica i parametri del blocco. Nessun atomico, nessuna allocazione. */
    void setParams (const EngineParams& p) noexcept;

    /** Il livello dell'LFO di questa voce: lo legge SynthEngine per il meter. */
    float getLfoLevel() const noexcept { return lfoRetrig_ ? lfo_.level() : globalLfoLevel_; }

    /**
     * Il livello **attuale** di una delle cinque sorgenti del matrix.
     *
     * E' la stessa espressione che applyModulation() usa per riempire sourceLevels_, e non e'
     * un caso: qui c'e' una sola definizione di "quanto vale env adesso", e la usano sia il
     * suono sia il meter. Se le due divergessero, l'anello del knob nella UI tornerebbe a
     * raccontare una storia diversa da quella che il filtro sta suonando — il difetto che
     * questo percorso esiste per togliere.
     *
     * Legge lo stato vivo (l'inviluppo, la velocity), non la cache di sourceLevels_: quella e'
     * ferma al valore di *inizio* della sotto-fetta di controllo appena resa, e per un meter
     * campionato ogni 33 ms il valore piu' fresco e' quello giusto.
     */
    float getSourceLevel (ModSource src) const noexcept;

    /**
     * Il livello dell'LFO libero, e nient'altro.
     *
     * E' l'unico campo di EngineParams che cambia *dentro* il blocco: SynthEngine lo rinfresca a
     * ogni sotto-fetta di controllo. Farlo arrivare con setParams() vorrebbe dire ripubblicare
     * tutta la struct — sedici sotto-fette per sedici voci, cioe' 256 chiamate per blocco,
     * ognuna delle quali ricalcola tre coefficienti d'inviluppo con exp() e riscorre la lista
     * delle route per rifare la maschera. Qui si scrive un float.
     *
     * Serve solo con lfoRetrig falso: con il retrigger acceso la voce legge il proprio LFO.
     */
    void setGlobalLfoLevel (float level) noexcept { globalLfoLevel_ = level; }

private:
    void updateCutoff (bool snap) noexcept;

    /** Ricalcola rapporti di detune e guadagno di compensazione. Gira solo quando `unison` o
        `detune` cambiano davvero: exp2() non ha niente da fare in un loop per campione. */
    void updateUnison (int voices, float detuneCents) noexcept;

    /** Ricalcola i livelli delle cinque sorgenti e riapplica i sette target modulabili.
        Gira una volta per blocco (o per fetta fra due eventi MIDI), mai per campione. */
    void applyModulation() noexcept;

    /** Somma le route che puntano a `targetIndex` sul valore normalizzato di base, e clampa.
        Stessa aritmetica di liveValue() in WebUI/src/synth/mod.ts. */
    float modulated (int targetIndex) const noexcept;

    /** Vero se almeno una route punta a quel target. Vedi il commento di modMask_. */
    bool isModulated (int targetIndex) const noexcept
    {
        return targetIndex >= 0 && (modMask_ & (1u << (unsigned) targetIndex)) != 0u;
    }

    double sampleRate_ { 44100.0 };
    bool active_ { false };
    int midiNote_ { -1 };
    float velocity_ { 0.0f };
    float frequencyHz_ { 440.0f };

    /** Il tasto e' stato lasciato: lo sa questa classe perche' ADSREnvelope non espone lo
        stadio in cui si trova. Falso a ogni start()/retrigger(), vero a stop(). */
    bool released_ { false };

    // --- furto ---

    /**
     * I due numeri della dissolvenza del furto, e la ragione per cui **non** sono un bool e un
     * contatore: moltiplicano il segnale per campione, quindi il percorso normale deve costare
     * niente ed essere bit-trasparente.
     *
     * A riposo `fadeGain_` vale esattamente 1.0f e `fadeCoeff_` esattamente 1.0f, quindi la
     * ricorrenza `fadeGain_ *= fadeCoeff_` e' l'identita' bit per bit (1.0f * 1.0f e' 1.0f in
     * IEEE, senza arrotondamento) e `x * fadeGain_` restituisce x intatto. Non serve nessun
     * ramo nel loop per campione, e il segnale di una voce che non e' stata rubata esce con gli
     * stessi bit di prima che questo meccanismo esistesse.
     */
    float fadeGain_ { 1.0f };
    float fadeCoeff_ { 1.0f };
    bool fading_ { false };

    /** Il coefficiente per campione che porta la dissolvenza da 1 a kStealFadeFloor in
        kStealFadeSeconds. Calcolato in prepare(): l'exp() che serve non deve girare al momento
        del furto, che avviene sul thread audio. */
    float stealFadeCoeff_ { 1.0f };

    /**
     * Quando questa voce e' stata avviata, nella numerazione del pool. Assegnato da start(),
     * confrontato da VoiceManager::chooseVictim().
     *
     * Volutamente **non** azzerato da reset(): e' un dato che ha significato solo mentre la
     * voce e' attiva, e start() lo riscrive sempre prima che qualcuno lo legga. Azzerarlo
     * sarebbe una riga che sembra pulizia e invece e' solo un'altra strada da cui il valore
     * puo' divergere.
     *
     * A 64 bit non trabocca: anche a un milione di note al secondo servirebbero seicentomila
     * anni. Con un intero piu' piccolo il confronto "piu' vecchia" si invertirebbe
     * silenziosamente al giro di boa, ed e' un bug che si manifesterebbe dopo ore di uso.
     */
    unsigned long long startOrder_ { 0 };

    /** Le otto copie possibili; ne girano `unisonVoices_`. L'array e' sempre grande otto:
        allocarlo a runtime sarebbe vietato, e ottanta byte per voce non si notano. */
    std::array<dsp::WavetableOscillator, kMaxUnison> oscillators_;

    /** Il filtro del canale sinistro — l'unico quando unison e' 1, perche' in quel caso la
        voce e' monofonica fino al pan finale, esattamente come prima. */
    dsp::StateVariableFilter filter_;

    /** Il filtro del canale destro, usato solo con unison > 1. Con le copie sparse nel campo
        stereo i due canali portano miscele diverse, e un filtro solo le rifonderebbe in una:
        lo spread non sopravviverebbe al filtro, che e' il punto di averlo. */
    dsp::StateVariableFilter filterRight_;

    dsp::ADSREnvelope envelope_;

    /**
     * Il secondo inviluppo: parte, rilascia e si azzera insieme a quello d'ampiezza, ma la sua
     * uscita non moltiplica mai il segnale — la legge solo applyModulation(), come livello della
     * sorgente ModSource::env2.
     *
     * Non entra in isActive(): la vita della voce continua a dipendere dal solo `envelope_`.
     * Fosse altrimenti, un release corto qui dentro troncherebbe una nota ancora in coda, e un
     * sustain a zero la ucciderebbe a meta' — un modulatore che spegne cio' che modula.
     *
     * Avanza in render() con lo stesso numero di campioni dell'inviluppo d'ampiezza, in un ciclo
     * a parte: getNextSample() e' l'unico modo che ADSREnvelope offre per far correre il tempo,
     * e i due cicli di rendering restano quelli di prima, riga per riga.
     */
    dsp::ADSREnvelope envelope2_;

    bool oscOn_ { true };
    bool filterOn_ { true };
    float driveGain_ { 1.0f };
    float tuningSemitones_ { 0.0f };
    float velocityAmount_ { 0.0f };
    float baseCutoffHz_ { 1000.0f };
    float keyTrack_ { 0.0f };

    // --- unison ---

    int unisonVoices_ { 1 };
    float detuneCents_ { 0.0f };

    /** Rapporto di frequenza di ogni copia, exp2(cent / 1200). Calcolato in updateUnison(),
        cioe' a cambio di parametro, mai per campione. Parte da 1 su tutte e otto: con unison 1
        la copia zero deve moltiplicare la frequenza per *esattamente* 1.0f. */
    std::array<float, kMaxUnison> detuneRatio_ { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    /** 1/sqrt(N): N copie scorrelate sommano in potenza, non in ampiezza, quindi crescono come
        la radice. Senza, passare da unison 1 a unison 8 aggiungerebbe circa 9 dB. */
    float unisonGain_ { 1.0f };

    /** Guadagni di pan per copia, ricalcolati una volta per blocco: N coppie invece di una, ma
        sempre fuori dal loop per campione — cos()/sin() li' dentro sono vietati. */
    std::array<float, kMaxUnison> unisonGainL_ {};
    std::array<float, kMaxUnison> unisonGainR_ {};

    /**
     * Rampe di 20 ms: evitano gradini udibili quando l'utente o l'host muovono un parametro a
     * scatti fra un blocco e l'altro.
     *
     * Dentro ci finisce il valore **di base**, mai quello gia' modulato. Non e' un dettaglio
     * d'implementazione: con il bersaglio riposato una volta per sotto-fetta la rampa ne
     * recupera 32/960 per volta, che e' un passa-basso a un polo a 7.96 Hz. Applicato al totale,
     * quel polo filtrava la modulazione insieme al knob — una route lfo -> cutoff usciva a -3 dB
     * a 8 Hz e a -8.6 dB a 20, il massimo che l'LFO sa produrre. Stavamo filtrando la nostra
     * stessa modulazione, che e' gia' continua per costruzione e non ha nessun gradino da
     * smussare. La somma delle modulazioni entra quindi **dopo**, in render(): gli scostamenti
     * qui sotto. E' la stessa separazione di Vital (SynthModule::createBaseModControl: lo
     * smoother sul controllo, la somma delle mod collegata a valle).
     *
     * Cutoff e Position restano costosi (tan(), ricalcolo degli indici di frame) e si aggiornano
     * una sola volta per sotto-fetta; Pan pure, perche' costa cos()/sin(); solo Level e' rampato
     * per campione, perche' getNextValue() e' un'interpolazione lineare senza libm.
     */
    juce::SmoothedValue<float> smoothedCutoff_;
    juce::SmoothedValue<float> smoothedFramePosition_;
    juce::SmoothedValue<float> smoothedLevel_;
    juce::SmoothedValue<float> smoothedPan_;

    /**
     * Cio' che la modulazione aggiunge al valore rampato, ricalcolato a ogni sotto-fetta di
     * controllo e applicato **a valle** dello smoother. Zero (e uno, per il rapporto) quando
     * nessuna route punta a quel bersaglio: il percorso non modulato resta bit per bit quello
     * di prima, come per modMask_.
     *
     * Per cutoff e' un **rapporto** e non uno scostamento, e la ragione e' la mappa: `cutoff` e'
     * Map::Log da 20 a 20000 Hz, cioe' 20 x 1000^x, quindi lo scostamento in hertz che una
     * stessa profondita' produce dipende da dove sta la base — 632 Hz di modulazione a meta'
     * corsa, 12 kHz vicino al fondo scala. Uno scostamento additivo *salterebbe* ogni volta che
     * la base si muove, proprio mentre la rampa e' li' a impedire quel salto. Il rapporto
     * invece non dipende dalla base (e' 1000^(delta normalizzato)): la rampa moltiplicata per
     * una costante resta una rampa. Gli altri tre bersagli hanno mappe identita' (wtpos, level)
     * o affini (pan), dove lo scostamento e' gia' indipendente dalla base e la somma basta.
     *
     * Il clamp a 0..1 resta **prima** della denormalizzazione e dopo la somma, dentro
     * modulated(): e' il contratto con liveValue() di WebUI/src/synth/mod.ts, ed e' quel valore
     * che l'anello del knob mostra.
     */
    float cutoffModRatio_ { 1.0f };

    /** cutoffHzFromRaw(modBase[cutoff]), calcolato in setParams. La base non cambia dentro il
        blocco, mentre applyModulation() gira una volta per sotto-fetta: senza questa cache la
        mappa Log del cutoff pagherebbe **due** std::pow per sotto-fetta invece di uno — quello
        della base e quello del totale — cioe' 256 pow in piu' per blocco su sedici voci. */
    float modBaseCutoffHz_ { 0.0f };
    float framePositionMod_ { 0.0f };
    float levelMod_ { 0.0f };
    float panMod_ { 0.0f };

    // --- modulazione ---

    /** I parametri del blocco per intero: la modulazione si valuta in render(), non in
        setParams(), quindi servono ancora dopo che il processore li ha depositati. */
    EngineParams params_ {};

    dsp::Lfo lfo_;

    /** I livelli delle cinque sorgenti, nell'ordine di engine::ModSource. */
    std::array<float, (size_t) ModSource::count> sourceLevels_ {};

    /**
     * Un bit per target modulabile: acceso quando almeno una route punta li'.
     *
     * Serve a tenere separati i due percorsi. Per un target senza route la voce continua a
     * usare il valore gia' denormalizzato che arriva in EngineParams, cioe' esattamente il
     * codice di prima della modulazione: la non-regressione diventa cosi' una proprieta'
     * strutturale invece di una coincidenza numerica fra due formule che devono combaciare.
     * Come effetto secondario, a matrix vuoto si risparmiano sette conversioni per blocco e
     * per voce, due delle quali con std::pow.
     */
    unsigned int modMask_ { 0 };

    float globalLfoLevel_ { 0.0f };
    bool lfoRetrig_ { true };
    float lfoPhaseOffset01_ { 0.0f };
};
} // namespace engine
