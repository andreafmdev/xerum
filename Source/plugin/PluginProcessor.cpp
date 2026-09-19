#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "parameters/ParamCollect.h"

namespace
{
// Ogni quanto il timer del processore controlla se wtIndex e' cambiato.
constexpr int kWavetablePollHz = 25;
} // namespace

SerumStyleSynthAudioProcessor::SerumStyleSynthAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", params::createParameterLayout()),
      engine_ (std::make_unique<engine::SynthEngine>())
{
    state::ensureChildren (apvts_.state);

    // Risolti una volta sola qui: collectParams() indicizza paramSlots_ con params::ParamSlot,
    // nessuna ricerca per nome sul thread audio.
    paramSlots_[(size_t) params::ParamSlot::oscOn] = apvts_.getRawParameterValue ("oscOn");
    paramSlots_[(size_t) params::ParamSlot::wtpos] = apvts_.getRawParameterValue ("wtpos");
    paramSlots_[(size_t) params::ParamSlot::oct] = apvts_.getRawParameterValue ("oct");
    paramSlots_[(size_t) params::ParamSlot::semi] = apvts_.getRawParameterValue ("semi");
    paramSlots_[(size_t) params::ParamSlot::fine] = apvts_.getRawParameterValue ("fine");
    paramSlots_[(size_t) params::ParamSlot::level] = apvts_.getRawParameterValue ("level");
    paramSlots_[(size_t) params::ParamSlot::unison] = apvts_.getRawParameterValue ("unison");
    paramSlots_[(size_t) params::ParamSlot::detune] = apvts_.getRawParameterValue ("detune");
    paramSlots_[(size_t) params::ParamSlot::filtOn] = apvts_.getRawParameterValue ("filtOn");
    paramSlots_[(size_t) params::ParamSlot::ftype] = apvts_.getRawParameterValue ("ftype");
    paramSlots_[(size_t) params::ParamSlot::slope] = apvts_.getRawParameterValue ("slope");
    paramSlots_[(size_t) params::ParamSlot::cutoff] = apvts_.getRawParameterValue ("cutoff");
    paramSlots_[(size_t) params::ParamSlot::res] = apvts_.getRawParameterValue ("res");
    paramSlots_[(size_t) params::ParamSlot::drive] = apvts_.getRawParameterValue ("drive");
    paramSlots_[(size_t) params::ParamSlot::keytrk] = apvts_.getRawParameterValue ("keytrk");
    paramSlots_[(size_t) params::ParamSlot::att] = apvts_.getRawParameterValue ("att");
    paramSlots_[(size_t) params::ParamSlot::dec] = apvts_.getRawParameterValue ("dec");
    paramSlots_[(size_t) params::ParamSlot::sus] = apvts_.getRawParameterValue ("sus");
    paramSlots_[(size_t) params::ParamSlot::rel] = apvts_.getRawParameterValue ("rel");
    paramSlots_[(size_t) params::ParamSlot::envVel] = apvts_.getRawParameterValue ("envVel");
    paramSlots_[(size_t) params::ParamSlot::pan] = apvts_.getRawParameterValue ("pan");
    paramSlots_[(size_t) params::ParamSlot::bypass] = apvts_.getRawParameterValue ("bypass");

    // I sei dell'LFO: i parametri esistevano già in parameters.json, mancava solo chi li
    // risolvesse. Senza queste righe collectParams() troverebbe puntatori nulli e l'LFO
    // girerebbe con la forma, il rate e la fade di default, ignorando i knob del tab LFO.
    paramSlots_[(size_t) params::ParamSlot::lshape] = apvts_.getRawParameterValue ("lshape");
    paramSlots_[(size_t) params::ParamSlot::lrate] = apvts_.getRawParameterValue ("lrate");
    paramSlots_[(size_t) params::ParamSlot::lsync] = apvts_.getRawParameterValue ("lsync");
    paramSlots_[(size_t) params::ParamSlot::lphase] = apvts_.getRawParameterValue ("lphase");
    paramSlots_[(size_t) params::ParamSlot::lfade] = apvts_.getRawParameterValue ("lfade");
    paramSlots_[(size_t) params::ParamSlot::lretrig] = apvts_.getRawParameterValue ("lretrig");

    // wtIndex e volume non passano da EngineParams/collectEngineParams: restano a parte.
    paramWtIndex_ = apvts_.getRawParameterValue ("wtIndex");
    paramVolume_ = apvts_.getRawParameterValue ("volume");

    listenToState (apvts_.state);

    // Subito, non solo al primo cambiamento: un progetto riaperto con delle assegnazioni già
    // salvate deve suonare modulato dal primo blocco, senza aspettare che l'utente tocchi
    // qualcosa. (setStateInformation, se arriva, rifà comunque il giro sul nuovo albero.)
    rebuildModSnapshot();

    apvts_.addParameterListener ("wtIndex", this);
    startTimerHz (kWavetablePollHz);
}

SerumStyleSynthAudioProcessor::~SerumStyleSynthAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    listenedState_.removeListener (this);
    apvts_.removeParameterListener ("wtIndex", this);
}

