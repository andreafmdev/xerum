#pragma once
#include "engine/MeterFrame.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace bridge
{
/** Legge MeterFrame a 30 Hz sul message thread e manda un evento "meters" alla WebView.
    Esiste solo con l'editor aperto: costo zero a editor chiuso. */
class MeterChannel final : private juce::Timer
{
public:
    MeterChannel (engine::MeterFrame& frame, juce::WebBrowserComponent& view);
    ~MeterChannel() override { stopTimer(); }

private:
    void timerCallback() override;
    engine::MeterFrame& frame_;
    juce::WebBrowserComponent& view_;
};
} // namespace bridge
