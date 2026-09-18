#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

SerumStyleSynthAudioProcessor::SerumStyleSynthAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", params::createParameterLayout()),
      engine_ (std::make_unique<engine::SynthEngine>())
{
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
        engine_->setMasterGainLinear (v <= 0.0f ? 0.0f : v * juce::Decibels::decibelsToGain (6.0f)); // v è lineare 0..1; +6 dB di headroom
    }

    // "level" è riservato al mix degli oscillatori (fase 3): lo leggiamo già qui per tenere il puntatore vivo.
    if (levelParam_ != nullptr)
        (void) levelParam_->load (std::memory_order_relaxed);

    // Merge notes played on the editor's on-screen keyboard into the host MIDI stream.
    // MidiKeyboardState takes a brief CriticalSection internally (JUCE's standard pattern);
    // contention only happens on UI note on/off, never on the steady-state path.
    keyboardState_.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    engine_->process (buffer, midi);
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
            apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SerumStyleSynthAudioProcessor();
}
