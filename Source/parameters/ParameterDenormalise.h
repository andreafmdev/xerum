#pragma once

#include "parameters/ParameterTable.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>

namespace params
{
inline float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }

/**
 * Valore reale dal normalizzato 0..1. Stesse formule di WebUI/src/synth/mapping.ts.
 *
 * Vive in un header separato da ParameterMapping.h (che porta dentro
 * juce_audio_processors per makeParameter()/formatValue()) cosi' chi ha solo bisogno di
 * denormalizzare — come ParamCollect.h, incluso dai test — non trascina l'APVTS. Solo
 * juce_core, gia' linkato ovunque.
 */
inline float denormalise (const Spec& s, float v) noexcept
{
    const float x = clamp01 (v);
    switch (s.map)
    {
        case Map::Linear:    return s.min + x * (s.max - s.min);
        case Map::Log:       return s.min * std::pow (s.max / s.min, x);
        case Map::Db:        return x <= 0.0f ? -std::numeric_limits<float>::infinity() : 20.0f * std::log10 (x) + s.offset;
        case Map::MsSquared: return s.min + x * x * (s.max - s.min);
        case Map::None:      break;
    }
    return v;
}
} // namespace params
