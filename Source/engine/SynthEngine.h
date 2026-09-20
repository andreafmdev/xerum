#pragma once

#include "dsp/Chorus.h"
#include "dsp/PlateReverb.h"
#include "dsp/Constants.h"
#include "engine/Arpeggiator.h"
#include "engine/NoteMask.h"
#include "engine/EngineParams.h"
#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

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
    static constexpr int kControlBlockSamples = dsp::kControlRateSamples;

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

    /**
     * Pubblica una nuova sequenza di step per l'arpeggiatore. Gemella di setMods(), stesso
     * anello di quattro slot preallocati e stesso puntatore atomico, per la stessa ragione: nel
     * ValueTree gli step sono una **stringa CSV**, e sul thread audio non si tokenizza niente.
     *
     * Chiamabile da qualunque thread. Come per le mod, il peggio che un'infilata di pubblicazioni
     * dentro un solo blocco audio puo' produrre e' una sequenza sbagliata per un blocco, mai una
     * lettura di memoria liberata: gli slot vivono quanto il motore.
     */
    void setArpSteps (const engine::ArpSnapshot& snapshot) noexcept;

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

    /**
     * L'indice di griglia dell'ultimo passo dell'arpeggiatore, 0..15, per MeterFrame::arpStep.
     *
     * Istantaneo come `lfo` e `mw`, e per la stessa ragione: e' una **posizione** dentro il
     * pattern, non un transitorio. Un massimo su un frame del meter mostrerebbe l'indice piu'
     * alto degli ultimi 33 ms, cioe' un riquadro che salta avanti e torna indietro.
     */
    int getArpStep() const noexcept { return arpStep_.load (std::memory_order_relaxed); }

    /**
     * Quali note stanno suonando, un bit per nota MIDI: 0..63 in `Lo`, 64..127 in `Hi`.
     *
     * Istantaneo come `mw` e `arpStep`, e per la stessa ragione: una nota tenuta e' uno **stato**,
     * non un transitorio. Azzerarlo alla lettura spegnerebbe la tastiera della UI non appena il
     * thread audio smette di girare, mentre il tasto e' ancora premuto.
     *
     * I bit si alzano qui, cioe' **a valle dell'arpeggiatore**: ad arp acceso il mask descrive il
     * pattern che suona, non i tasti tenuti. E' la scelta voluta.
     */
    juce::uint64 getActiveNotesLo() const noexcept { return activeNotesLo_.load (std::memory_order_relaxed); }
    juce::uint64 getActiveNotesHi() const noexcept { return activeNotesHi_.load (std::memory_order_relaxed); }

