#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <vector>

namespace bridge
{
/** Un relay JUCE per parametro (slider per float/int, toggle per bool, combo per choice).
    Il nome del relay coincide con l'id del parametro nell'APVTS, così la WebUI può
    recuperarlo con getSliderState(id) / getToggleState(id) / getComboBoxState(id).

    Ordine imposto da JUCE: costruire i relay → applyTo(Options) → costruire la WebView → attach(). */
class WebRelays
{
public:
    WebRelays();

    /** Aggiunge alle Options i native function / initialisation data di tutti i relay.
        Va chiamata prima di costruire la WebBrowserComponent. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options) const;

    /** Collega ogni relay al parametro omonimo dell'APVTS. Va chiamata dopo che la
        WebBrowserComponent è stata costruita. */
    void attach (juce::AudioProcessorValueTreeState& apvts, juce::UndoManager* undo = nullptr);

    int numRelays() const noexcept { return (int) (sliders_.size() + toggles_.size() + combos_.size()); }

private:
    struct Slider { juce::String id; std::unique_ptr<juce::WebSliderRelay> relay; };
    struct Toggle { juce::String id; std::unique_ptr<juce::WebToggleButtonRelay> relay; };
    struct Combo  { juce::String id; std::unique_ptr<juce::WebComboBoxRelay> relay; };

    std::vector<Slider> sliders_;
    std::vector<Toggle> toggles_;
    std::vector<Combo>  combos_;

    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>>       sliderAttachments_;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments_;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>>     comboAttachments_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebRelays)
};
} // namespace bridge
