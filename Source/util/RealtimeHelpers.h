#pragma once

/** Utilities and conventions for the audio thread.
 *
 * Real-time rules (audio callback / processBlock):
 * - No heap allocations
 * - No locks / mutexes
 * - No file or network I/O
 * - No logging
 *
 * Cross-thread data (wavetables, presets) must use lock-free handoff
 * (double buffer, AbstractFifo, or atomic pointer swap) — see WavetableStore.
 */
namespace util
{
} // namespace util
