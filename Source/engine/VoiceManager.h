#pragma once

#include "dsp/MipTable.h"
#include "engine/EngineParams.h"
#include "engine/NoteMask.h"
#include "engine/SynthVoice.h"

#include <array>
#include <cstddef>

namespace engine
{
/** Fixed voice pool — no heap activity on the audio thread. */
class VoiceManager
{
public:
    /**
     * Quante voci possono **suonare insieme**: la polifonia dichiarata, quella che si esaurisce
     * e fa scattare il furto.
     *
     * Non e' piu' la dimensione del pool, ed e' una distinzione che prima non esisteva perche'
     * i due numeri coincidevano. Vedi poolSize qui sotto.
     */
    static constexpr int maxVoices = 16;

    /**
     * Gli slot che il pool ha **in piu'** della polifonia, riservati alle voci rubate che stanno
     * dissolvendo.
     *
     * A cosa serve il margine: la nota nuova deve prendere una voce davvero libera, altrimenti
     * la dissolvenza non ha dove avvenire e si torna al kill(). E' il pattern di Gin ("extra
     * voices should handle this"), di Surge XT (`margin = 3`) e di Vital (kMaxPolyphony 33
     * contro kMaxActivePolyphony 32).
     *
     * Tre e non uno. Il numero che conta e' quanti furti possono cadere dentro una finestra di
     * kStealFadeSeconds (8 ms): con uno solo di margine bastano **due** note in eccesso a 8 ms
     * di distanza — un accordo suonato non perfettamente insieme, o due sedicesimi a 250 BPM —
     * per esaurirlo, e il secondo furto ricadrebbe sul percorso degradato. Con tre, la finestra
     * regge una nota in eccesso ogni 2.7 ms, che e' piu' veloce di qualunque cosa esca da una
     * tastiera e copre anche il cambio d'accordo, dove le note nuove arrivano tutte nello stesso
     * evento MIDI. Vital si permette 1 perche' il suo pool e' di 32: il margine relativo e'
     * quello che conta, e 3 su 16 e' quasi il doppio di 1 su 32.
     *
     * Il costo e' limitato e prepagato: tre SynthVoice in piu' allocate una volta in prepare(),
     * e al massimo tre render() in piu' per fetta, per non piu' di 8 ms dopo un furto.
     */
    static constexpr int stealFadeMargin = 3;

    /** Gli slot che il pool possiede davvero. Piu' di `maxVoices` di proposito. */
    static constexpr int poolSize = maxVoices + stealFadeMargin;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void noteOn (int midiNote, float velocity) noexcept;
    void noteOff (int midiNote) noexcept;
    void allNotesOff() noexcept;
    void allSoundOff() noexcept;

    /**
     * Il pedale di sustain (CC 64), gia' ridotto a un booleano da chi legge il MIDI.
     *
     * A pedale giu' un note-off non viene *tradotto* in qualcos'altro: viene **differito**.
     * La nota resta in `held_` esattamente com'era e la voce non viene toccata; l'unica traccia
     * e' un bit in `sustained_`. Alzare il pedale riapplica i note-off differiti, e da li' in
     * poi sono note-off normali che passano per la stessa `noteOff()` di sempre — compresi il
     * ritorno al tasto precedente in Mono e il ritrigger dell'inviluppo.
     *
     * Differire invece di tradurre e' cio' che tiene i due modi monofonici corretti senza una
     * riga di codice in piu': `held_` continua a descrivere "cosa deve suonare", che a pedale
     * giu' non coincide piu' con "quali tasti sono fisicamente premuti", ed e' la prima delle
     * due che monoNoteOn/monoNoteOff hanno sempre voluto sapere.
     *
     * Chiamarla con il valore che ha gia' non fa niente: un CC 64 ripetuto — che e' quello che
     * mandano le pedaliere continue a ogni passo della corsa — non deve rilasciare niente.
     */
    void setSustainPedal (bool down) noexcept;

    void render (float* outL, float* outR, int numSamples) noexcept;

    /** Propaga la tavola attiva a tutte le voci. Chiamata dal thread audio. */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /** Propaga i parametri del blocco a tutte le voci, attive o no. Una volta per blocco. */
    void setParams (const EngineParams& p) noexcept;

    /**
     * Propaga il solo livello dell'LFO libero. Una volta per **sotto-fetta** di controllo.
     *
     * Questo metodo prima non c'era, ed era una decisione motivata: il livello dell'LFO libero
     * arriva gia' alle voci dentro EngineParams::globalLfoLevel, e un secondo canale per lo
     * stesso dato e' una via in piu' da tenere sincronizzata. Era la scelta giusta finche' le
     * fette per blocco erano quattro. Con le sotto-fette da 32 campioni sono sedici, e
     * ripubblicare l'intera EngineParams per far arrivare un float costava 240 chiamate a
     * SynthVoice::setParams in piu' per blocco (16 x 16 contro 4 x 16 di prima) — ognuna con
     * tre exp() per i coefficienti dell'inviluppo e una riscorsa della lista delle route.
     *
     * Il dato resta uno solo: SynthEngine pubblica EngineParams una volta per blocco, come
     * prima del tasso di controllo fisso, e da li' in poi muove solo questo.
     */
    void setGlobalLfoLevel (float level) noexcept;

