#include "plugin/PluginProcessor.h"
#if ! XERUM_HEADLESS_TESTS
 #include "plugin/PluginEditor.h"
#endif
#include "engine/ParamCollect.h"
#include "state/StateToEngine.h"
#include "state/StateTree.h"

namespace
{
// Ogni quanto il timer del processore controlla se wtIndex e' cambiato.
constexpr int kWavetablePollHz = 25;
} // namespace

XerumAudioProcessor::XerumAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", params::createParameterLayout()),
      engine_ (std::make_unique<engine::SynthEngine>())
{
    state::ensureChildren (apvts_.state);

    // Risolti una volta sola qui, ciclando sulla tabella generata: collectParams() indicizza
    // paramSlots_ con params::ParamSlot, nessuna ricerca per nome sul thread audio.
    //
    // Il ciclo ha sostituito 28 assegnazioni scritte a mano. Non era una questione di righe: era
    // la seconda di tre copie della stessa mappatura id -> slot (le altre due erano ParamSlot.h
    // e il RealAccessor di Tests/ParameterSeamTests.cpp), e con tre copie un errore in una sola
    // produce un test verde su un cablaggio sbagliato. Ora l'enum e kSlotIds escono entrambi da
    // parameters.json (scripts/gen-params.mjs), allineati per costruzione.
    for (int i = 0; i < params::kNumSlots; ++i)
    {
        auto* raw = apvts_.getRawParameterValue (params::kSlotIds[i]);

        // Strutturalmente impossibile: kSlotIds e il layout nascono dalla stessa tabella. Se
        // succede e' un difetto del generatore, e va visto in debug invece che diventare un
        // parametro muto a runtime (collectParams() ha comunque il suo fallback sui nullptr).
        jassert (raw != nullptr);
        paramSlots_[(size_t) i] = raw;
    }

    // wtIndex e volume non passano da EngineParams/collectEngineParams: restano a parte.
    paramWtIndex_ = apvts_.getRawParameterValue ("wtIndex");
    paramVolume_ = apvts_.getRawParameterValue ("volume");

    listenToState (apvts_.state);

    // Subito, non solo al primo cambiamento: un progetto riaperto con delle assegnazioni già
    // salvate deve suonare modulato dal primo blocco, senza aspettare che l'utente tocchi
    // qualcosa. (setStateInformation, se arriva, rifà comunque il giro sul nuovo albero.)
    rebuildModSnapshot();
    rebuildArpSnapshot();

    apvts_.addParameterListener ("wtIndex", this);
    startTimerHz (kWavetablePollHz);
}

XerumAudioProcessor::~XerumAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    listenedState_.removeListener (this);
    apvts_.removeParameterListener ("wtIndex", this);
}

void XerumAudioProcessor::listenToState (juce::ValueTree root)
{
    listenedState_.removeListener (this);
    listenedState_ = std::move (root);
    listenedState_.addListener (this);
}

void XerumAudioProcessor::rebuildModSnapshot()
{
    engine::ModSnapshot snapshot;
    state::buildModSnapshot (apvts_.state.getChildWithName (state::ids::MODS), snapshot);
    engine_->setMods (snapshot);
}

void XerumAudioProcessor::rebuildArpSnapshot()
{
    engine::ArpSnapshot snapshot;
    state::buildArpSnapshot (apvts_.state.getChildWithName (state::ids::ARP), snapshot);
    engine_->setArpSteps (snapshot);
}

// Il listener sta sulla radice dell'APVTS, quindi qui passa *ogni* proprietà dell'albero: ogni
// movimento di ogni knob, che vive nei figli PARAM. Senza questo filtro ricostruiremmo lo
// snapshot a ogni giro di automazione, per niente.
void XerumAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (state::ids::MOD) || tree.hasType (state::ids::MODS) || tree.hasType (state::ids::ARP))
        triggerAsyncUpdate();
}

void XerumAudioProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (state::ids::MODS))
        triggerAsyncUpdate();
}

void XerumAudioProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (state::ids::MODS))
        triggerAsyncUpdate();
}

