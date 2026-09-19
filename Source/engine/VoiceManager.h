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
    static constexpr int maxVoices = 16;

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
     * "La prima" e non "l'ultima suonata": e' la scelta che getLfoLevel() faceva gia', e
     * cambiarla adesso vorrebbe dire dare un ordine di eta' alle voci — un contatore in piu' da
     * tenere in SynthVoice, mosso da noteOn/retrigger/kill, cioe' stato nuovo sul percorso audio
     * per un meter. Su un accordo tenuto la piu' recente sarebbe probabilmente piu' vicina a
     * quello che la mano ha appena fatto; su una singola nota — il caso in cui si guarda un
     * anello per capire cosa fa una route — le due scelte coincidono. Non vale il prezzo.
     */
    SourceLevels getSourceLevels() const noexcept;

    /** Il livello dell'LFO della prima voce attiva, per il meter. Zero se non suona niente. */
    float getLfoLevel() const noexcept { return getSourceLevels().lfo; }

private:
    SynthVoice* findFreeVoice() noexcept;
    SynthVoice* findVoiceForNote (int midiNote) noexcept;
    SynthVoice* stealVoice() noexcept;

    std::array<SynthVoice, maxVoices> voices_ {};
    int roundRobin_ { 0 };
};
} // namespace engine