    /**
     * I livelli delle quattro sorgenti **per voce**, tutti letti dalla stessa voce.
     *
     * Che siano della stessa voce e' il punto, non un dettaglio di implementazione: l'anello di
     * un knob con due route — poniamo env e vel sullo stesso cutoff — somma i due livelli, e
     * sommare l'inviluppo di una nota alla velocity di un'altra mostrerebbe un valore che
     * nessuna delle due voci sta suonando. Per questo si sceglie la voce una volta sola qui, e
     * non quattro volte da quattro accessori separati.
     *
     * `mw` non c'e' perche' non e' per voce: e' il CC 1, e ce l'ha SynthEngine.
     */
    struct SourceLevels
    {
        float lfo  { 0.0f };
        float env  { 0.0f };
        float env2 { 0.0f };
        float vel  { 0.0f };
    };

    /**
     * I livelli della **prima voce attiva** nell'ordine del pool, tutti zero se non suona niente.
     *
     * "La prima" e non "l'ultima suonata": e' la scelta che getLfoLevel() faceva gia'. Un
     * ordine di eta' fra le voci adesso **esiste** — SynthVoice::startOrder_, che il furto usa
     * per sapere quale voce suoni da piu' tempo — quindi l'argomento che teneva ferma questa
     * scelta (costava un contatore in piu' sul percorso audio, per un meter) e' caduto. Resta
     * l'altro, che non dipendeva dal costo: su un accordo tenuto la piu' recente sarebbe
     * probabilmente piu' vicina a quello che la mano ha appena fatto, ma su una singola nota —
     * il caso in cui si guarda un anello per capire cosa fa una route — le due scelte
     * coincidono. Cambiarla sarebbe una decisione sul meter, da prendere guardando il meter.
     */
    SourceLevels getSourceLevels() const noexcept;

    /** Il livello dell'LFO della prima voce attiva, per il meter. Zero se non suona niente. */
    float getLfoLevel() const noexcept { return getSourceLevels().lfo; }

    /** Le voci che occupano un posto nella polifonia: attive e non in dissolvenza da furto.
        Non supera mai maxVoices — e' proprio l'invariante che noteOn() mantiene. */
    int countActive() const noexcept;

    /** Tutto cio' che sta producendo segnale, code dei furti comprese. Puo' arrivare a
        poolSize. Serve per verificare che nulla resti appeso. */
    int countSounding() const noexcept;

    /** Lettura di uno slot del pool, per poterne osservare lo stato dall'esterno (i test).
        Indice fuori range: la voce zero, che e' sempre un oggetto valido. */
    const SynthVoice& getVoice (int index) const noexcept;

    /** Quante note sono fisicamente premute in questo momento, secondo la lista dei tasti
        tenuti. In Poly non viene aggiornata: e' un dato che serve solo ai due modi monofonici. */
    int countHeld() const noexcept { return held_.count; }

private:
    /**
     * I tasti premuti, nell'ordine in cui sono stati premuti, a capacita' fissa.
     *
     * Serve ai due modi monofonici e a nient'altro: quando si lascia il tasto che sta suonando
     * e un altro e' ancora giu', la voce deve tornare su quest'ultimo invece di spegnersi. E'
     * il modello di Odin 2 (`PluginProcessorMidi.cpp`), che e' anche quello con cui una mano
     * suona: l'ultimo premuto e' quello che si sente, e lasciandolo si torna al precedente.
     *
     * Array e contatore, niente std::vector e niente std::deque: questa struttura viene scritta
     * dal thread audio a ogni nota, e un'allocazione li' dentro e' vietata. Sedici e' la stessa
     * capacita' della polifonia — non ha senso poter tenere piu' tasti di quante note il
     * sintetizzatore sappia suonare — e a tastiera piena il piu' vecchio viene scartato invece
     * di far crescere l'array: e' il tasto di cui l'esecutore si e' gia' dimenticato.
     *
     * La velocity viaggia insieme alla nota perche' il ritorno al tasto precedente, in Mono,
     * ritriggera l'inviluppo: senza, ripartirebbe con la velocity della nota appena lasciata,
     * cioe' di un tasto che non e' piu' premuto.
     */
    struct HeldNotes
    {
        struct Key { int note { -1 }; float velocity { 0.0f }; };

        std::array<Key, (size_t) maxVoices> keys {};
        int count { 0 };

