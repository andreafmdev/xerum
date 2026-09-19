#pragma once

#include "engine/EngineParams.h"
#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace engine
{
struct EngineSpec
{
    double sampleRate { 44100.0 };
    int maximumBlockSize { 512 };
    int numChannels { 2 };
};

/** Real-time synth core. Owned by PluginProcessor; no UI / allocations here. */
class SynthEngine
{
public:
    /**
     * Lunghezza massima di una fetta di controllo, in campioni: il render si spezza qui dentro
     * a prescindere da quanto lungo sia il blocco che l'host consegna.
     *
     * E' la garanzia che il tasso di modulazione sia una proprieta' del sintetizzatore e non
     * della scheda audio. Prima le fette erano delimitate solo dagli eventi MIDI, quindi la
     * modulazione si rivalutava 375 volte al secondo con buffer da 128 e 47 con buffer da 1024:
     * a 47 Hz un LFO a 8 Hz ha meno di sei punti per ciclo, e la stessa patch cambiava suono
     * spostando un cursore nelle preferenze dell'host. Con 32 campioni il tasso e' 1500 Hz a
     * 48 kHz, qualunque sia il buffer.
     *
     * Trentadue e non sessantaquattro, e il numero viene da una misura, non da un'intuizione: il
     * costo **non** e' lineare nel numero di sotto-fette, perche' ognuna paga un applyModulation()
     * (sette denormalizzazioni), un updateCutoff() (std::exp2) e un setCutoffHz() (std::tan) per
     * voce. In Release, Apple Silicon, sul caso peggiore che il motore sappia produrre — 16 voci,
     * unison 8, una route lfo -> cutoff, blocchi da 512 a 48 kHz, cioe' 10.67 ms di budget:
     *
     *     fetta      tasso      us/blocco    % del budget
     *     nessuna      94 Hz        482.0        4.52 %   (delimitata solo dagli eventi MIDI)
     *     128         375 Hz        485.7        4.55 %
     *      64         750 Hz        497.2        4.66 %
     *      32        1500 Hz        512.0        4.80 %
     *
     * Ogni riga e' il minimo su piu' compilazioni, ognuna il migliore di sette passate da 2000
     * blocchi: la dispersione fra una compilazione e l'altra e' del 2 %, quindi il primo decimale
     * e' rumore mentre il confronto fra le righe non lo e'.
     *
     * Quadruplicare il tasso di controllo costa 0.28 punti percentuali del budget — il 6 % in piu'
     * di un motore che ne usa il quattro e mezzo — e si compra con lo spicciolo. E non e' una
     * differenza cosmetica: fra 32 e 64 campioni di fetta la stessa patch modulata esce con una
     * differenza di -19 dB RMS, quindi la risoluzione in piu' sta facendo un lavoro che si sente.
     * Il numero e' lo stesso `BLOCK_SIZE` di Surge XT, per la stessa ragione; Vital si ferma a
     * 128 (`kMaxBufferSize`).
     *
     * Pubblica perche' e' parte del contratto osservabile: i test dell'invarianza rispetto al
     * buffer dell'host allineano gli eventi MIDI a questa griglia, ed e' l'unico modo che hanno
     * di dire perche' si aspettano un'uguaglianza esatta e non approssimata.
     */
    static constexpr int kControlBlockSamples = 32;

    void prepare (const EngineSpec& spec) noexcept;
    void reset() noexcept;

    /** Apply MIDI for this block then render. Buffers must be cleared by caller. */
    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept;

    void setMasterGainLinear (float gain) noexcept;

    /** Propaga i parametri del blocco alle voci. Thread audio, una volta per blocco. */
    void setParams (const EngineParams& p) noexcept;

    /** Propaga subito la tavola attiva alle voci. Non real-time safe quanto a chi la chiama:
        usarla solo quando il thread audio non gira ancora (prepareToPlay). */
    void setWavetable (const dsp::MipTable* table) noexcept;

    /**
     * Pubblica una nuova tavola da applicare alla prossima process(). Lock-free, chiamabile
     * da qualunque thread (il message thread, quando wtIndex cambia a runtime): SynthVoice::
     * setWavetable muta più campi non atomici, farlo da un thread diverso da quello audio
     * mentre render() legge sarebbe una race. Qui si pubblica solo il puntatore; l'applicazione
     * vera avviene dentro process(), sul thread audio.
     */
    void setPendingWavetable (const dsp::MipTable* table) noexcept;

    /**
     * Pubblica una nuova lista di assegnazioni. Chiamabile da qualunque thread: si copia lo
     * snapshot in uno slot libero dell'anello e si pubblica solo il puntatore.
     *
     * Anello di quattro e non doppio buffer: le mod cambiano molto piu' spesso di una wavetable
     * (l'utente trascina uno slider di depth), e con due soli slot il message thread potrebbe
     * riscrivere quello che il thread audio sta leggendo. Con quattro dovrebbe pubblicare
     * quattro volte dentro un singolo blocco audio per raggiungere il lettore: nella pratica
     * impossibile, e comunque il danno sarebbe una modulazione sbagliata per un blocco, mai
     * una lettura di memoria liberata — gli slot vivono quanto il motore.
     */
    void setMods (const engine::ModSnapshot& snapshot) noexcept;

    /** Il livello corrente dell'LFO, per il meter dell'editor. Bipolare -1..1, istantaneo. */
    float getLfoLevel() const noexcept { return lfoLevel_.load (std::memory_order_relaxed); }

    /**
     * Il **picco del blocco appena reso** dell'inviluppo d'ampiezza della voce rappresentativa.
     *
     * Picco e non valore di fine blocco, e la ragione e' il lettore: bridge::MeterChannel
     * campiona a 30 Hz, un frame ogni 33 ms, mentre l'attacco piu' corto che il motore sa fare
     * dura qualche decina di campioni — meno di un millisecondo. Pubblicando l'istantaneo,
     * l'anello del knob non vedrebbe quasi mai il punto piu' alto raggiunto da cio' che modula:
     * su un pluck, il solo momento che conta. E' lo stesso ragionamento — e lo stesso
     * meccanismo — dei picchi audio inPeak/outPeak, che il lettore azzera con exchange(0).
     *
     * Il massimo si accumula a **tasso di controllo**, non una volta per blocco: renderControlSlices()
     * campiona dopo ogni sotto-fetta da kControlBlockSamples, cioe' 1500 Hz a 48 kHz. Prenderlo
     * solo a fine blocco avrebbe spostato il problema invece di risolverlo — con buffer da 512 un
     * attacco di mezzo millisecondo e' finito e gia' decaduto prima che qualcuno guardi.
     *
     * Il fondo della risoluzione e' quindi il tasso di controllo, non il frame del meter: un
     * transitorio piu' corto di kControlBlockSamples passa fra due campionamenti e non si vede.
     * Scendere sotto vorrebbe dire guardare l'inviluppo campione per campione dentro
     * SynthVoice::render — costo sul percorso audio per un'indicazione sullo schermo.
     *
     * Vale per env e env2. `vel` non ha transitori dentro un blocco (e' costante per tutta la
     * nota) ma segue la stessa regola per una ragione sua: una nota piu' corta di un frame — uno
     * staccato, un passo d'arpeggio — con l'istantaneo non comparirebbe affatto.
     */
    float getEnvLevel() const noexcept { return envPeak_.load (std::memory_order_relaxed); }
    float getEnv2Level() const noexcept { return env2Peak_.load (std::memory_order_relaxed); }
    float getVelocityLevel() const noexcept { return velPeak_.load (std::memory_order_relaxed); }

    /** La posizione del mod wheel (CC 1), 0..1. Istantanea: e' una posizione, non un transitorio. */
    float getModWheelLevel() const noexcept { return modWheelLevel_.load (std::memory_order_relaxed); }

private:
    void handleMidiEvent (const juce::MidiMessage& message) noexcept;

    /**
     * Rende `numSamples` campioni spezzandoli in sotto-fette di al piu' kControlBlockSamples,
     * ciascuna con la propria valutazione della modulazione e il proprio avanzamento dell'LFO
     * libero. Il numero di sotto-fette e' al piu' ceil(numSamples / 32) + 1: limitato, noto, e
     * senza una sola struttura dinamica di mezzo.
     *
     * `gridPhase` e' la posizione del primo campione **dentro il buffer dell'host**, e serve a
     * tenere i confini di controllo su una griglia ancorata all'inizio del buffer invece che al
     * punto in cui l'ultimo evento MIDI ha interrotto il render. Senza, un note-on a un campione
     * qualunque sfasava la griglia di li' in avanti, e la sfasatura dipendeva da dove cominciava
     * il blocco dell'host: due buffer diversi valutavano la modulazione a campioni diversi.
     * Finche' lo smoother filtrava anche la modulazione la differenza restava all'uno per cento,
     * ora che non la filtra piu' arriverebbe al tre. La prima sotto-fetta dopo un evento e'
     * quindi piu' corta, quanto basta a rimettersi in griglia.
     */
    void renderControlSlices (float* left, float* right, int numSamples, int gridPhase) noexcept;

    /** Deposita i cinque livelli negli atomici che l'editor legge. `mw` viene da modWheel_:
        e' l'unica sorgente che il motore conosce da se', senza passare dalle voci. */
    void publishSourceLevels (float lfo, float env, float env2, float vel) noexcept;

    VoiceManager voices_;
    EngineSpec spec_ {};
    EngineParams params_ {};
    float masterGain_ { 1.0f };
    float previousMasterGain_ { 1.0f }; // per rampare il gain fra un blocco e l'altro, vedi process()
    std::atomic<const dsp::MipTable*> pendingWavetable_ { nullptr };

    // --- modulazione ---

    /** L'LFO che gira anche senza note: e' cio' che rende "libera" la fase condivisa quando
        lretrig e' falso. Avanza in renderControlSlices(), una sotto-fetta per volta: fuori da
        quel ciclo sarebbe l'unica sorgente rimasta a tasso di blocco, e i due percorsi — LFO
        libero e LFO per voce — divergerebbero al cambiare del buffer dell'host. */
    dsp::Lfo globalLfo_;

    ModSnapshot modRing_[4] {};
    std::atomic<int> modWriteSlot_ { 0 };
    std::atomic<const ModSnapshot*> activeMods_ { nullptr };

    std::atomic<float> lfoLevel_ { 0.0f };
    float modWheel_ { 0.0f }; // CC 1, solo thread audio

    // --- telemetria per il meter ---------------------------------------------------------
    // Sola lettura verso l'editor: niente qui dentro torna nel percorso del segnale. Gli
    // atomici si scrivono una volta per blocco con store(relaxed), gli accumulatori sono
    // normali float toccati solo dal thread audio.

    std::atomic<float> envPeak_ { 0.0f };
    std::atomic<float> env2Peak_ { 0.0f };
    std::atomic<float> velPeak_ { 0.0f };
    std::atomic<float> modWheelLevel_ { 0.0f };

    /** I massimi accumulati sulle sotto-fette del blocco in corso. Azzerati da process(). */
    float blockEnvPeak_ { 0.0f };
    float blockEnv2Peak_ { 0.0f };
    float blockVelPeak_ { 0.0f };
};
} // namespace engine
