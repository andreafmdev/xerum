#include "bridge/WebRelays.h"

#include "parameters/ParameterTable.h"

namespace bridge
{
WebRelays::WebRelays()
{
    // Un relay per voce della tabella generata: il nome è l'id del parametro.
    for (const auto& s : params::kTable)
    {
        switch (s.kind)
        {
            case params::Kind::Bool:
                toggles_.push_back ({ s.id, std::make_unique<juce::WebToggleButtonRelay> (s.id) });
                break;

            case params::Kind::Choice:
                combos_.push_back ({ s.id, std::make_unique<juce::WebComboBoxRelay> (s.id) });
                break;

            case params::Kind::Float:
            case params::Kind::Int:
                sliders_.push_back ({ s.id, std::make_unique<juce::WebSliderRelay> (s.id) });
                break;
        }
    }
}

juce::WebBrowserComponent::Options WebRelays::applyTo (juce::WebBrowserComponent::Options options) const
{
    for (auto& s : sliders_) options = options.withOptionsFrom (*s.relay);
    for (auto& t : toggles_) options = options.withOptionsFrom (*t.relay);
    for (auto& c : combos_)  options = options.withOptionsFrom (*c.relay);

    return options;
}

void WebRelays::attach (juce::AudioProcessorValueTreeState& apvts, juce::UndoManager* undo)
{
    for (auto& s : sliders_)
        if (auto* p = apvts.getParameter (s.id))
            sliderAttachments_.push_back (std::make_unique<juce::WebSliderParameterAttachment> (*p, *s.relay, undo));

    for (auto& t : toggles_)
        if (auto* p = apvts.getParameter (t.id))
            toggleAttachments_.push_back (std::make_unique<juce::WebToggleButtonParameterAttachment> (*p, *t.relay, undo));

    for (auto& c : combos_)
        if (auto* p = apvts.getParameter (c.id))
            comboAttachments_.push_back (std::make_unique<juce::WebComboBoxParameterAttachment> (*p, *c.relay, undo));

    // Ogni relay deve aver trovato il parametro omonimo: se scatta, id tabella e id APVTS divergono.
    jassert ((int) (sliderAttachments_.size() + toggleAttachments_.size() + comboAttachments_.size())
             == numRelays());
}
} // namespace bridge