private:
    void handleMidiEvent (const juce::MidiMessage& message) noexcept;

    /**
     * Arma o disarma il pedale di sustain, e con lui la maschera del keybed.
     *
     * Chi decide `down` e' il ramo CC 64 di handleMidiEvent, che ci passa gia' il verdetto
     * dell'arbitraggio con l'arpeggiatore: ad arp acceso il pedale e' del latch dell'arp, e qui
     * il sustain sulle voci deve restare spento. Vedi il commento di `sustainPedal_`.
     */
    void setSustainPedal (bool down) noexcept;

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

    /**
     * Lo stadio FX, fra la fine del rendering e `applyGainRamp`.
     *
     * **Dove sta, e perche' li'.** Dopo il render delle voci e prima del gain master, che e' la
     * stessa posizione che gli danno Vital, Surge e Odin. Il soft clipper resta l'ultima cosa
     * della catena: continua a essere la rete di sicurezza per tutto cio' che lo precede, FX
     * compresi. Uno stadio FX **e'** uno stadio di guadagno dopo il filtro, quindi tocca lo
     * stesso margine di cui parla docs/architecture.md — vedi ModulationStressTests, dove la
     * stessa passata gira adesso anche con il chorus acceso, e con tutti e due gli effetti.
     *
     * **Due effetti in serie, ciascuno con la sua dissolvenza.** Chorus e poi riverbero, ognuno
     * con il proprio juce::dsp::DryWetMixer e il proprio guadagno di bypass: `fx1On` muove
     * chorusGain_, `fx2On` muove reverbGain_, tutti e due arrivano a destinazione in
     * kFxCrossfadeSeconds. Finche' una dissolvenza e' in corso l'uscita di quell'effetto e'
     * `secco + g * (lavorato - secco)` — dove "secco" e' l'ingresso **di quell'effetto**, cioe'
     * per il riverbero l'uscita del chorus. Tutti e quattro i synth studiati tagliano netto, e
     * tutti e quattro fanno clic.
     *
     * Quando un guadagno e' arrivato **esattamente** a zero il ramo di quell'effetto non gira:
     * da li' discende il criterio di accettazione piu' importante del lavoro — con `fx2On`
     * falso l'uscita e' bit per bit quella di prima che il riverbero esistesse, e con tutti e
     * due falsi quella di prima che lo stadio FX esistesse. E' la stessa proprieta' strutturale
     * del mod matrix vuoto, ottenuta nello stesso modo: non un ramo che calcola l'identita', un
     * ramo che non calcola niente.
     *
     * **Le due accensioni non si somigliano, e il motivo e' istruttivo.** Il chorus va
     * *riempito* prima di dissolvere (vedi sotto): i suoi tap leggono zeri finche' il ritardo
     * non e' trascorso e poi cominciano di colpo a leggere segnale a regime. Il riverbero ha lo
     * stesso difetto in forma peggiore — il gradino arriva dopo il predelay e ci mette centinaia
     * di millisecondi a diffondersi — ma il rimedio non puo' essere lo stesso, perche' aspettare
     * che la coda sia "piena" vorrebbe dire aspettare secondi. Quindi il riverbero riceve un
     * **ingresso rampato** dalla stessa dissolvenza che rampa la sua uscita: dentro le linee non
     * entra nessun gradino, il bagnato cresce da zero per costruzione e non c'e' niente da
     * riempire. E' la differenza fra un effetto la cui uscita *deve* venire dal passato e uno la
     * cui uscita puo' cominciare da adesso.
     *
     * **Ringout.** Si contano i campioni consecutivi in cui l'**ingresso** dello stadio e'
     * silenzioso; oltre la soglia gli effetti si spengono e le loro linee si azzerano. E' il
     * pattern di Surge. La soglia non e' piu' una costante: mezzo secondo bastava al chorus
     * (feedback a fondo corsa su un ritardo di 11 ms: -145 dB dopo mezzo secondo) ma
     * troncherebbe qualunque coda di riverbero, quindi con il riverbero acceso diventa la coda
     * che i suoi parametri correnti producono — 2.5 s ai valori di fabbrica, fino a 10.7 s a size
     * e decay a fondo corsa. Vedi ringoutSamples().
     *
     * Non alloca: le linee di ritardo, le linee del riverbero, il buffer del secco e quelli dei
     * due DryWetMixer sono dimensionati in prepare().
     */
    void processFx (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels) noexcept;

    /** Una fetta dello stadio, mai piu' lunga di quanto prepare() abbia dimensionato. */
    void processFxChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                         int numChannels) noexcept;

    /**
     * Dissolve `channels` fra la copia in fxDry_ e cio' che ci sta sopra adesso, consumando
     * `gain`. Una copia dello smoother per canale — i due canali devono vedere **la stessa**
     * rampa, non una che avanza due volte piu' in fretta sul secondo — e uno skip solo alla
     * fine.
     */
    void crossfadeWithDry (float* const* channels, int numChannels, int numSamples,
                           juce::SmoothedValue<float>& gain) noexcept;

    /** I campioni di silenzio in ingresso dopo i quali lo stadio si spegne. Vedi processFx(). */
    int ringoutSamples() const noexcept;

    /** Spegne i due effetti e azzera la loro memoria. Non alloca. */
    void stopFx() noexcept;

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

    // --- arpeggiatore --------------------------------------------------------------------
    // Sta in testa a process(), prima del ciclo degli eventi: riscrive il MidiBuffer e basta.
    // Con `arpOn` falso non lo tocca, quindi il resto del motore non sa nemmeno che esiste.

    Arpeggiator arp_;
    ArpSnapshot arpRing_[4] {};
    std::atomic<int> arpWriteSlot_ { 0 };
    std::atomic<const ArpSnapshot*> activeArp_ { nullptr };
    std::atomic<int> arpStep_ { 0 };

    std::atomic<float> lfoLevel_ { 0.0f };
    float modWheel_ { 0.0f }; // CC 1, solo thread audio
    float pitchBend_ { 0.0f }; // rotella di pitch, -1..1, solo thread audio

    /**
     * Il pedale di sustain (CC 64) **come lo vede questo strato**, cioe' gia' arbitrato con
     * l'arpeggiatore: falso ogni volta che l'arp e' acceso, perche' li' il pedale e' un latch
     * dei tasti e se ne occupa Arpeggiator.
     *
     * L'arbitraggio serve e non e' una comodita': l'arp riscrive il MidiBuffer *prima* di questo
     * strato, quindi un sustain armato qui sotto terrebbe ogni nota che l'arp emette, e la
     * sequenza si accumulerebbe in un cluster che cresce a ogni passo invece di arpeggiare.
     *
     * E' una copia di quello che tiene VoiceManager, ed e' voluto: qui serve a decidere i bit
     * del keybed, che sono di questo strato e che VoiceManager non conosce.
     */
    bool sustainPedal_ { false };

    /**
     * I tasti che restano **accesi sul keybed** perche' li tiene il pedale, un bit per nota.
     *
     * Non e' la stessa maschera di VoiceManager: quella governa quando una voce va in release,
     * questa quando un tasto della UI si spegne. Tenerle separate e' cio' che permette al keybed
     * di mostrare quello che si sente davvero senza che la UI sappia niente del pool di voci.
     */
    NoteMask sustainedNotes_;

    // --- telemetria per il meter ---------------------------------------------------------
    // Sola lettura verso l'editor: niente qui dentro torna nel percorso del segnale. Gli
    // atomici si scrivono una volta per blocco con store(relaxed), gli accumulatori sono
    // normali float toccati solo dal thread audio.

    std::atomic<float> envPeak_ { 0.0f };
    std::atomic<float> env2Peak_ { 0.0f };
    std::atomic<float> velPeak_ { 0.0f };
    std::atomic<float> modWheelLevel_ { 0.0f };
    std::atomic<juce::uint64> activeNotesLo_ { 0 };   // note 0..63
    std::atomic<juce::uint64> activeNotesHi_ { 0 };   // note 64..127

    /** I massimi accumulati sulle sotto-fette del blocco in corso. Azzerati da process(). */
    float blockEnvPeak_ { 0.0f };
    float blockEnv2Peak_ { 0.0f };
    float blockVelPeak_ { 0.0f };

    // --- stadio FX -----------------------------------------------------------------------

