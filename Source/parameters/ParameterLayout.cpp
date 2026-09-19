#include "parameters/ParameterLayout.h"
#include "parameters/ParameterMapping.h"
#include "parameters/PresetValue.h"

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

void resetToDefaults (juce::AudioProcessorValueTreeState& apvts)
{
    for (const auto& spec : kTable)
    {
        auto* parameter = apvts.getParameter (spec.id);

        if (parameter == nullptr)
            continue;

        // normalisedDefault() e non spec.def: per Kind::Int e Kind::Choice `def` e' il valore
        // grezzo (l'intero, l'indice), mentre setValueNotifyingHost vuole il normalizzato 0..1
        // sul range del parametro. E' la stessa regola che params::presetValue() applica ai
        // parametri che un preset non elenca, e vive in un posto solo apposta.
        parameter->setValueNotifyingHost (normalisedDefault (spec));
    }
}
} // namespace params
