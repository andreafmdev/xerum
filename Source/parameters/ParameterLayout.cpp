#include "parameters/ParameterLayout.h"
#include "parameters/ParameterMapping.h"

namespace params
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> list;
    list.reserve ((size_t) kNumParams);
    for (const auto& spec : kTable)
        list.push_back (makeParameter (spec));
    return { list.begin(), list.end() };
}
} // namespace params