// Coalizzato invece di ricostruito dentro la callback: state::setMods riscrive l'intera lista
// come removeAllChildren + N append, cioè 2N notifiche per un singolo drag di depth. Pubblicarne
// una per notifica significherebbe pubblicare anche gli stati intermedi, fra cui quello a lista
// vuota subito dopo removeAllChildren: se un blocco audio cade in quella finestra la modulazione
// sparisce per 128 campioni, che su level o pan è un click. Un solo rebuild per giro di message
// loop pubblica soltanto lo stato finale. È la stessa ragione per cui bridge::StateChannel
// coalizza il suo emitState.
void XerumAudioProcessor::handleAsyncUpdate()
{
    // Tutti e due, senza guardare quale dei due nodi si sia mosso: ricostruirli costa una
    // manciata di confronti di stringhe e una tokenizzazione di sedici numeri, una volta per giro
    // di message loop. Un flag per nodo sarebbe uno stato in piu' da tenere allineato in cambio
    // di niente.
    rebuildModSnapshot();
    rebuildArpSnapshot();
}

int XerumAudioProcessor::wavetableIndexFromParam() const noexcept
{
    if (paramWtIndex_ == nullptr)
        return 0;

    // `wtIndex` è un AudioParameterChoice: il valore grezzo è già l'indice.
    return juce::jlimit (0, wavetables_.getNumTables() - 1,
                         (int) paramWtIndex_->load (std::memory_order_relaxed));
}

engine::EngineParams XerumAudioProcessor::collectParams() const noexcept
{
    // Thin caller: tutta la denormalizzazione/arrotondamento vive in
    // params::collectEngineParams (ParamCollect.h), esercitata direttamente dai test con un
    // accessor finto. Qui si passa solo una lambda che legge paramSlots_, gia' risolto nel
    // costruttore: una singola lettura d'array indicizzata da params::ParamSlot, senza
    // hashing ne' confronto di stringhe a runtime — stesso costo per blocco di prima.
    return params::collectEngineParams (
        [this] (params::ParamSlot slot) noexcept -> float
        {
            const auto* raw = paramSlots_[(size_t) slot];
            if (raw != nullptr)
                return raw->load (std::memory_order_relaxed);

            // Puntatore nullo: strutturalmente irraggiungibile (ogni slot viene da
            // apvts_.getRawParameterValue() sullo stesso id che params::createParameterLayout()
            // registra da ParameterTable.h), ma se mai succedesse level deve tornare al guadagno
            // pieno (non passa da denormalise(), vedi ParamCollect.h), non ammutolire lo
            // strumento; per tutti gli altri 0.0f e' innocuo quanto lo era prima.
            return slot == params::ParamSlot::level ? 1.0f : 0.0f;
        });
}

void XerumAudioProcessor::parameterChanged (const juce::String& id, float)
{
    // Può arrivare dal thread audio (automazione host): qui si marca soltanto.
    if (id == "wtIndex")
        wavetableDirty_.store (true, std::memory_order_release);
}

void XerumAudioProcessor::timerCallback()
{
    if (! wavetableDirty_.exchange (false, std::memory_order_acquire))
        return;

    const juce::ScopedLock lock (wavetableLock_); // vedi commento sul membro: serializza con prepareToPlay

    const auto index = wavetableIndexFromParam();

    if (index == lastWavetableIndex_)
        return;

    const auto* previousActive = wavetables_.active();
    wavetables_.setActive (index); // alloca: message thread

    // setActive() lascia la tavola precedente attiva se la costruzione fallisce (blob assente,
    // corrotto, o frame troppo corto): il puntatore pubblicato non cambia. Se non lo notiamo e
    // avanziamo comunque lastWavetableIndex_, quell'indice non verra' piu' ritentato finche'
    // wtIndex non cambia di nuovo — un indice rotto resterebbe silenziosamente "gia' provato".
    if (wavetables_.active() == previousActive)
        return;

    lastWavetableIndex_ = index;

    // Pubblica solo il puntatore: l'applicazione alle voci avviene sul thread audio dentro
    // SynthEngine::process(), l'unico che può mutare quello stato in sicurezza.
    if (const auto* table = wavetables_.active())
        engine_->setPendingWavetable (table);
}

void XerumAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Costruire la tavola alloca: qui è lecito, in processBlock no. Il thread audio non gira
    // ancora, quindi applicarla direttamente alle voci è sicuro. Il lock serializza solo con
    // timerCallback() (vedi commento sul membro): nello Standalone questo può girare su un
    // thread diverso dal message thread.
    const juce::ScopedLock lock (wavetableLock_);

    const auto index = wavetableIndexFromParam();
    wavetables_.setActive (index);
    lastWavetableIndex_ = index;
    engine_->setWavetable (wavetables_.active());

    engine::EngineSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();
    engine_->prepare (spec);

    // Ogni riavvio dello stream riparte "vergine": un valore gia' visto in una sessione
    // precedente non deve essere considerato "gia' mandato" in questa, altrimenti la prima
    // posizione della rotella dopo un riavvio (identica all'ultima prima dello stop) non
    // verrebbe mai iniettata nel buffer.
    lastSentPitchBend_ = -1;
    lastSentModWheel_ = -1;
}

