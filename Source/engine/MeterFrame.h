#pragma once
#include <atomic>

namespace engine
{
/** Scritto dal thread audio con store(relaxed), letto dal timer dell'editor.
    Solo atomics: nessun lock, nessuna dipendenza GUI. */
struct MeterFrame
{
    std::atomic<float> inPeak  { 0.0f };   // picco pre-master del blocco
    std::atomic<float> outPeak { 0.0f };   // picco post-master
    std::atomic<float> lfo     { 0.0f };   // -1..1, zero finché l'LFO non esiste nel DSP
    std::atomic<int>   arpStep { 0 };      // 0..15, zero finché l'arp non esiste
};
} // namespace engine
