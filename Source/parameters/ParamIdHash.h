#pragma once

#include <cstdint>

namespace params
{
/**
 * FNV-1a a 32 bit, `constexpr`: hash minimo per instradare un id di parametro (una stringa,
 * es. "cutoff") verso il puntatore atomico giusto in O(1) via `switch` (tabella di salto),
 * invece di una catena di confronti di stringhe.
 *
 * Vive qui, non dentro PluginProcessor.cpp, cosi' la proprieta' che conta — "gli id noti
 * producono hash distinti" — si puo' testare senza juce_audio_processors: questo header non
 * dipende da nient'altro che <cstdint>. Se due id collidessero, lo `switch` di
 * PluginProcessor::collectParams avrebbe due `case` con lo stesso valore e smetterebbe di
 * compilare, invece di instradare in silenzio al parametro sbagliato; il test in
 * Tests/EngineTests.cpp verifica la stessa cosa esplicitamente su tutta ParameterTable.h.
 */
constexpr std::uint32_t fnv1aParamId (const char* s) noexcept
{
    std::uint32_t hash = 2166136261u;
    while (*s != '\0')
    {
        hash ^= (std::uint32_t) (unsigned char) *s;
        hash *= 16777619u;
        ++s;
    }
    return hash;
}
} // namespace params