        /** Ribattere un tasto gia' premuto lo **sposta** in cima invece di duplicarlo: la lista
            deve restare un insieme, altrimenti un note-off solo ne lascerebbe dentro una copia
            e la voce tornerebbe su una nota che nessuno sta piu' tenendo. */
        void add (int note, float velocity) noexcept
        {
            remove (note);

            if (count == maxVoices)
            {
                for (int i = 1; i < count; ++i)
                    keys[(size_t) (i - 1)] = keys[(size_t) i];

                --count;
            }

            keys[(size_t) count++] = { note, velocity };
        }

        /** Toglie il tasto, se c'e'. Vero se c'era. */
        bool remove (int note) noexcept
        {
            for (int i = 0; i < count; ++i)
                if (keys[(size_t) i].note == note)
                {
                    for (int j = i + 1; j < count; ++j)
                        keys[(size_t) (j - 1)] = keys[(size_t) j];

                    --count;
                    return true;
                }

            return false;
        }

        /** L'ultimo premuto: quello che si sente. `note` vale -1 con la lista vuota. */
        Key last() const noexcept { return count > 0 ? keys[(size_t) (count - 1)] : Key {}; }

        bool contains (int note) const noexcept
        {
            for (int i = 0; i < count; ++i)
                if (keys[(size_t) i].note == note)
                    return true;

            return false;
        }

        void clear() noexcept { count = 0; }
    };

    /** Il ramo monofonico di noteOn/noteOff: una voce sola, la lista dei tasti, e il ritrigger
        dell'inviluppo che c'e' in Mono e non in Legato. */
    void monoNoteOn (int midiNote, float velocity) noexcept;
    void monoNoteOff (int midiNote) noexcept;

    /** Riapplica i note-off che il pedale teneva fermi, dal piu' vecchio al piu' recente. */
    void releaseSustainedNotes() noexcept;

    /** Lettura e scrittura di un bit di `sustained_` (vedi engine::NoteMask). */
    bool sustainedBit (int midiNote) const noexcept { return sustained_.test (midiNote); }
    void setSustainedBit (int midiNote, bool on) noexcept { sustained_.set (midiNote, on); }
    void clearSustainedBits() noexcept { sustained_.clear(); }

    /**
     * Se la nota che sta per partire debba scivolare da `lastStartedNote_` o cominciare alla
     * propria altezza. Va chiamata **prima** di aggiungere la nota nuova a `held_`.
     *
     * E' il `kPortamentoForce` di Vital con force spento, che e' il suo default: il glide si
     * applica solo fra note **legate**, non a ogni nota. Le tre condizioni sono tutte
     * necessarie, e ognuna toglie di mezzo un caso che altrimenti suonerebbe sbagliato.
     *
     * 1. `lastStartedNote_ >= 0` — deve esserci una nota da cui partire. La prima nota dopo un
     *    reset non ce l'ha.
     *
     * 2. `held_.count > 0` — un tasto dev'essere gia' premuto. Senza questa, una nota suonata
     *    dieci secondi dopo l'ultima scivolerebbe comunque da quella: un glissando che arriva
     *    dal nulla, in mezzo a un silenzio, su una frase nuova che non c'entra niente con la
     *    precedente. E' la condizione che descrive il *legato* come lo intende una mano.
     *
     * 3. `soundedSinceLastNoteOn_` — la nota precedente dev'essere stata **suonata**, cioe'
     *    dev'essere passato almeno un campione dal suo note-on. Questa e' la meta' meno ovvia,
     *    e senza di lei la seconda non basta: le note di un accordo arrivano come note-on
     *    consecutivi senza nessun render in mezzo, quindi quando la seconda entra la prima
     *    risulta gia' "premuta" e l'accordo partirebbe smerdato — ogni nota che scivola dalla
     *    precedente, con un ritardo d'intonazione che nessuno ha chiesto. Un accordo non e' una
     *    sequenza di note legate: e' un evento solo, e un glide da una nota che non ha ancora
     *    emesso un campione e' un glide da qualcosa che non si e' sentito.
     *
     * L'alternativa — glide **sempre**, anche dopo il silenzio e dentro un accordo — esiste ed
     * e' un `bool glideForce` in EngineParams piu' un `||` qui dentro. Non e' esposta adesso
     * perche' vorrebbe dire un parametro in piu' in parameters.json, quindi un controllo in piu'
     * nella UI e un giro di generazione, per un'opzione che in Vital e' spenta di default e che
     * nessuno ci ha chiesto: il caso che serve davvero e' il glide fra note legate, e quello c'e'.
     */
    bool canGlideFromLastNote() const noexcept
    {
        return lastStartedNote_ >= 0 && held_.count > 0 && soundedSinceLastNoteOn_;
    }

