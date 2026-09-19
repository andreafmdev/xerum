#pragma once

#include "dsp/MipTable.h"
#include "engine/EngineParams.h"
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

private:
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
};
} // namespace engine
