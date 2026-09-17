#pragma once

/**
 * Phase 2 placeholder: own wavetable sample data on the message/UI thread,
 * publish a read-only view to the audio thread via atomic pointer swap
 * (or double-buffer). Never allocate inside processBlock.
 *
 * Declared now so engine/dsp can grow against a stable ownership model.
 */
namespace dsp
{
class WavetableStore
{
public:
    // Phase 2:
    // bool loadFromFile (const juce::File&);
    // const float* getActiveTable() const noexcept;
    // int getTableSize() const noexcept;
};
} // namespace dsp