void XerumAudioProcessor::releaseResources()
{
    keyboardState_.reset();
    engine_->reset();
}

bool XerumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void XerumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    buffer.clear();

    // Non `const`: collectParams() ritorna per valore e il BPM si aggiunge qui, non là dentro —
    // params::collectEngineParams legge solo l'APVTS, e il tempo non è un parametro.
    auto params = collectParams();

    // Il tempo serve all'LFO quando `lsync` è attivo e **sempre** all'arpeggiatore, la cui
    // divisione è tempo-sincrona in tutti e due i regimi. Senza playhead (Standalone, qualche
    // render offline) o senza BPM esposto si resta a 120, lo stesso fallback che dsp::syncedRateHz
    // applica per conto suo: nessun ramo speciale da mantenere.
    //
    // PPQ e `isPlaying` servono al solo arpeggiatore, ed è la coppia che lo fa agganciare alla
    // timeline dell'host quando il transport gira. Un host che dica "sto suonando" ma non esponga
    // la posizione non dà niente a cui agganciarsi: in quel caso l'arp resta nel regime libero,
    // che è il comportamento giusto e non un ripiego.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                params.bpm = (float) *bpm;

            if (const auto ppq = position->getPpqPosition())
            {
                params.ppqPosition = *ppq;
                params.transportPlaying = position->getIsPlaying();
            }
        }

    engine_->setParams (params);

    if (paramVolume_ != nullptr)
    {
        const float v = paramVolume_->load (std::memory_order_relaxed);
        // v e' gia' il guadagno lineare 0..1 dell'APVTS: nessun boost fisso qui, l'headroom
        // vive per intero in SynthVoice::kVoiceHeadroomGain (vedi il suo commento).
        engine_->setMasterGainLinear (v <= 0.0f ? 0.0f : v);
    }

    // Le rotelle della UI diventano messaggi veri, e solo quando cambiano: mandarli a ogni blocco
    // riscriverebbe di continuo sopra un controller hardware che sta mandando gli stessi CC.
    if (const auto bend = uiPitchBend_.load (std::memory_order_relaxed);
        bend >= 0 && bend != lastSentPitchBend_)
    {
        midi.addEvent (juce::MidiMessage::pitchWheel (1, bend), 0);
        lastSentPitchBend_ = bend;
    }

    if (const auto mw = uiModWheel_.load (std::memory_order_relaxed);
        mw >= 0 && mw != lastSentModWheel_)
    {
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, mw), 0);
        lastSentModWheel_ = mw;
    }

    // Merge notes played on the editor's on-screen keyboard into the host MIDI stream.
    // MidiKeyboardState takes a brief CriticalSection internally (JUCE's standard pattern);
    // contention only happens on UI note on/off, never on the steady-state path.
    keyboardState_.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    engine_->process (buffer, midi);

    // Picchi del blocco per l'editor. Finché l'engine non espone un tap pre-master, "in" e "out"
    // ricevono lo stesso picco post-master; il meter IN diventerà reale con la fase DSP.
    // engine::storePeak non è una CAS: va bene perché il thread audio è l'unico scrittore e il
    // lettore (timer dell'editor) fa solo exchange(0) — l'argomento sta per intero nel suo commento.
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    engine::storePeak (meters_.outPeak, peak);
    engine::storePeak (meters_.inPeak,  peak);

    // I cinque livelli delle sorgenti del mod matrix: è ciò che fa muovere gli anelli attorno ai
    // knob modulati. Prima arrivava il solo LFO e la UI riempiva le altre quattro con costanti,
    // quindi l'anello mostrava la profondità giusta e il movimento sbagliato.
    //
    // -1..1: è il puntino che il tab LFO della UI disegna. A differenza dei picchi non si
    // accumula con storePeak ma si sovrascrive: un LFO bipolare non ha un "picco di blocco" che
    // significhi qualcosa, quello che serve è il valore di adesso. Stesso discorso per il mod
    // wheel, che è una posizione e non un transitorio.
    meters_.lfo.store (engine_->getLfoLevel(), std::memory_order_relaxed);
    meters_.mw.store  (engine_->getModWheelLevel(), std::memory_order_relaxed);

    // L'indice di griglia dell'arp: è ciò che accende il riquadro sul passo in riproduzione nel
    // tab Arp. Istantaneo come i due sopra — è una posizione dentro il pattern, non un picco.
    meters_.arpStep.store (engine_->getArpStep(), std::memory_order_relaxed);

    // Quali note stanno suonando: e' cio' che accende i tasti nella striscia della UI.
    meters_.notesLo.store (engine_->getActiveNotesLo(), std::memory_order_relaxed);
    meters_.notesHi.store (engine_->getActiveNotesHi(), std::memory_order_relaxed);

    // Unipolari e più veloci di un frame del meter: si accumula il massimo, come per i picchi
    // audio, e il lettore lo azzera. Il motore ha già preso il massimo sulle sotto-fette di
    // questo blocco; qui si tiene il massimo fra i blocchi che cadono nello stesso frame.
    engine::storePeak (meters_.env,  engine_->getEnvLevel());
    engine::storePeak (meters_.env2, engine_->getEnv2Level());
    engine::storePeak (meters_.vel,  engine_->getVelocityLevel());
}

