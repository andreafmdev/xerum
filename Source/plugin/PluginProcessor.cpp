#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "parameters/ParameterIDs.h"

SerumStyleSynthAudioProcessor::SerumStyleSynthAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_ (*this, nullptr, "PARAMS", params::createParameterLayout()),
      engine_ (std::make_unique<engine::SynthEngine>())
{
    masterGainParam_ = apvts_.getRawParameterValue (params::masterGain);
    osc1LevelParam_ = apvts_.getRawParameterValue (params::osc1Level);
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

    if (masterGainParam_ != nullptr)
    {
        const float gainDb = masterGainParam_->load();
        engine_->setMasterGainLinear (juce::Decibels::decibelsToGain (gainDb));
    }

    // osc1_level reserved for phase 3 oscillator mix; read to keep linker happy / future use.
    if (osc1LevelParam_ != nullptr)
        (void) osc1LevelParam_->load();

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
