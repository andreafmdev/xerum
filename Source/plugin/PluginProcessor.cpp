#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "parameters/ParameterMapping.h"

namespace
{
// Headroom di +6 dB in guadagno lineare. Calcolato una sola volta all'avvio perché
// decibelsToGain() usa std::pow: niente libm sul thread audio.
const float kHeadroomGain = juce::Decibels::decibelsToGain (6.0f);

// Ogni quanto il timer del processore controlla se wtIndex e' cambiato.
constexpr int kWavetablePollHz = 25;

/** Valore denormalizzato di un parametro, usando la spec già risolta in ParameterTable. */
float realValue (const params::Spec* spec, const std::atomic<float>* raw) noexcept
{
    if (raw == nullptr || spec == nullptr)
        return 0.0f;

    return params::denormalise (*spec, raw->load (std::memory_order_relaxed));
}

/** `ftype` è un AudioParameterChoice: il valore grezzo è già l'indice 0/1/2. */
dsp::StateVariableFilter::Type filterTypeFromChoice (const std::atomic<float>* raw) noexcept
{
    const int index = raw != nullptr ? (int) raw->load (std::memory_order_relaxed) : 0;
    switch (index)
    {
        case 1:  return dsp::StateVariableFilter::Type::highPass;
        case 2:  return dsp::StateVariableFilter::Type::bandPass;
        default: return dsp::StateVariableFilter::Type::lowPass;
    }
}
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
    specOct_ = params::find ("oct");
    paramSemi_ = apvts_.getRawParameterValue ("semi");
    specSemi_ = params::find ("semi");
    paramFine_ = apvts_.getRawParameterValue ("fine");
    specFine_ = params::find ("fine");

    paramLevel_ = apvts_.getRawParameterValue ("level");

    paramFiltOn_ = apvts_.getRawParameterValue ("filtOn");
    paramFtype_ = apvts_.getRawParameterValue ("ftype");
    paramSlope_ = apvts_.getRawParameterValue ("slope");

    paramCutoff_ = apvts_.getRawParameterValue ("cutoff");
    specCutoff_ = params::find ("cutoff");
    paramRes_ = apvts_.getRawParameterValue ("res");
    specRes_ = params::find ("res");
    paramDrive_ = apvts_.getRawParameterValue ("drive");
    specDrive_ = params::find ("drive");
    paramKeytrk_ = apvts_.getRawParameterValue ("keytrk");
    specKeytrk_ = params::find ("keytrk");

    paramAtt_ = apvts_.getRawParameterValue ("att");
    specAtt_ = params::find ("att");
    paramDec_ = apvts_.getRawParameterValue ("dec");
    specDec_ = params::find ("dec");
    paramSus_ = apvts_.getRawParameterValue ("sus");
    specSus_ = params::find ("sus");
    paramRel_ = apvts_.getRawParameterValue ("rel");
    specRel_ = params::find ("rel");
    paramEnvVel_ = apvts_.getRawParameterValue ("envVel");
    specEnvVel_ = params::find ("envVel");

    paramVolume_ = apvts_.getRawParameterValue ("volume");
    paramPan_ = apvts_.getRawParameterValue ("pan");
    specPan_ = params::find ("pan");
    paramBypass_ = apvts_.getRawParameterValue ("bypass");

    jassert (specOct_ != nullptr && specSemi_ != nullptr && specFine_ != nullptr
             && specCutoff_ != nullptr && specRes_ != nullptr && specDrive_ != nullptr
             && specKeytrk_ != nullptr && specAtt_ != nullptr && specDec_ != nullptr
             && specSus_ != nullptr && specRel_ != nullptr && specEnvVel_ != nullptr
             && specPan_ != nullptr);

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
    engine::EngineParams p;

    p.oscOn = paramOscOn_ != nullptr && paramOscOn_->load (std::memory_order_relaxed) >= 0.5f;
    p.framePosition = paramWtpos_ != nullptr ? paramWtpos_->load (std::memory_order_relaxed) : 0.0f;
    p.octave = (int) realValue (specOct_, paramOct_);
    p.semitones = (int) realValue (specSemi_, paramSemi_);
    p.fineCents = realValue (specFine_, paramFine_);

    // `level` ha mappa Db, ma il valore grezzo è già il guadagno lineare (vedi ParameterMapping.h).
    p.level = paramLevel_ != nullptr ? paramLevel_->load (std::memory_order_relaxed) : 1.0f;

    p.filterOn = paramFiltOn_ != nullptr && paramFiltOn_->load (std::memory_order_relaxed) >= 0.5f;
    p.filterType = filterTypeFromChoice (paramFtype_);
    p.filterStages = paramSlope_ != nullptr && paramSlope_->load (std::memory_order_relaxed) >= 0.5f ? 2 : 1;
    p.cutoffHz = realValue (specCutoff_, paramCutoff_);

    // res 0..100 % → Q 0.707 (Butterworth) … 20 (autoscillante quasi).
    p.resonanceQ = juce::jmap (realValue (specRes_, paramRes_) * 0.01f, 0.707f, 20.0f);

    // drive 0..24 dB → guadagno lineare pre-saturazione.
    p.driveGain = juce::Decibels::decibelsToGain (realValue (specDrive_, paramDrive_));
    p.keyTrack = realValue (specKeytrk_, paramKeytrk_) * 0.01f;

    p.attackSeconds = realValue (specAtt_, paramAtt_) * 0.001f;   // la mappa è in ms
    p.decaySeconds = realValue (specDec_, paramDec_) * 0.001f;
    p.sustain = realValue (specSus_, paramSus_) * 0.01f;
    p.releaseSeconds = realValue (specRel_, paramRel_) * 0.001f;
    p.velocityAmount = realValue (specEnvVel_, paramEnvVel_) * 0.01f;

    p.pan = realValue (specPan_, paramPan_) * 0.02f;              // -50..50 → -1..1
    p.bypass = paramBypass_ != nullptr && paramBypass_->load (std::memory_order_relaxed) >= 0.5f;

    return p;
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
    // ancora, quindi applicarla direttamente alle voci è sicuro.
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
