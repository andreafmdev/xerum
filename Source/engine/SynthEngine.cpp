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

    // L'arpeggiatore chiede qui tutta la memoria del suo buffer MIDI: vedi engine::Arpeggiator.
    arp_.prepare (spec_.sampleRate);

    // Tutto cio' che alloca nello stadio FX alloca **qui**: la linea di ritardo del chorus, la
    // copia del secco per la dissolvenza di bypass e il buffer interno del DryWetMixer. Da
    // process() in avanti non c'e' piu' una sola richiesta di memoria.
    //
    // `noexcept` su prepare() e' una promessa gia' presente prima di questo stadio: se una di
    // queste allocazioni fallisse il processo terminerebbe invece di lanciare. E' il
    // comportamento giusto per un plugin — un prepareToPlay senza memoria non ha un ripiego
    // sensato — ma vale la pena saperlo leggendo.
    const auto fxChannels = juce::jlimit (1, 2, spec_.numChannels);
    const auto fxBlock = juce::jmax (1, spec_.maximumBlockSize);

    chorus_.prepare (spec_.sampleRate, fxBlock, fxChannels);

    // Il riverbero alloca qui e solo qui — ~440 KB di linee dimensionate sulla size massima e
    // sul predelay massimo, cosi' muovere `rvSize` o `rvPredelay` non chiede memoria a nessuno.
    // E' il secondo dei tre caveat sul codice adottato, e il commento di dsp::PlateReverb dice
    // dove stava nell'originale.
    reverb_.prepare (spec_.sampleRate);

    juce::dsp::ProcessSpec dspSpec {};
    dspSpec.sampleRate = spec_.sampleRate;
    dspSpec.maximumBlockSize = (juce::uint32) fxBlock;
    dspSpec.numChannels = (juce::uint32) fxChannels;

    chorusMix_.setMixingRule (juce::dsp::DryWetMixingRule::sin3dB);
    chorusMix_.prepare (dspSpec);

    reverbMix_.setMixingRule (juce::dsp::DryWetMixingRule::sin3dB);
    reverbMix_.prepare (dspSpec);

    fxDry_.setSize (fxChannels, fxBlock, false, false, true);

    chorusGain_.reset (spec_.sampleRate, kFxCrossfadeSeconds);
    chorusGain_.setCurrentAndTargetValue (0.0f);
    reverbGain_.reset (spec_.sampleRate, kFxCrossfadeSeconds);
    reverbGain_.setCurrentAndTargetValue (0.0f);

    fxRingoutSamples_ = (int) (kFxRingoutSeconds * spec_.sampleRate);

    // Il riempimento dura quanto il ritardo piu' lungo che il chorus sappia produrre: oltre
    // quel punto ogni tap sta leggendo segnale vero, non gli zeri della linea appena azzerata.
    chorusPrimeLength_ = (int) std::ceil (dsp::Chorus::kBaseDelayMs * 0.001
                                          * (1.0 + dsp::Chorus::kMaxDepthFraction) * spec_.sampleRate);

    fxSilentSamples_ = 0;
    chorusPrimeSamples_ = 0;
    chorusRunning_ = false;
    reverbRunning_ = false;
}

void SynthEngine::reset() noexcept
{
    voices_.reset();
    arp_.reset();
    arpStep_.store (0, std::memory_order_relaxed);
    activeNotesLo_.store (0, std::memory_order_relaxed);
    activeNotesHi_.store (0, std::memory_order_relaxed);
    chorusMix_.reset();
    reverbMix_.reset();
    stopFx();
}

