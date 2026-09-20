#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>

namespace engine
{
/**
 * I quattro modi dell'arpeggiatore, nello stesso ordine delle opzioni di `arpMode`: l'indice
 * grezzo dell'APVTS e' direttamente questo enum. Stessa convenzione di engine::VoiceMode.
 */
enum class ArpMode { up = 0, down = 1, upDown = 2, random = 3 };

/** Gli step della griglia. E' lo stesso numero di state::kArpSteps e di MeterFrame::arpStep. */
inline constexpr int kArpSteps = 16;

/**
 * La sequenza di step, a dimensione fissa, pubblicata dal message thread — la gemella di
 * engine::ModSnapshot, e per la stessa ragione.
 *
 * Nel ValueTree gli step vivono come **stringa CSV** dentro il nodo ARP (vedi
 * state::setArpSteps): tokenizzarla vorrebbe dire allocare, e sul thread audio non si alloca.
 * Qui arrivano gia' sedici float, in un blocco di memoria che il motore possiede.
 *
 * Il significato di un valore e' la **velocity dello step**: 0 spegne il passo, >0 e' il livello
 * con cui la nota viene emessa. E' la lettura che il tab Arp gia' disegna (click = 0 / 0.8,
 * rotella = +-0.1, altezza della barra proporzionale), ed e' una scelta nostra: Odin separa
 * on/off da due modulatori generici, Helm e Surge hanno step bipolari di modulazione.
 */
struct ArpSnapshot
{
    float steps[kArpSteps] {};
};

/**
 * Traduce il nodo ARP del ValueTree in uno snapshot. Message thread: qui si tokenizza una
 * stringa e si alloca. `out` viene riscritto per intero, anche quando il nodo e' assente o la
 * stringa e' piu' corta di kArpSteps (gli step mancanti restano a zero, come fa gia'
 * state::toVar verso la WebUI).
 */
void buildArpSnapshot (const juce::ValueTree& arpNode, ArpSnapshot& out);

/**
 * Quanti quarti dura un passo, per ognuna delle **quattro** divisioni di `arpRate`.
 *
 * E' la gemella in C++ di ARP_DIVS (WebUI/src/synth/mapping.ts) e di Label::ArpRate
 * (Source/parameters/ParameterMapping.h): "1/32", "1/16", "1/8", "1/4", cioe' 0.125, 0.25, 0.5
 * e 1 quarto per passo.
 *
 * **Non** e' dsp::syncedRateHz, e non deve diventarlo: quella e' dell'LFO e ha sei divisioni
 * diverse (da 1/16 a 2 battute). Riusarla qui darebbe due tabelle che sembrano la stessa cosa e
 * non lo sono, cioe' il knob dell'arp che mostra "1/8" e suona un quarto.
 */
double arpBeatsPerStep (float raw) noexcept;

/** I sei parametri dell'arp, gia' denormalizzati, piu' il puntatore alla sequenza pubblicata. */
struct ArpConfig
{
    bool on { false };
    ArpMode mode { ArpMode::up };

    /** Grezzo 0..1: la divisione si sceglie con arpBeatsPerStep(), non con una denormalizzazione. */
    float rateRaw { 0.4f };

    float gate01 { 0.6f };   // frazione del passo in cui la nota suona
    int octaves { 1 };       // 1..4
    float swing01 { 0.0f };  // 0..1