void SerumStyleSynthAudioProcessor::listenToState (juce::ValueTree root)
{
    listenedState_.removeListener (this);
    listenedState_ = std::move (root);
    listenedState_.addListener (this);
}

void SerumStyleSynthAudioProcessor::rebuildModSnapshot()
{
    engine::ModSnapshot snapshot;
    engine::buildModSnapshot (apvts_.state.getChildWithName (state::ids::MODS), snapshot);
    engine_->setMods (snapshot);
}

// Il listener sta sulla radice dell'APVTS, quindi qui passa *ogni* proprietà dell'albero: ogni
// movimento di ogni knob, che vive nei figli PARAM. Senza questo filtro ricostruiremmo lo
// snapshot a ogni giro di automazione, per niente.
void SerumStyleSynthAudioProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree.hasType (state::ids::MOD) || tree.hasType (state::ids::MODS))
        triggerAsyncUpdate();
}

void SerumStyleSynthAudioProcessor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (state::ids::MODS))
        triggerAsyncUpdate();
}

void SerumStyleSynthAudioProcessor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
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
void SerumStyleSynthAudioProcessor::handleAsyncUpdate()
{
    rebuildModSnapshot();
}

int SerumStyleSynthAudioProcessor::wavetableIndexFromParam() const noexcept
{
    if (paramWtIndex_ == nullptr)
        return 0;

    // `wtIndex` è un AudioParameterChoice: il valore grezzo è già l'indice.
    return juce::jlimit (0, wavetables_.getNumTables() - 1,
                         (int) paramWtIndex_->load (std::memory_order_relaxed));
}

engine::EngineParams SerumStyleSynthAudioProcessor::collectParams() const noexcept
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

void SerumStyleSynthAudioProcessor::parameterChanged (const juce::String& id, float)
{
    // Può arrivare dal thread audio (automazione host): qui si marca soltanto.
    if (id == "wtIndex")
        wavetableDirty_.store (true, std::memory_order_release);
}

void SerumStyleSynthAudioProcessor::timerCallback()
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

void SerumStyleSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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
}

void SerumStyleSynthAudioProcessor::releaseResources()
{
    keyboardState_.reset();
    engine_->reset();
}

bool SerumStyleSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void SerumStyleSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    buffer.clear();

    // Non `const`: collectParams() ritorna per valore e il BPM si aggiunge qui, non là dentro —
    // params::collectEngineParams legge solo l'APVTS, e il tempo non è un parametro.
    auto params = collectParams();

    // Il tempo serve al solo LFO, e solo quando `lsync` è attivo. Senza playhead (Standalone,
    // qualche render offline) o senza BPM esposto si resta a 120, lo stesso fallback che
    // dsp::syncedRateHz applica per conto suo: nessun ramo speciale da mantenere.
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                params.bpm = (float) *bpm;

    engine_->setParams (params);

    if (paramVolume_ != nullptr)
    {
        const float v = paramVolume_->load (std::memory_order_relaxed);
        // v e' gia' il guadagno lineare 0..1 dell'APVTS: nessun boost fisso qui, l'headroom
        // vive per intero in SynthVoice::kVoiceHeadroomGain (vedi il suo commento).
        engine_->setMasterGainLinear (v <= 0.0f ? 0.0f : v);
    }

    // Merge notes played on the editor's on-screen keyboard into the host MIDI stream.
    // MidiKeyboardState takes a brief CriticalSection internally (JUCE's standard pattern);
    // contention only happens on UI note on/off, never on the steady-state path.
    keyboardState_.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    engine_->process (buffer, midi);

    // Picchi del blocco per l'editor. Finché l'engine non espone un tap pre-master, "in" e "out"
    // ricevono lo stesso picco post-master; il meter IN diventerà reale con la fase DSP.
    // store(jmax(load, peak)) non è una CAS: va bene perché il thread audio è l'unico scrittore
    // e il lettore (timer dell'editor) fa solo exchange(0), quindi non c'è race sulla read-modify-write.
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    meters_.outPeak.store (juce::jmax (meters_.outPeak.load (std::memory_order_relaxed), peak), std::memory_order_relaxed);
    meters_.inPeak.store  (juce::jmax (meters_.inPeak.load  (std::memory_order_relaxed), peak), std::memory_order_relaxed);

    // -1..1: è il puntino che il tab LFO della UI disegna. A differenza dei picchi non si
    // accumula con jmax ma si sovrascrive: un LFO bipolare non ha un "picco di blocco" che
    // significhi qualcosa, quello che serve è il valore di adesso.
    meters_.lfo.store (engine_->getLfoLevel(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* SerumStyleSynthAudioProcessor::createEditor()
{
    return new SerumStyleSynthAudioProcessorEditor (*this);
}

void SerumStyleSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts_.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SerumStyleSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts_.state.getType()))
        {
            // Siamo sul message thread: ricreare i figli mancanti e notificare è sicuro.
            JUCE_ASSERT_MESSAGE_THREAD

            apvts_.replaceState (juce::ValueTree::fromXml (*xml));

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

            stateReplaced_.sendChangeMessage();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SerumStyleSynthAudioProcessor();
}
