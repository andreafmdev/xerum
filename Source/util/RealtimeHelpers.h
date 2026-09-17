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
 * (double buffer, AbstractFifo, or atomic pointer swap) — see WavetableStore (phase 2).
 */
namespace util
{
/** Optional debug tone frequency (Hz). Engine ignores this when kEnableTestTone is false. */
inline constexpr float kTestToneHz = 440.0f;

/** Default: silence. Set true only for local smoke tests of the voice path. */
inline constexpr bool kEnableTestTone = false;
} // namespace util