    /**
     * La sequenza pubblicata dal message thread, o nullptr.
     *
     * nullptr vuol dire **ogni passo acceso a livello pieno**, non "nessun passo": e' lo stesso
     * criterio di EngineParams::mods, dove nullptr significa "nessuna modulazione" e non "tutto
     * a zero". Un arp acceso senza sequenza pubblicata arpeggia; in produzione il caso non
     * esiste (state::ensureChildren crea sempre il nodo ARP), ma e' la via con cui i test del
     * timing esercitano l'arp senza costruire uno snapshot.
     */
    const ArpSnapshot* steps { nullptr };
};

/** Cio' che l'arp sa della timeline dell'host. Vedi Arpeggiator::process(). */
struct ArpTransport
{
    double bpm { 120.0 };
    double ppqPosition { 0.0 };
    bool isPlaying { false };
};

/**
 * L'arpeggiatore: **riscrive il MidiBuffer** in testa a SynthEngine::process(), prima del ciclo
 * degli eventi.
 *
 * E' il pattern del demo ISC di JUCE (external/JUCE/examples/Plugins/ArpeggiatorPluginDemo.h)
 * ma senza la sua juce::SortedSet, che alloca sul thread audio: i tasti tenuti stanno in un
 * array a capacita' fissa, tenuto ordinato da un'insertion sort.
 *
 * **Perche' riscrivere il MIDI e non chiamare le voci.** Il ciclo di SynthEngine::process()
 * spezza gia' il render a ogni evento MIDI, quindi la precisione campione-esatta e' gratis; e
 * VoiceManager, SynthVoice e tutta la logica di poly/mono/legato/glide restano intatti. Da
 * questo discende il criterio di accettazione piu' importante del lavoro: con `arpOn` falso il
 * buffer non viene toccato — c'e' una riga sola a garantirlo, il primo `return` di process() —
 * e l'uscita e' bit per bit quella di prima che l'arp esistesse.
 *
 * **I due indici (Odin 2).** Un solo contatore di passi ne alimenta due, che si avvolgono
 * separatamente: `n % kArpSteps` sceglie lo step della griglia (cioe' se il passo suona e con
 * che velocity), `n % lunghezzaSequenza` sceglie quale nota. Con lunghezze coprime il pattern
 * si ripete solo dopo il minimo comune multiplo: tre tasti su una griglia di periodo quattro
 * danno dodici passi, non tre e non quattro. Il contatore avanza a **ogni** passo, anche quando
 * lo step e' spento: e' cio' che rende il poliritmo, e non costa niente.
 *
 * **Lo swing.** `T_pari = T0 (1 + s/2)`, `T_dispari = T0 (1 - s/2)`, con la parita' di `n`: la
 * somma della coppia resta `2 T0`, quindi lo swing sposta il secondo ottavo senza cambiare il
 * tempo. Surge usa un flip-flop, che perde il passo giusto appena qualcosa lo desincronizza.
 *
 * **Le note appese.** La lista delle note **in suono** e' completamente disaccoppiata da quella
 * dei tasti tenuti: tiene il numero MIDI *emesso* (che con le ottave non e' quello premuto) e un
 * timer di gate per nota. E' strutturale, non una precauzione — senza, cambiare accordo a meta'
 * pattern lascerebbe appesa ogni nota la cui origine e' stata rilasciata.
 *
 * **Nessuna allocazione sul percorso audio**, con un solo caveat onesto: il buffer di uscita e'
 * un membro, e dopo lo swap con quello dell'host ci si ritrova in mano il suo — se e' piu'
 * piccolo della riserva, il primo ensureSize() dopo lo swap lo fa crescere. Gli host riusano lo
 * stesso MidiBuffer da un blocco all'altro, quindi dopo i primi due blocchi la richiesta non si
 * ripresenta. Da li' in poi non c'e' piu' una sola allocazione.
 */
class Arpeggiator
{
public:
    /** Quanti tasti l'arp segue insieme. Oltre, i nuovi vengono ignorati finche' non si libera
        un posto: un arpeggio a diciassette note non e' un caso musicale. */
    static constexpr int kMaxHeldKeys = 16;

    /** Quante note emesse possono essere in suono insieme. Con gate <= 100 % ne basterebbe una,
        ma il gate e' clampato e un cambio di divisione a caldo puo' accavallare: il margine
        rende impossibile che una nota resti senza il suo note-off. */
    static constexpr int kMaxSounding = 16;

    /** Byte riservati al buffer di uscita: ~340 eventi a tre byte. Vedi il commento della classe. */
    static constexpr size_t kMidiReserveBytes = 2048;

    void prepare (double sampleRate) noexcept;

    /** Azzera contatori, tasti e note in suono **senza** emettere niente: chi chiama ha gia'
        ammutolito le voci (SynthEngine::reset(), il ramo di bypass). */
    void reset() noexcept;

    /**
     * Riscrive `midi` in luogo: i note on/off in ingresso vengono consumati e sostituiti dai
     * passi dell'arpeggio, tutto il resto (CC, pitch bend, all-notes-off) passa intatto e alla
     * stessa posizione.
     *
     * **Il transport.** Con `isPlaying` falso l'arp gira libero: un contatore di campioni suo,
     * riavviato quando il primo tasto scende — cosi' la prima nota suona sotto il dito e non al
     * prossimo confine di una griglia che nessuno vede. Con `isPlaying` vero i confini vengono
     * dal PPQ dell'host e si riancorano a **ogni blocco**: un salto del cursore, un loop o un
     * cambio di tempo riagganciano l'arp alla timeline invece di farlo derivare. Il dedup su
     * `lastFiredStep_` e' cio' che impedisce di riemettere lo stesso passo a ogni riancoraggio.
     *
     * `numSamples` e' la lunghezza del blocco: gli eventi oltre quel confine vengono clampati.
     */
    void process (juce::MidiBuffer& midi, int numSamples, const ArpConfig& cfg,
                  const ArpTransport& transport) noexcept;

    /** L'indice di griglia dell'ultimo passo emesso, 0..15. Zero quando l'arp e' fermo.
        E' cio' che MeterFrame::arpStep pubblica e che il tab Arp usa per il riquadro. */
    int currentStep() const noexcept { return currentStep_; }

private:
    struct HeldKey
    {
        int note { 0 };
        int channel { 1 };

