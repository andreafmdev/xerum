#include "bridge/MeterChannel.h"
#include "engine/NoteMask.h"

namespace bridge
{
MeterChannel::MeterChannel (engine::MeterFrame& frame, juce::WebBrowserComponent& view)
    : frame_ (frame), view_ (view)
{
    startTimerHz (30);
}

void MeterChannel::timerCallback()
{
    // exchange azzera i picchi dopo la lettura; lfo/mw/arpStep restano finché il DSP non li aggiorna.
    //
    // Chi sta di qua e chi sta di là non è una scelta di comodo, è la natura delle due grandezze:
    // i picchi (audio, env, env2, vel) sono transitori unipolari che fra due giri di timer — 33 ms
    // — possono nascere e morire, e l'exchange è l'unico modo di non perderli; l'LFO è bipolare
    // (un massimo perderebbe il segno o non scenderebbe mai) e il mod wheel è una posizione, non un
    // transitorio. Vedi il commento di engine::MeterFrame.
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("in",      (double) frame_.inPeak.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("out",     (double) frame_.outPeak.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("lfo",     (double) frame_.lfo.load (std::memory_order_relaxed));
    obj->setProperty ("env",     (double) frame_.env.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("env2",    (double) frame_.env2.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("vel",     (double) frame_.vel.exchange (0.0f, std::memory_order_relaxed));
    obj->setProperty ("mw",      (double) frame_.mw.load (std::memory_order_relaxed));
    obj->setProperty ("arpStep", frame_.arpStep.load (std::memory_order_relaxed));

    // Quattro parole da 32 bit e non due da 64: vedi engine::splitNoteMask.
    const auto words = engine::splitNoteMask (frame_.notesLo.load (std::memory_order_relaxed),
                                              frame_.notesHi.load (std::memory_order_relaxed));
    obj->setProperty ("n0", (double) words[0]);
    obj->setProperty ("n1", (double) words[1]);
    obj->setProperty ("n2", (double) words[2]);
    obj->setProperty ("n3", (double) words[3]);

    view_.emitEventIfBrowserIsVisible ("meters", juce::var (obj));
}
} // namespace bridge
