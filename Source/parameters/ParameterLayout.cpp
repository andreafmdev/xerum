#include "parameters/ParameterLayout.h"
#include "parameters/ParameterIDs.h"

namespace params
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> paramsList;

    paramsList.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { masterGain, 1 },
        "Master Gain",
        juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f },
        -6.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    paramsList.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { osc1Level, 1 },
        "Osc 1 Level",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.8f));

    return { paramsList.begin(), paramsList.end() };
}
} // namespace params