    /** Registra che una nota e' partita: da qui in poi si riparte a contare i campioni resi. */
    void noteStarted (int midiNote) noexcept
    {
        lastStartedNote_ = midiNote;
        soundedSinceLastNoteOn_ = false;
    }

    /** La voce che i modi monofonici stanno usando, o nullptr se non ce n'e' piu' una viva. */
    SynthVoice* monoVoice() noexcept;

    SynthVoice* findFreeVoice() noexcept;
    SynthVoice* findVoiceForNote (int midiNote) noexcept;

    /**
     * Quale voce sacrificare quando la polifonia e' piena: la piu' silenziosa fra quelle in
     * release e, se nessuna e' in release, la piu' vecchia ancora premuta.
     *
     * L'ordine delle due preferenze e' quello di Surge XT (`softkillVoice`) e di Vital
     * (`grabVoice`: libera, poi rilasciata, poi tenuta), e la ragione e' che una voce in release
     * e' gia' sulla strada dell'uscita: toglierla anticipa una fine che l'esecutore ha gia'
     * chiesto. Dentro quel gruppo si prende la piu' silenziosa perche' e' quella la cui coda
     * manca di meno, non la piu' vecchia: due note rilasciate insieme con inviluppi diversi non
     * sono equivalenti, e l'eta' non lo sa.
     */
    SynthVoice* chooseVictim() noexcept;

    /**
     * L'uscita di sicurezza: se il margine e' esaurito — piu' di stealFadeMargin furti dentro
     * una sola finestra di dissolvenza — si riusa la coda gia' piu' avanti nella sua, azzerandola.
     *
     * E' il solo punto in cui e' rimasto un kill() sul percorso del furto, ed e' anche il punto
     * in cui costa meno: fra le code in corso si sceglie quella al livello piu' basso. Surge fa
     * la stessa scelta — le uniche voci che dealloca davvero sono quelle gia' in uberrelease.
     */
    SynthVoice* reclaimQuietestFading() noexcept;

    std::array<SynthVoice, poolSize> voices_ {};

    /**
     * Il numero che si da' alla prossima nota, per sapere quale voce sia la piu' vecchia.
     *
     * Sta qui e non dentro SynthVoice perche' e' un ordinamento fra voci: contatori separati non
     * sarebbero confrontabili. A 64 bit non torna mai indietro (vedi SynthVoice::startOrder_).
     */
    unsigned long long nextStartOrder_ { 1 };

    // --- glide e modi di voce ---

    /** Poly finche' qualcuno non dice il contrario: setParams() lo aggiorna una volta per blocco. */
    VoiceMode mode_ { VoiceMode::poly };

    HeldNotes held_ {};

    /**
     * Lo slot del pool che i modi monofonici stanno usando, -1 se nessuno.
     *
     * Un indice e non un puntatore: gli slot non si spostano mai, ma un indice si confronta con
     * -1 senza che ci sia un oggetto a cui puntare, e sopravvive a un reset() del pool senza
     * restare appeso.
     */
    int monoVoiceIndex_ { -1 };

    /**
     * L'ultima nota **avviata**, per il glide in Poly: una voce nuova scivola da li' alla
     * propria, come fa Vital.
     *
     * Sta qui e non dentro SynthVoice per la stessa ragione di nextStartOrder_: e' un dato che
     * mette in relazione due note diverse, e quindi due voci diverse, e una voce non puo'
     * conoscerlo da sola. Vale -1 finche' non e' stata suonata nessuna nota — la prima nota
     * dopo un reset() non ha da dove scivolare e parte alla propria altezza.
     */
    int lastStartedNote_ { -1 };

    /**
     * Se sia stato reso almeno un campione dall'ultimo note-on. Vedi canGlideFromLastNote(): e'
     * cio' che distingue una nota legata a quella prima da una nota dello stesso accordo.
     *
     * Un bool e non un contatore: la domanda e' "e' passato del tempo?", non "quanto". E lo
     * alza render(), cioe' l'unico posto in cui del tempo passa davvero — non setParams(), che
     * gira una volta per blocco anche se poi non si rende niente.
     */
    bool soundedSinceLastNoteOn_ { false };

    // --- pedale di sustain ---

    /** Se il pedale risulta giu'. Campo del solo thread audio, come modWheel_ in SynthEngine. */
    bool sustainPedal_ { false };

    /**
     * Le note il cui note-off e' stato differito dal pedale, un bit per nota MIDI.
     *
     * Una maschera e non una lista: l'insieme e' senza ordine per costruzione — l'ordine in cui i
     * note-off vanno riapplicati non e' quello in cui sono arrivati, e' quello di `held_` (vedi
     * releaseSustainedNotes()). Niente atomico: ci scrive e ci legge solo il thread audio.
     */
    NoteMask sustained_;
};
} // namespace engine
