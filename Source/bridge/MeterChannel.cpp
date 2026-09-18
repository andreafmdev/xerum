#include "bridge/MeterChannel.h"

namespace bridge
{
MeterChannel::MeterChannel (engine::MeterFrame& frame, juce::WebBrowserComponent& view)
    : frame_ (frame), view_ (view)
{
    startTimerHz (30);
}

void MeterChannel::timerCallback()
{
    // exchange azzera i picchi dopo la lettura; lfo/arpStep restano finché il DSP non li aggiorna.
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("in",      (double) frame_.inPeak.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("out",     (double) frame_.outPeak.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("lfo",     (double) frame_.lfo.load (std::memory_order_relaxed));
    obj->setProperty ("arpStep", frame_.arpStep.load (std::memory_order_relaxed));
    view_.emitEventIfBrowserIsVisible ("meters", juce::var (obj));
}
} // namespace bridge