juce::AudioProcessorEditor* XerumAudioProcessor::createEditor()
{
   #if XERUM_HEADLESS_TESTS
    // XerumTests compila il processore senza juce_gui_extra: nessun editor, e nessuno lo chiede.
    return nullptr;
   #else
    return new XerumAudioProcessorEditor (*this);
   #endif
}

void XerumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts_.copyState();

    // La versione del *formato*, sulla radice: è il ramo da cui una migrazione futura può
    // partire. Vedi state::kStateVersion e il suo changelog.
    state::stampSchemaVersion (state);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void XerumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts_.state.getType()))
        {
            // Siamo sul message thread: ricreare i figli mancanti e notificare è sicuro.
            JUCE_ASSERT_MESSAGE_THREAD

            const auto incoming = juce::ValueTree::fromXml (*xml);

            // Il formato dello stato che stiamo leggendo. Oggi c'è un solo formato e non c'è
            // niente da migrare, ma il numero va letto comunque: è il punto in cui la prima
            // migrazione si innesterà, ed è l'unica ragione per cui è stato scritto.
            // Uno stato senza l'attributo (tutto ciò che è stato salvato finora) vale 1.
            const auto version = state::schemaVersionOf (incoming);

            // Uno stato più nuovo del formato che conosciamo (progetto salvato da una versione
            // successiva del plugin): lo si legge comunque per quel che si può, perché
            // rifiutarlo butterebbe via il lavoro dell'utente senza dirglielo.
            jassert (version <= state::kStateVersion);
            juce::ignoreUnused (version);

            // Prima di sostituire l'albero, ogni parametro torna al suo default.
            //
            // replaceState() non cicla sullo schema, cicla sui figli del file: un parametro
            // assente dallo stato salvato — perché aggiunto dopo che l'utente ha salvato quel
            // progetto — non riceve nessuna notifica e si tiene il valore corrente, cioè un
            // pezzo del suono precedente. Azzerando prima, il verso dell'iterazione torna
            // quello giusto (schema corrente, valori pescati dal file) e dà gratis le tre
            // regole: aggiunto -> default, rimosso -> ignorato, nessun residuo.
            params::resetToDefaults (apvts_);

            apvts_.replaceState (incoming);

            // replaceState() sostituisce l'oggetto albero, non lo modifica: da qui in poi
            // l'ordine conta. Prima i figli non parametrici (uno stato salvato prima che MODS
            // esistesse non li ha), poi il riaggancio del listener — restare su quello vecchio
            // significherebbe non vedere mai più cambiare una mod — e infine lo snapshot.
            state::ensureChildren (apvts_.state);
            listenToState (apvts_.state);

            // Un rebuild ancora in coda riguarderebbe l'albero appena buttato via; e questo qui
            // è sincrono, non coalizzato, perché il thread audio sta già rendendo con le
            // assegnazioni del preset precedente: aspettare il prossimo giro di message loop
            // vorrebbe dire suonare il preset nuovo con le mod di quello vecchio.
            cancelPendingUpdate();
            rebuildModSnapshot();

            // Sincrono anche questo, e per la stessa ragione: caricare un preset mentre l'audio
            // gira con il pattern d'arpeggio di quello precedente e' lo stesso problema del mod
            // matrix, con un sintomo piu' evidente — la sequenza sbagliata si sente a ogni passo.
            rebuildArpSnapshot();

            stateReplaced_.sendChangeMessage();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XerumAudioProcessor();
}
