#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

namespace
{
// Headroom di +6 dB in guadagno lineare. Calcolato una sola volta all'avvio perché
// decibelsToGain() usa std::pow: niente libm sul thread audio.
const float kHeadroomGain = juce::Decibels::decibelsToGain (6.0f);
} // namespace

SerumStyleSynthAudioProcessor::SerumStyleSynthAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", params::createParameterLayout()),
      engine_ (std::make_unique<engine::SynthEngine>())
{
    state::ensureChildren (apvts_.state);

    volumeParam_ = apvts_.getRawParameterValue ("volume");
    levelParam_ = apvts_.getRawParameterValue ("level");
}

void SerumStyleSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
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

    if (volumeParam_ != nullptr)
    {
        const float v = volumeParam_->load (std::memory_order_relaxed);
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