        /**
         * Se a tenere questo tasto sia il pedale invece del dito.
         *
         * Il latch non e' una seconda lista: e' un flag sulla stessa, perche' per tutto il resto
         * dell'arpeggiatore — la sequenza, le ottave, l'ordinamento per numero di nota — un
         * tasto tenuto dal pedale e' un tasto premuto e basta. La distinzione serve in un punto
         * solo, quando il pedale si alza e vanno via *quelli e non gli altri*.
         */
        bool latched { false };
    };

    struct Sounding
    {
        int note { 0 };
        int channel { 1 };
        double offPos { 0.0 }; // posizione del note-off, relativa all'inizio del blocco
        bool active { false };
    };

    /** Emette tutto cio' che cade prima di `limit`: prima i gate-off, poi i passi. */
    void emitUntil (int limit, const ArpConfig& cfg) noexcept;

    /** Il passo `nextStepIndex_`, al campione `s`. Riprogramma il successivo. */
    void fireStep (int s, const ArpConfig& cfg) noexcept;

    void addHeld (int note, int channel) noexcept;
    void removeHeld (int note) noexcept;

    /** Toglie dalla lista ogni tasto che teneva il pedale, lasciando quelli ancora sotto le dita. */
    void dropLatchedKeys() noexcept;

    /** Il note-off di ogni nota in suono con quel numero, al campione `s`. E' quel che impedisce
        il bug di Odin: con gate pieno e nota ripetuta, il note-on arrivava **prima** del
        note-off e spegneva la voce appena avviata. */
    void killSounding (int note, int s) noexcept;
    void flushSounding (int s) noexcept;
    void addSounding (int note, int channel, double offPos, int s) noexcept;

    /** La lunghezza della sequenza di note: tasti x ottave, raddoppiata e specchiata in UpDn. */
    int sequenceLength (ArpMode mode, int expanded) const noexcept;

    /** L'indice espanso (0..tasti x ottave) del passo `index` della sequenza, per modo. */
    int expandedIndexFor (ArpMode mode, int index, int expanded) noexcept;

    /** Durata in campioni del passo `n`: lunga se pari, corta se dispari. Vedi lo swing. */
    double stepDurationSamples (std::int64_t n) const noexcept;

    /** Posizione (relativa all'inizio del blocco) del confine del passo `n`, in sync. */
    double positionOfStep (std::int64_t n) const noexcept;

    double sampleRate_ { 48000.0 };

    /** Il buffer che si costruisce e poi si scambia con quello dell'host. Vedi la classe. */
    juce::MidiBuffer output_;

    HeldKey held_[kMaxHeldKeys] {};
    int heldCount_ { 0 };

    Sounding sounding_[kMaxSounding] {};

    /** Falso quando l'arp non ha niente da spegnere ne' da ricordare: e' la condizione che fa
        uscire process() **prima** di toccare il buffer. */
    bool active_ { false };

    /**
     * Il pedale di sustain (CC 64) visto dall'arpeggiatore, che con l'arp acceso lo interpreta
     * come un **latch**: a pedale giu' le dita possono alzarsi e la sequenza continua a girare
     * sull'accordo. E' cio' che fa la maggior parte degli hardware, ed e' l'unica lettura che
     * dia un risultato suonabile — tenere invece le note che l'arp *emette* le accumulerebbe in
     * un cluster che cresce a ogni passo.
     *
     * Il CC 64 viene comunque inoltrato a valle dal ramo generale dei controller, e non e' uno
     * spreco: e' quello che permette a SynthEngine di sapere che il pedale e' giu' anche mentre
     * l'arp se ne sta occupando, e quindi di non lasciare voci appese se l'arp viene spento con
     * il piede ancora sul pedale.
     */
    bool sustain_ { false };

    /** Il contatore dei passi, monotono. In sync viene ricalcolato dal PPQ a ogni blocco. */
    std::int64_t nextStepIndex_ { 0 };

    /** L'ultimo passo emesso, per non riemetterlo quando il riancoraggio ricade dentro di lui. */
    std::int64_t lastFiredStep_ { INT64_MIN };

    /** Posizione del prossimo confine, relativa all'inizio del blocco. Frazionaria di proposito:
        e' cio' che tiene i confini senza deriva quando i campioni per passo non sono interi. */
    double nextStepPos_ { 0.0 };

    // Ricalcolati a ogni blocco da process(): durate dei due passi della coppia di swing, e la
    // geometria della griglia in quarti per il riancoraggio sul PPQ.
    double evenStepSamples_ { 0.0 };
    double oddStepSamples_ { 0.0 };
    double pairBeats_ { 1.0 };
    double longBeats_ { 0.5 };
    double samplesPerBeat_ { 24000.0 };
    double blockStartPpq_ { 0.0 };
    bool synced_ { false };

    int currentStep_ { 0 };

    /** xorshift32 per il modo Random: deterministico, senza stato globale e senza costruire uno
        std::random_device o uno std::shuffle sul thread audio (sono i due difetti di Odin). */
    std::uint32_t randomState_ { 0x9e3779b9u };

    std::uint32_t nextRandom() noexcept;
};
} // namespace engine
