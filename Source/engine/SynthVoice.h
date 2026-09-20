#pragma once

#include "dsp/ADSREnvelope.h"
#include "dsp/Lfo.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "dsp/Constants.h"
#include "dsp/Saturation.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstddef>

namespace engine
{

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
inline constexpr float kStealFadeFloor = dsp::kSilenceFloor;

/**
 * Sotto questo tempo il glide non esiste: la nota nuova arriva subito.
 *
 * E' la soglia di Vital (`portamento_slope.cpp`: sotto un millisecondo la pendenza va in
 * bypass), e serve a due cose diverse. La prima e' numerica: l'incremento per campione e'
 * `12 / (T * |dnota| * fs)`, e con T che tende a zero tende all'infinito — un ramo di bypass
 * costa meno di una guardia su un quoziente enorme. La seconda e' che a `glide` = 0, cioe' il
 * default del parametro, il bypass e' *l'unica* cosa che rende il percorso bit per bit quello
 * di prima: la posizione resta ferma a 1, la nota interpolata e' esattamente `(float) midiNote_`
 * e midiNoteToHz calcola gli stessi identici bit.
 */
inline constexpr float kGlideMinSeconds = 0.001f;

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
     * Cambia la nota di una voce che sta gia' suonando: e' il cambio d'intonazione del mono e
     * del legato, e il glide — se c'e' — parte da dove la voce si trova **adesso**.
     *
     * "Adesso" e non "dalla nota nominale precedente": se il glide precedente era a meta'
     * strada, la sorgente del nuovo e' il punto intermedio, non il capolinea che non e' mai
     * stato raggiunto. E' il comportamento di Surge (`update_portamento` conserva `portaphase`
     * e `portasrc_key` correnti), ed e' cio' che rende continuo un trillo suonato piu' veloce
     * del tempo di glide: senza, ogni nota nuova salterebbe indietro all'ultima nota nominale.
     *
     * Non tocca ne' l'inviluppo, ne' la fase, ne' il filtro: chi vuole anche il ritrigger
     * chiama retrigger() dopo. La separazione fra i due e' esattamente la differenza fra Mono e
     * Legato, e sta in VoiceManager.
     *
     * Con `glide` falso l'intonazione salta: e' il caso in cui la nota nuova non e' legata alla
     * precedente (vedi VoiceManager::canGlideFromLastNote). Non e' la stessa cosa di `glide`
     * vero con il knob a zero solo perche' il risultato coincide: li' e' il parametro a dire di
     * no, qui e' il modo di suonare.
     */
    void glideToNote (int midiNote, bool glide) noexcept;

    /**
     * Fa partire la nota gia' avviata da `sourceNote` invece che dalla propria altezza: e' il
     * glide **poly**, dove ogni voce nuova scivola dall'ultima nota suonata alla propria.
     *
     * Si chiama dopo start(), non al suo posto: start() e' il percorso che azzera il filtro,
     * risincronizza le rampe e fa partire gli inviluppi, e nessuna di quelle decisioni cambia
     * per via del glide. Qui si riscrive solo l'altezza da cui la voce parte.
     */
    void glideFrom (float sourceNote) noexcept;

    /** La nota **suonata** in questo istante, in numero di nota frazionario: durante un glide
        sta fra la sorgente e il bersaglio, altrove coincide con getMidiNote(). E' il numero su
        cui si interpola — non la frequenza — ed e' quello che i test misurano. */
    float getGlideNote() const noexcept { return glideNote_; }

    /** Vero finche' il glide non e' arrivato. Falso a glide spento: la posizione parte da 1. */
    bool isGliding() const noexcept { return glidePosition_ < 1.0f; }

    /** La frequenza della copia base dell'unison, in hertz: il detune delle altre copie e'
        relativo a questa. La leggono i test per verificare l'interpolazione. */
    float getFrequencyHz() const noexcept { return frequencyHz_; }

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

    /**
     * Arma un glide da `fromNote` alla nota corrente, o lo salta se non c'e' niente da fare.
     *
     * **Constant rate**: il tempo vero non e' `glideSeconds_`, e' `glideSeconds_ * |dnota| / 12`
     * — il knob dice quanto dura un'**ottava**. Costa la moltiplicazione che si vede qui sotto,
     * e ce l'hanno sia Vital (`kPortamentoScale`) sia Surge (`porta_constrate`), perche' e' cio'
     * che rende il glide musicale su intervalli diversi: a tempo fisso un semitono e due ottave
     * ci mettono lo stesso, cioe' il semitono striscia e il salto sembra istantaneo. A rate
     * costante la velocita' in semitoni al secondo e' la stessa, che e' quello che fa una mano
     * su una corda.
     *
     * L'incremento **non** viene memorizzato: advanceGlide() lo ricalcola da glideSeconds_ e
     * dagli estremi a ogni sotto-fetta. Costa una divisione per voce e solo mentre un glide e'
     * in corso, e in cambio muovere il knob a nota tenuta ha effetto subito invece che dalla
     * prossima nota — che e' il comportamento che ci si aspetta da un knob.
     */
    void beginGlide (float fromNote) noexcept;

    /** Avanza la posizione del glide di `numSamples` e ricalcola la nota interpolata. Gira una
        volta per sotto-fetta di controllo, come tutto il resto: le fette valgono al piu'
        SynthEngine::kControlBlockSamples, quindi 1500 Hz a 48 kHz. */
    void advanceGlide (int numSamples) noexcept;

    /** Riporta la frequenza degli oscillatori alla nota suonata (glide + accordatura). E' la
        coda di applyModulation(), estratta perche' anche glideFrom() e glideToNote() devono
        poterla richiamare senza rifare tutta la modulazione. */
    void updatePitch() noexcept;

    /** Ricalcola rapporti di detune e guadagno di compensazione. Gira solo quando `unison` o
        `detune` cambiano davvero: exp2() non ha niente da fare in un loop per campione. */
    void updateUnison (int voices, float detuneCents) noexcept;

    /** Ricalcola i livelli delle cinque sorgenti e riapplica gli otto target modulabili.
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

    // --- glide ---

    /**
     * I quattro numeri del glide, e il motivo per cui si interpola il **numero di nota** e non
     * la frequenza in hertz.
     *
     * L'orecchio sente l'altezza in logaritmo della frequenza: un'ottava e' un'ottava sia fra 100
     * e 200 Hz sia fra 1000 e 2000. Interpolare linearmente in hertz — che e' cio' che fa Odin 2,
     * con un polo singolo sulla frequenza — vuol dire che a meta' strada fra DO3 e DO4 si sente
     * il SOL e non il FA#, che un glide in su e uno in giu' sullo stesso intervallo hanno forme
     * percettive diverse, e che il tempo d'arrivo percepito dipende da dove si parte. Vital,
     * Surge, Serum e Diva interpolano tutti in numero di nota; qui non costa nulla, perche'
     * midiNoteToHz prende gia' un offset frazionario per il fine tuning e l'esponenziale gira
     * comunque una volta per sotto-fetta.
     *
     * A riposo `glidePosition_` vale esattamente 1 e `glideNote_` esattamente `(float) midiNote_`:
     * advanceGlide() esce al primo confronto e la frequenza calcolata e' bit per bit quella di
     * prima che il glide esistesse. E' lo stesso argomento di fadeGain_ per il furto — il
     * percorso spento non e' "quasi" trasparente, e' l'identita'.
     */
    float glideSourceNote_ { 0.0f };
    float glideTargetNote_ { 0.0f };
    float glideNote_ { 0.0f };
    float glidePosition_ { 1.0f };

    /** Il tempo del glide per **ottava**, in secondi, copiato da EngineParams a ogni blocco.
        Vedi EngineParams::glideSeconds e beginGlide() per il perche' di "per ottava". */
    float glideSeconds_ { 0.0f };

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

    /**
     * Quanto del campione saturato entra in miscela con quello secco: 0 a `drive` spento, 1 da
     * kDriveFadeGain in su.
     *
     * Esiste perche' `saturate()` **non e' l'identita' sul piccolo segnale nel senso che serve
     * qui**. La sua pendenza nell'origine vale 1, ma il segnale che le arriva e' un oscillatore
     * a fondo scala, e li' la curva comprime da sola: saturate(1.0) = 0.778, cioe' -2.2 dB.
     * Con il solo cancello `driveGain_ > 1` il knob passava quindi da "spenta" a "accesa a
     * piena forza" fra 0.00 e 0.01 dB, e la misura lo diceva: un **gradino di -0.94 dB di RMS
     * (-1.31 di picco) nel nulla**, seguito dai primi due decibel di corsa spesi solo a
     * riemergere dalla buca. Un knob che, girato di un capello, abbassa.
     *
     * La miscela lo toglie senza toccare la curva: sotto kDriveFadeGain l'uscita e'
     * `x + mix * (f(x) - x)`, cioe' parte esattamente da `x` e arriva esattamente a `f(x)`.
     * A `drive` zero il valore e' zero e il ramo non gira affatto, quindi il percorso di
     * default resta bit per bit quello di prima.
     */
    float driveMix_ { 0.0f };
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
    juce::SmoothedValue<float> smoothedWarp_;

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
    float warpMod_ { 0.0f };

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