public:
    /**
     * Durata della dissolvenza di bypass dello stadio FX, in secondi.
     *
     * Dodici millisecondi: dentro la finestra 5-20 ms che rende il passaggio inudibile senza
     * diventare un dissolvenza percepibile a se'. Sotto i 5 ms il gradino residuo su un'onda a
     * fondo scala torna a sentirsi; sopra i 20 l'interruttore smette di sembrare un interruttore.
     * Pubblica perche' e' il numero su cui il test del bypass dimensiona la sua finestra.
     */
    static constexpr double kFxCrossfadeSeconds = 0.012;

    /** Quanto il plugin dichiara all'host come coda (getTailLengthSeconds): copre la coda
        peggiore di riverbero piu' chorus. ReverbTests verifica che la formula ci stia dentro. */
    static constexpr double kDeclaredTailSeconds = 11.0;


private:
    /**
     * Silenzio in ingresso oltre il quale lo stadio smette di girare, quando c'e' solo il
     * chorus. Con il riverbero acceso comanda la sua coda: vedi ringoutSamples().
     */
    static constexpr double kFxRingoutSeconds = 0.5;

    /** Sotto questo modulo un campione conta come silenzio per il ringout. -140 dBFS: sotto la
        risoluzione del float a livelli musicali, e ben sotto la coda di qualunque voce. */
    static constexpr float kFxSilenceFloor = 1.0e-7f;

    dsp::Chorus chorus_;
    dsp::PlateReverb reverb_;

    /**
     * Il dry/wet dello stadio, con regola `sin3dB` — l'equal-power, la stessa di Vital.
     *
     * Non scritto a mano: juce::dsp::DryWetMixer implementa sette regole di mix e smussa da se'
     * i due guadagni su 50 ms, quindi muovere `chMix` non produce zipper. A mix 0 il guadagno
     * del secco vale esattamente 1 e quello del bagnato esattamente 0, il che rende il caso
     * degenere gratuito invece che un ramo in piu' da mantenere.
     */
    juce::dsp::DryWetMixer<float> chorusMix_;

    /** Il gemello del precedente per il riverbero: `rvMix` e `chMix` sono due proporzioni
        distinte su due effetti in serie, quindi due mixer e non uno. */
    juce::dsp::DryWetMixer<float> reverbMix_;

    /** Copia del secco del blocco, per la dissolvenza di bypass. Il DryWetMixer ha una copia
        sua ma la consuma dentro mixWetSamples(): servono entrambe. Una sola basta per tutti e
        due gli effetti perche' le due dissolvenze sono **in sequenza**: quella del riverbero
        ricopia sopra, e cio' che ricopia e' l'uscita del chorus, che e' il suo secco.
        Dimensionato in prepare(). */
    juce::AudioBuffer<float> fxDry_;

    /** 0 = effetto completamente fuori, 1 = completamente dentro. Vedi processFx(). */
    juce::SmoothedValue<float> chorusGain_;
    juce::SmoothedValue<float> reverbGain_;

    int fxSilentSamples_ { 0 };
    int fxRingoutSamples_ { 0 };

    /** Campioni che mancano al riempimento della linea prima che la dissolvenza del chorus
        possa partire, e la sua lunghezza a regime (il ritardo massimo del chorus). Il riverbero
        non ne ha uno: rampa l'ingresso invece di riempire. Vedi processFx(). */
    int chorusPrimeSamples_ { 0 };
    int chorusPrimeLength_ { 0 };

    bool chorusRunning_ { false };
    bool reverbRunning_ { false };
};
} // namespace engine
