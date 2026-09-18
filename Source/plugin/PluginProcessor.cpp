#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "parameters/ParamCollect.h"
#include "parameters/ParamIdHash.h"

namespace
{
// Headroom di +6 dB in guadagno lineare. Calcolato una sola volta all'avvio perché
// decibelsToGain() usa std::pow: niente libm sul thread audio.
const float kHeadroomGain = juce::Decibels::decibelsToGain (6.0f);

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

    paramOscOn_ = apvts_.getRawParameterValue ("oscOn");
    paramWtIndex_ = apvts_.getRawParameterValue ("wtIndex");
    paramWtpos_ = apvts_.getRawParameterValue ("wtpos");

    paramOct_ = apvts_.getRawParameterValue ("oct");
    paramSemi_ = apvts_.getRawParameterValue ("semi");
    paramFine_ = apvts_.getRawParameterValue ("fine");

    paramLevel_ = apvts_.getRawParameterValue ("level");

    paramFiltOn_ = apvts_.getRawParameterValue ("filtOn");
    paramFtype_ = apvts_.getRawParameterValue ("ftype");
    paramSlope_ = apvts_.getRawParameterValue ("slope");

    paramCutoff_ = apvts_.getRawParameterValue ("cutoff");
    paramRes_ = apvts_.getRawParameterValue ("res");
    paramDrive_ = apvts_.getRawParameterValue ("drive");
    paramKeytrk_ = apvts_.getRawParameterValue ("keytrk");

    paramAtt_ = apvts_.getRawParameterValue ("att");
    paramDec_ = apvts_.getRawParameterValue ("dec");
    paramSus_ = apvts_.getRawParameterValue ("sus");
    paramRel_ = apvts_.getRawParameterValue ("rel");
    paramEnvVel_ = apvts_.getRawParameterValue ("envVel");

    paramVolume_ = apvts_.getRawParameterValue ("volume");
    paramPan_ = apvts_.getRawParameterValue ("pan");
    paramBypass_ = apvts_.getRawParameterValue ("bypass");

    apvts_.addParameterListener ("wtIndex", this);
    startTimerHz (kWavetablePollHz);
}

SerumStyleSynthAudioProcessor::~SerumStyleSynthAudioProcessor()
{
    stopTimer();
    apvts_.removeParameterListener ("wtIndex", this);
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
    // accessor finto. Qui si passa solo una lambda che legge i puntatori atomici già
    // risolti nel costruttore — instradamento O(1) via `switch` su hash (params::fnv1aParamId,
    // ParamIdHash.h), non una catena di confronti: stesso costo per blocco di prima.
    return params::collectEngineParams (
        [this] (const char* id) noexcept -> float
        {
            const auto load = [] (const std::atomic<float>* raw, float fallback) noexcept -> float
            {
                return raw != nullptr ? raw->load (std::memory_order_relaxed) : fallback;
            };

            switch (params::fnv1aParamId (id))
            {
                case params::fnv1aParamId ("oscOn"):  return load (paramOscOn_, 0.0f);
                case params::fnv1aParamId ("wtpos"):  return load (paramWtpos_, 0.0f);
                case params::fnv1aParamId ("oct"):    return load (paramOct_, 0.0f);
                case params::fnv1aParamId ("semi"):   return load (paramSemi_, 0.0f);
                case params::fnv1aParamId ("fine"):   return load (paramFine_, 0.0f);
                // Fallback 1.0f (guadagno pieno), non 0.0f: `level` non passa da denormalise()
                // (vedi ParamCollect.h), quindi qui il fallback e' gia' il valore finale. Un
                // puntatore nullo non deve far ammutolire lo strumento.
                case params::fnv1aParamId ("level"):  return load (paramLevel_, 1.0f);
                case params::fnv1aParamId ("filtOn"): return load (paramFiltOn_, 0.0f);
                case params::fnv1aParamId ("ftype"):  return load (paramFtype_, 0.0f);
                case params::fnv1aParamId ("slope"):  return load (paramSlope_, 0.0f);
                case params::fnv1aParamId ("cutoff"): return load (paramCutoff_, 0.0f);
                case params::fnv1aParamId ("res"):    return load (paramRes_, 0.0f);
                case params::fnv1aParamId ("drive"):  return load (paramDrive_, 0.0f);
                case params::fnv1aParamId ("keytrk"): return load (paramKeytrk_, 0.0f);
                case params::fnv1aParamId ("att"):    return load (paramAtt_, 0.0f);
                case params::fnv1aParamId ("dec"):    return load (paramDec_, 0.0f);
                case params::fnv1aParamId ("sus"):    return load (paramSus_, 0.0f);
                case params::fnv1aParamId ("rel"):    return load (paramRel_, 0.0f);
                case params::fnv1aParamId ("envVel"): return load (paramEnvVel_, 0.0f);
                case params::fnv1aParamId ("pan"):    return load (paramPan_, 0.0f);
                case params::fnv1aParamId ("bypass"): return load (paramBypass_, 0.0f);
                default:
                    jassertfalse; // id sconosciuto: collectEngineParams ne ha chiesto uno non mappato qui
                    return 0.0f;
            }
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

    wavetables_.setActive (index); // alloca: message thread
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

    const auto params = collectParams();
    engine_->setParams (params);

    if (paramVolume_ != nullptr)
    {
        const float v = paramVolume_->load (std::memory_order_relaxed);
        engine_->setMasterGainLinear (v <= 0.0f ? 0.0f : v * kHeadroomGain); // v è lineare 0..1; +6 dB di headroom
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

            state::ensureChildren (apvts_.state);
            stateReplaced_.sendChangeMessage();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SerumStyleSynthAudioProcessor();
}