void SynthEngine::stopFx() noexcept
{
    chorus_.reset();
    reverb_.reset();
    chorusGain_.setCurrentAndTargetValue (0.0f);
    reverbGain_.setCurrentAndTargetValue (0.0f);
    fxSilentSamples_ = 0;
    chorusPrimeSamples_ = 0;
    chorusRunning_ = false;
    reverbRunning_ = false;
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

void SynthEngine::setArpSteps (const engine::ArpSnapshot& snapshot) noexcept
{
    // Identica a setMods(), anello compreso: vedi li' perche' fetch_add e perche' quattro slot.
    const auto slot = arpWriteSlot_.fetch_add (1, std::memory_order_relaxed) & 3;
    arpRing_[slot] = snapshot;
    activeArp_.store (&arpRing_[slot], std::memory_order_release);
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

    if (message.isPitchWheel())
    {
        // -1..1 attorno a 8192, che e' il centro della corsa a 14 bit. Come modWheel_: campo del
        // solo thread audio, letto una volta per blocco in process().
        pitchBend_ = ((float) message.getPitchWheelValue() - 8192.0f) / 8192.0f;
        return;
    }

    // Un bit per nota MIDI in activeNotesLo_/activeNotesHi_, per SynthEngine::getActiveNotesLo/Hi.
    // A valle dell'arpeggiatore come tutto il resto di questa funzione: vedi il commento dei
    // getter in SynthEngine.h.
    const auto setNoteBit = [this] (int note, bool on) noexcept
    {
        auto& slot = note < 64 ? activeNotesLo_ : activeNotesHi_;
        const auto bit = juce::uint64 (1) << (note % 64);
        const auto current = slot.load (std::memory_order_relaxed);
        slot.store (on ? (current | bit) : (current & ~bit), std::memory_order_relaxed);
    };

    // Il panico (all-notes-off e all-sound-off) azzera il mask per intero: gemella di
    // setNoteBit, condivisa fra i due rami sotto perche' e' lo stesso azzeramento.
    const auto clearNoteMask = [this]() noexcept
    {
        activeNotesLo_.store (0, std::memory_order_relaxed);
        activeNotesHi_.store (0, std::memory_order_relaxed);
    };

    if (message.isNoteOn())
    {
        voices_.noteOn (message.getNoteNumber(), message.getFloatVelocity());
        setNoteBit (message.getNoteNumber(), true);
    }
    else if (message.isNoteOff())
    {
        voices_.noteOff (message.getNoteNumber());
        setNoteBit (message.getNoteNumber(), false);
    }
    else if (message.isAllNotesOff())
    {
        voices_.allNotesOff();
        clearNoteMask();
    }
    else if (message.isAllSoundOff())
    {
        voices_.allSoundOff();
        clearNoteMask();
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

void SynthEngine::processFx (juce::AudioBuffer<float>& buffer, int numSamples, int numChannels) noexcept
{
    // I buffer dello stadio — la copia del secco e quello interno del DryWetMixer — sono
    // dimensionati in prepare() su spec_.maximumBlockSize. L'host promette di non superarlo, ma
    // e' una promessa che qualche host rompe, e qui la rottura non sarebbe un suono sbagliato:
    // sarebbe una scrittura fuori dai limiti. Si spezza, e basta. Il ciclo non costa niente nel
    // caso normale, dove gira una volta sola.
    const auto chunkLimit = juce::jmax (1, fxDry_.getNumSamples());

    for (int offset = 0; offset < numSamples;)
    {
        const auto chunk = std::min (chunkLimit, numSamples - offset);
        processFxChunk (buffer, offset, chunk, numChannels);
        offset += chunk;
    }
}

void SynthEngine::crossfadeWithDry (float* const* channels, int numChannels, int numSamples,
                                   juce::SmoothedValue<float>& gain) noexcept
{
    for (int ch = 0; ch < numChannels; ++ch)
    {
        // Una copia dello smoother per canale: i due canali devono vedere **la stessa** rampa,
        // non una che avanza due volte piu' in fretta sul secondo.
        auto ramp = gain;
        const auto* dry = fxDry_.getReadPointer (ch);
        auto* wet = channels[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = ramp.getNextValue();
            wet[i] = dry[i] + g * (wet[i] - dry[i]);
        }
    }

    gain.skip (numSamples);
}

int SynthEngine::ringoutSamples() const noexcept
{
    // Senza riverbero acceso comanda il chorus, ed e' la costante di prima. Con il riverbero,
    // spegnere lo stadio dopo mezzo secondo di silenzio taglierebbe la coda **a meta'**: e'
    // esattamente cio' che l'utente sente come "il riverbero si spegne da solo". La soglia
    // diventa allora la coda che i parametri correnti producono, calcolata e non misurata (vedi
    // dsp::PlateReverb::tailSeconds), quindi ai valori bassi il costo resta quello di prima e
    // solo a size e decay a fondo corsa lo stadio gira per quasi undici secondi di silenzio.
    if (! reverbRunning_)
        return fxRingoutSamples_;

    return juce::jmax (fxRingoutSamples_, (int) (reverb_.tailSeconds() * spec_.sampleRate));
}

void SynthEngine::processFxChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                  int numChannels) noexcept
{
    const auto wantChorus = params_.chorusOn;
    const auto wantReverb = params_.reverbOn;

    // "Fermo a zero": spento, e la dissolvenza finita da un pezzo. E' la condizione che rende
    // bit-trasparente il ramo di un effetto, ed e' per questo che viene letta **prima** di
    // qualunque cosa tocchi il buffer e non ricalcolata dopo.
    const auto chorusIdle = ! chorusGain_.isSmoothing() && chorusGain_.getCurrentValue() <= 0.0f;
    const auto reverbIdle = ! reverbGain_.isSmoothing() && reverbGain_.getCurrentValue() <= 0.0f;

    if (! wantChorus && ! wantReverb && chorusIdle && reverbIdle)
    {
        // Si esce **senza toccare il buffer**: e' questa riga, non un confronto a valle, a
        // garantire che con i due interruttori spenti l'uscita sia bit per bit quella di prima
        // dello stadio FX.
        if (chorusRunning_ || reverbRunning_)
            stopFx();

        return;
    }

    // Il minimo fra i canali del buffer e quelli su cui lo stadio e' stato preparato: se l'host
    // cambiasse la disposizione senza ripassare da prepareToPlay, fxDry_ avrebbe meno canali del
    // buffer e la copia del secco scriverebbe fuori.
    const auto fxChannels = std::min (std::min (numChannels, 2), fxDry_.getNumChannels());

    if (fxChannels <= 0)
        return;

    float* channels[2] { buffer.getWritePointer (0, startSample), nullptr };

    if (fxChannels > 1)
        channels[1] = buffer.getWritePointer (1, startSample);

    // Ringout: il contatore guarda l'**ingresso** dello stadio, non l'uscita, perche' e' quello
    // che dice se c'e' ancora qualcosa da lavorare. Finche' una dissolvenza e' in corso non si
    // spegne niente, altrimenti una dissolvenza avviata su una coda che nel frattempo tace
    // resterebbe appesa a meta'.
    auto inputPeak = 0.0f;

    for (int ch = 0; ch < fxChannels; ++ch)
    {
        const auto* data = channels[ch];

        for (int i = 0; i < numSamples; ++i)
            inputPeak = juce::jmax (inputPeak, std::abs (data[i]));
    }

    fxSilentSamples_ = inputPeak <= kFxSilenceFloor ? fxSilentSamples_ + numSamples : 0;

    if (! chorusGain_.isSmoothing() && ! reverbGain_.isSmoothing()
        && fxSilentSamples_ >= ringoutSamples())
    {
        // Gli effetti si spengono mentre l'ingresso tace, quindi i guadagni possono andare a
        // zero di scatto: non c'e' niente da dissolvere. stopFx() fa anche quello — senza, al
        // rientro del suono il guadagno ripartirebbe da 1 su linee appena azzerate, cioe' da un
        // buco.
        if (chorusRunning_ || reverbRunning_)
            stopFx();

        return;
    }

    juce::dsp::AudioBlock<float> block (channels, (size_t) fxChannels, (size_t) numSamples);

    // --- chorus ---------------------------------------------------------------------------

    if (wantChorus || ! chorusIdle)
    {
        if (! chorusRunning_)
        {
            // Si riparte da una linea pulita: quella vecchia contiene audio di quando l'effetto
            // era acceso l'ultima volta, e riaccenderlo lo rispuntererebbe fuori come un'eco di
            // un'altra epoca. Azzerare e' std::fill su qualche migliaio di float, nessuna
            // allocazione.
            chorus_.reset();
            chorusRunning_ = true;

            // ...ma una linea pulita non e' ancora un effetto pronto, e qui sta la parte che non
            // si vede arrivando: se la dissolvenza partisse adesso, ogni tap leggerebbe zeri
            // finche' il suo ritardo non e' trascorso, e poi comincerebbe di colpo a leggere un
            // segnale **gia' a regime**. Il salto e' grande quanto il campione che si trovava
            // all'ingresso nell'istante dell'accensione, moltiplicato per il peso del tap e per
            // il guadagno di dissolvenza raggiunto nel frattempo: cioe' dipende da dove,
            // nell'onda, capita di premere l'interruttore.
            //
            // Misurato facendo cadere l'accensione in sessanta punti diversi di una nota tenuta
            // a fondo scala con mix al 100 %: il salto peggiore fra campioni adiacenti passa da
            // 0.090 — la pendenza naturale dell'onda, cioe' niente — a **0.249**. E' esattamente
            // il clic che la dissolvenza doveva togliere, spostato dieci millisecondi piu' in la'.
            //
            // Quindi prima si riempie la linea e solo dopo si dissolve. Durante il riempimento
            // l'effetto gira ma il guadagno resta inchiodato a zero, e la dissolvenza incrociata
            // qui sotto riscrive il secco **esatto**: l'ascoltatore sente il ritardo
            // dell'accensione (16.5 ms di riempimento piu' 12 di dissolvenza), non un buco.
            chorusPrimeSamples_ = chorusPrimeLength_;
        }

        // Il bersaglio della dissolvenza tiene conto del riempimento: finche' la linea non e'
        // piena, acceso e spento si assomigliano — l'uscita e' il secco.
        chorusGain_.setTargetValue (wantChorus && chorusPrimeSamples_ <= 0 ? 1.0f : 0.0f);

        // La copia del secco serve solo alla dissolvenza: in regime, con l'effetto stabilmente
        // acceso, questo memcpy non avviene.
        const auto crossfading = chorusGain_.isSmoothing() || chorusGain_.getCurrentValue() < 1.0f;

        if (crossfading)
            for (int ch = 0; ch < fxChannels; ++ch)
                fxDry_.copyFrom (ch, 0, channels[ch], numSamples);

        chorusMix_.setWetMixProportion (juce::jlimit (0.0f, 1.0f, params_.chorusMix01));
        chorusMix_.pushDrySamples (block);

        chorus_.setParameters (params_.chorusRateHz, params_.chorusDepth01, params_.chorusFeedback01);
        chorus_.process (channels[0], channels[1], numSamples);

        chorusMix_.mixWetSamples (block);

        if (crossfading)
            crossfadeWithDry (channels, fxChannels, numSamples, chorusGain_);

        chorusPrimeSamples_ = juce::jmax (0, chorusPrimeSamples_ - numSamples);
    }
    else if (chorusRunning_)
    {
        chorus_.reset();
        chorusRunning_ = false;
    }

    // --- riverbero ------------------------------------------------------------------------

    if (wantReverb || ! reverbIdle)
    {
        if (! reverbRunning_)
        {
            reverb_.reset();
            reverbRunning_ = true;
        }

        reverbGain_.setTargetValue (wantReverb ? 1.0f : 0.0f);

        const auto crossfading = reverbGain_.isSmoothing() || reverbGain_.getCurrentValue() < 1.0f;

        // Il secco del riverbero e' l'**uscita del chorus**, non l'ingresso dello stadio: i due
        // effetti sono in serie e ciascuno dissolve verso cio' che ha davanti.
        if (crossfading)
            for (int ch = 0; ch < fxChannels; ++ch)
                fxDry_.copyFrom (ch, 0, channels[ch], numSamples);

        reverbMix_.setWetMixProportion (juce::jlimit (0.0f, 1.0f, params_.reverbMix01));
        reverbMix_.pushDrySamples (block);

        if (crossfading)
        {
            // La rampa **sull'ingresso**, dopo che il secco e' stato messo da parte e prima che
            // il riverbero lo legga. E' il rimedio al gradino d'accensione, e per il riverbero
            // e' l'unico possibile: aspettare che le linee siano piene vorrebbe dire aspettare
            // la coda intera. Cosi' dentro il tank non entra nessun gradino — l'ingresso sale da
            // zero — e il bagnato cresce da zero per costruzione, qualunque sia il punto
            // dell'onda in cui e' caduto l'interruttore.
            //
            // In spegnimento la stessa rampa chiude anche il rubinetto d'ingresso mentre la
            // dissolvenza d'uscita porta via la coda: dodici millisecondi in cui il riverbero
            // smette di sentire e di farsi sentire insieme.
            auto ramp = reverbGain_;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto g = ramp.getNextValue();

                for (int ch = 0; ch < fxChannels; ++ch)
                    channels[ch][i] *= g;
            }
        }

        reverb_.setParameters (params_.reverbSize01, params_.reverbDecay01, params_.reverbDamp01,
                               params_.reverbPredelaySeconds);
        reverb_.process (channels[0], channels[1], numSamples);

        reverbMix_.mixWetSamples (block);

        if (crossfading)
            crossfadeWithDry (channels, fxChannels, numSamples, reverbGain_);
    }
    else if (reverbRunning_)
    {
        reverb_.reset();
        reverbRunning_ = false;
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

        // Lo stadio FX si spegne con tutto il resto, e con lui la sua memoria: senza, togliendo
        // il bypass si sentirebbe ricomparire la coda del chorus di prima che scattasse.
        stopFx();

        // E anche l'arpeggiatore: le voci sono gia' state ammutolite qui sopra, quindi non c'e'
        // nessun note-off da emettere — c'e' solo uno stato da dimenticare, altrimenti togliendo
        // il bypass ripartirebbe da meta' pattern con dei tasti che nessuno sta piu' premendo.
        arp_.reset();
        arpStep_.store (0, std::memory_order_relaxed);
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
    params_.pitchBend = pitchBend_;

    // Stesso trattamento delle mod, e per la stessa ragione: se il message thread non ha ancora
    // pubblicato niente si tiene il puntatore arrivato con setParams(), che e' nullptr in
    // produzione ed e' la via con cui i test iniettano una sequenza senza passare da setArpSteps.
    if (const auto* publishedArp = activeArp_.load (std::memory_order_acquire); publishedArp != nullptr)
        params_.arp.steps = publishedArp;

    // **L'arpeggiatore, in testa a tutto**: riscrive il MidiBuffer prima che il ciclo qui sotto lo
    // legga, cosi' la precisione campione-esatta e' quella che il ciclo ha gia' (spezza il render
    // a ogni evento) e VoiceManager non sa nemmeno che l'arp esiste. Con `arpOn` falso non tocca
    // il buffer: e' da quella riga che discende la non-regressione bit per bit.
    arp_.process (midi, numSamples, params_.arp,
                  ArpTransport { (double) params_.bpm, params_.ppqPosition, params_.transportPlaying });

    arpStep_.store (arp_.currentStep(), std::memory_order_relaxed);

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

    // Lo stadio FX: dopo tutto il rendering, prima del gain master. Con il chorus spento non
    // tocca il buffer — vedi il commento della dichiarazione.
    processFx (buffer, numSamples, numChannels);

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
