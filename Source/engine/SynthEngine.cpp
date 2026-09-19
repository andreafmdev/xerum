#include "engine/SynthEngine.h"

#include "parameters/ParameterDenormalise.h"
#include "parameters/ParameterTable.h"

#include <algorithm>
#include <cmath>


namespace engine
{
namespace
{
/**
 * Sopra questa soglia l'uscita smette di essere lineare e comincia a piegare.
 *
 * Era 0.8 ed e' salita insieme a kVoiceHeadroomGain, per la ragione opposta a quella che
 * verrebbe in mente: non perche' adesso arrivi meno segnale, ma perche' ne arriva di piu'.
 * Con la voce a -3 dB un accordo ordinario (quattro note, level 1.0, volume di default)
 * presenta 0.898 al clipper. A 0.8 la rete di sicurezza si sarebbe accesa su una suonata
 * normale, cioe' sarebbe diventata uno stadio di distorsione a tempo pieno; a 0.9 ci sarebbe
 * passata dentro per mezzo millesimo, che non e' un margine. A 0.95 resta spenta con mezzo
 * decibel di margine, e resta spenta su tutti e dodici i preset di fabbrica (il piu' caldo,
 * "Acid Line", arriva a 0.84 su quattro note: oltre un decibel di margine).
 *
 * Il prezzo e' che il ginocchio si accorcia da 0.2 a 0.05 di corsa, quindi quando il clipper
 * interviene davvero (sedici voci: 2.11 in ingresso; matrix pieno: 3.5) piega molto piu'
 * bruscamente di prima. In cambio lascia intatta una fetta piu' larga di forma d'onda: sotto
 * 0.95 l'uscita e' bit per bit quella non clippata, mentre a 0.8 veniva toccato anche tutto
 * cio' che stava fra 0.8 e 0.95. Su escursioni di picco — che e' quello che il clipper vede —
 * toccare meno campioni conta piu' che piegarli gentilmente.
 */
constexpr float kSoftClipThreshold = 0.95f;

/**
 * Rete di sicurezza sull'uscita: identica all'ingresso fino alla soglia, poi piega dolcemente
 * e non supera mai 1.0. Serve perche' il guadagno per voce e' una costante (vedi
 * SynthVoice::kVoiceHeadroomGain): un accordo abbastanza fitto, un volume master alto o una
 * route del matrix su `res` possono comunque superare il fondo scala, e senza questo l'host
 * riceverebbe campioni troncati a zero decibel — il clipping digitale netto, quello che si
 * sente come strappo.
 *
 * Lineare sotto soglia: niente distorsione aggiunta al segnale normale, a differenza di un
 * soft clipper polinomiale attivo su tutta la corsa. La derivata vale 1 alla soglia, quindi
 * non c'e' spigolo nel punto di innesto. Nessuna libm: un confronto, una divisione.
 */
float softClip (float x) noexcept
{
    const auto magnitude = std::abs (x);

    if (magnitude <= kSoftClipThreshold)
        return x;

    const auto headroom = 1.0f - kSoftClipThreshold;
    const auto over = (magnitude - kSoftClipThreshold) / headroom;
    const auto bent = kSoftClipThreshold + headroom * (over / (1.0f + over));

    return x < 0.0f ? -bent : bent;
}
} // namespace

void SynthEngine::prepare (const EngineSpec& spec) noexcept
{
    spec_ = spec;
    voices_.prepare (spec_.sampleRate);
    globalLfo_.prepare (spec_.sampleRate);
}

void SynthEngine::reset() noexcept
{
    voices_.reset();
}

void SynthEngine::setMasterGainLinear (float gain) noexcept
{
    masterGain_ = gain;
}

void SynthEngine::setParams (const EngineParams& p) noexcept
{
    params_ = p;
    voices_.setParams (p);
}

void SynthEngine::setWavetable (const dsp::MipTable* table) noexcept
{
    voices_.setWavetable (table);
}

void SynthEngine::setPendingWavetable (const dsp::MipTable* table) noexcept
{
    pendingWavetable_.store (table, std::memory_order_release);
}

void SynthEngine::setMods (const engine::ModSnapshot& snapshot) noexcept
{
    // fetch_add invece di uno scrittore singolo: setMods puo' arrivare dal message thread e,
    // in futuro, da chi ricarica un preset. L'incremento atomico garantisce che due chiamate
    // concorrenti scelgano slot diversi, quindi nessuna delle due riscrive l'altra a meta'.
    const auto slot = modWriteSlot_.fetch_add (1, std::memory_order_relaxed) & 3;
    modRing_[slot] = snapshot;
    activeMods_.store (&modRing_[slot], std::memory_order_release);
}

void SynthEngine::publishSourceLevels (float lfo, float env, float env2, float vel) noexcept
{
    lfoLevel_.store (lfo, std::memory_order_relaxed);
    envPeak_.store (env, std::memory_order_relaxed);
    env2Peak_.store (env2, std::memory_order_relaxed);
    velPeak_.store (vel, std::memory_order_relaxed);
    modWheelLevel_.store (modWheel_, std::memory_order_relaxed);
}

void SynthEngine::handleMidiEvent (const juce::MidiMessage& message) noexcept
{
    if (message.isController() && message.getControllerNumber() == 1)
    {
        // Mod wheel: sorgente `mw` del matrix, 0..1. Prima degli altri rami perche' e' il caso
        // piu' frequente fra i messaggi non di nota e non ha niente a che vedere con le voci.
        modWheel_ = (float) message.getControllerValue() / 127.0f;
        return;
    }

    if (message.isNoteOn())
    {
        voices_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        voices_.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff())
    {
        voices_.allNotesOff();
    }
    else if (message.isAllSoundOff())
    {
        voices_.allSoundOff();
    }
}

/**
 * Il ciclo di process() spezza il render sugli eventi MIDI; questo lo spezza ancora, in fette di
 * al piu' kControlBlockSamples campioni. E' tutta la correzione: senza, la lunghezza delle fette
 * — e quindi il tasso a cui SynthVoice::render() rivaluta applyModulation() e avanza l'LFO — la
 * decideva l'host.
 *
 * Il costo non e' lineare nel numero di sotto-fette perche' non lo e' il lavoro che ognuna fa:
 * applyModulation() denormalizza sette target (due con std::pow), updateCutoff() chiama
 * std::exp2 e setCutoffHz() std::tan. Misurato in Release nel caso peggiore (16 voci, unison 8,
 * una route lfo -> cutoff, blocco da 512 a 48 kHz) il prezzo di scendere da 128 a 32 campioni di
 * fetta e' meno di mezzo punto percentuale del budget del blocco. La tabella sta nel commento di
 * kControlBlockSamples.
 *
 * Nessuna allocazione, nessuna struttura dinamica: due interi e un ciclo.
 */
void SynthEngine::renderControlSlices (float* left, float* right, int numSamples, int gridPhase) noexcept
{
    for (int offset = 0; offset < numSamples; )
    {
        // La griglia e' ancorata all'inizio del buffer, non al punto da cui si sta rendendo: la
        // prima sotto-fetta dopo un evento MIDI si accorcia quanto basta a ritrovare il confine.
        // Vedi il commento della dichiarazione: e' cio' che rende i confini di controllo gli
        // stessi campioni assoluti qualunque buffer passi l'host, e non solo la loro frequenza.
        const auto phase = (gridPhase + offset) % kControlBlockSamples;
        const auto slice = std::min (kControlBlockSamples - phase, numSamples - offset);

        // L'LFO libero avanza *qui dentro*, non una volta per blocco. Lasciarlo fuori sarebbe
        // stato il modo piu' facile di correggere meta' del difetto: le voci avrebbero avuto il
        // loro tasso fisso e l'LFO condiviso no, quindi con lfoRetrig falso — l'unico caso in cui
        // qualcuno lo legge — il buffer dell'host sarebbe tornato a decidere il suono.
        params_.globalLfoLevel = globalLfo_.advance (slice);

        // Il livello nuovo arriva alle voci da solo, non dentro una EngineParams ripubblicata.
        // E' l'unico campo che cambia fra una sotto-fetta e l'altra: tutto il resto e' gia'
        // posato da process(), una volta per blocco. Prima qui c'era voices_.setParams(params_),
        // corretta (ogni setter che tocca e' idempotente) ma cara — sedici sotto-fette per
        // sedici voci, e ogni chiamata ricalcola tre coefficienti d'inviluppo con exp() e
        // riscorre la lista delle route per rifare la maschera. Era quasi tutto il costo delle
        // sotto-fette: vedi il commento di VoiceManager::setGlobalLfoLevel.
        voices_.setGlobalLfoLevel (params_.globalLfoLevel);
        voices_.render (left + offset, right + offset, slice);

        // Telemetria, e nient'altro: si legge cio' che la fetta appena resa ha lasciato dietro
        // di se' e si tiene il massimo. Nessun ramo di qui torna nel segnale — l'audio esce
        // bit per bit come senza queste tre righe.
        //
        // Qui e non a fine blocco perche' qui c'e' la risoluzione: 32 campioni, 1500 Hz a
        // 48 kHz. A fine blocco un attacco di mezzo millisecondo sarebbe gia' passato e
        // ridisceso, e il meter mostrerebbe il sustain invece del picco.
        const auto levels = voices_.getSourceLevels();
        blockEnvPeak_ = std::max (blockEnvPeak_, levels.env);
        blockEnv2Peak_ = std::max (blockEnv2Peak_, levels.env2);
        blockVelPeak_ = std::max (blockVelPeak_, levels.vel);

        offset += slice;
    }
}

void SynthEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) noexcept
{
    if (params_.bypass)
    {
        // E' un sintetizzatore, non c'e' un ingresso da far passare: bypass significa silenzio
        // e nessuna voce che continua a suonare sotto sotto.
        voices_.allSoundOff();
        buffer.clear();

        // Anche gli anelli di modulazione devono dire la verita' in bypass: niente suona,
        // quindi niente si muove. Il mod wheel resta dov'e': e' una posizione, non un suono.
        publishSourceLevels (0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Se il message thread ha pubblicato una nuova tavola (wtIndex cambiato), applicarla
    // qui: siamo sul thread audio, l'unico che può mutare in sicurezza lo stato delle voci.
    if (const auto* table = pendingWavetable_.exchange (nullptr, std::memory_order_acquire); table != nullptr)
        voices_.setWavetable (table);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // I massimi ripartono da zero a ogni blocco: il meter deve mostrare cio' che e' successo
    // *adesso*, non il ricordo del picco piu' alto da quando il plugin e' aperto. A tenere il
    // picco fra una lettura dell'editor e l'altra ci pensa MeterFrame, un piano piu' in la'.
    blockEnvPeak_ = 0.0f;
    blockEnv2Peak_ = 0.0f;
    blockVelPeak_ = 0.0f;

    // L'LFO libero gira anche senza note: e' cio' che lo rende "libero". Qui si accorda soltanto;
    // ad avanzarlo e' renderControlSlices(), una sotto-fetta per volta. Con matrix vuoto non tocca
    // niente — le voci leggono globalLfoLevel solo se una route punta a un target — quindi farlo
    // girare sempre non cambia di un campione chi non lo usa.
    constexpr auto* specLrate = params::find ("lrate");
    static_assert (specLrate != nullptr, "lrate non e' in ParameterTable.h");

    globalLfo_.setShape ((dsp::Lfo::Shape) juce::jlimit (0, 4, params_.lfoShapeIndex));
    globalLfo_.setFadeSeconds (0.0f); // la dissolvenza e' per nota: non ha senso sull'LFO libero
    globalLfo_.setFrequencyHz (params_.lfoSync
                                   ? dsp::syncedRateHz (params_.lfoRateRaw, (double) params_.bpm)
                                   : params::denormalise (*specLrate, params_.lfoRateRaw));

    // Se il message thread non ha ancora pubblicato niente si tiene il puntatore arrivato con
    // setParams(): e' nullptr in produzione (collectEngineParams non lo riempie) ed e' la via
    // con cui i test della voce iniettano uno snapshot senza passare da setMods().
    if (const auto* published = activeMods_.load (std::memory_order_acquire); published != nullptr)
        params_.mods = published;

    params_.modWheel = modWheel_;

    // Il livello di fine blocco precedente, non ancora avanzato: dentro il ciclo lo rinfresca
    // renderControlSlices() a ogni sotto-fetta.
    params_.globalLfoLevel = globalLfo_.level();

    // L'unica pubblicazione dell'intera EngineParams del blocco, e sta prima del ciclo MIDI, non
    // dopo: una nota che parte a campione zero chiama start(), che valuta subito la modulazione,
    // e deve trovare i parametri di *questo* blocco gia' posati. Da qui in avanti le sotto-fette
    // muovono il solo livello dell'LFO libero, con setGlobalLfoLevel. Il mod wheel letto qui e'
    // quello di fine blocco precedente, perche' un CC 1 a meta' buffer viene gestito nel ciclo
    // qui sotto e avra' effetto dal blocco dopo.
    voices_.setParams (params_);

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : left;

    int samplePos = 0;

    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);
        const int slice = eventPos - samplePos;

        if (slice > 0)
        {
            renderControlSlices (left + samplePos, right + samplePos, slice, samplePos);
            samplePos = eventPos;
        }

        handleMidiEvent (metadata.getMessage());
    }

    if (samplePos < numSamples)
        renderControlSlices (left + samplePos, right + samplePos, numSamples - samplePos, samplePos);

    // Dopo il render, non prima: adesso params_.globalLfoLevel e' il livello di fine blocco,
    // quello che il meter dell'editor deve mostrare. L'LFO e' istantaneo (bipolare: un massimo
    // non vorrebbe dire niente), env/env2/vel sono i massimi accumulati sulle sotto-fette.
    publishSourceLevels (params_.lfoRetrig ? voices_.getLfoLevel() : params_.globalLfoLevel,
                         blockEnvPeak_, blockEnv2Peak_, blockVelPeak_);

    // Rampato, non applicato di scatto: spec 6.4 elenca volume fra i cinque bersagli di
    // smoothing (cutoff, wtpos, level, volume, pan). Un salto a gain di blocco produrrebbe lo
    // stesso zipper noise che gli altri quattro evitano gia' — muovere il fader master o
    // automatizzarlo con lo step precedente avrebbe prodotto un gradino udibile a ogni blocco.
    buffer.applyGainRamp (0, numSamples, previousMasterGain_, masterGain_);
    previousMasterGain_ = masterGain_;

    // Dopo il volume master, mai prima: e' il livello che esce davvero dal plugin quello da
    // proteggere. Si passa sui canali reali del buffer (non su left/right, che in mono sono
    // lo stesso puntatore e verrebbe clippato due volte).
    for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            data[i] = softClip (data[i]);
    }

    for (int ch = 2; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);
}
} // namespace engine
