#pragma once

#include "parameters/ParameterDenormalise.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <memory>

namespace params
{
/** I letterali della tabella sono UTF-8 ("Vel → amp", "°"): juce::String(const char*)
    li leggerebbe come ASCII, con assertion in debug e testo corrotto. */
inline juce::String utf8 (const char* t) { return juce::String (juce::CharPointer_UTF8 (t)); }

// clamp01() e denormalise() vivono in ParameterDenormalise.h: non serve juce_audio_processors
// per denormalizzare, solo per costruire i RangedAudioParameter qui sotto.

inline juce::String signedInt (int n) { return (n > 0 ? "+" : "") + juce::String (n); }

/** Testo per l'host (automazione, generic editor). Stesso output di formatValue() in TS. */
inline juce::String formatValue (const Spec& s, float v)
{
    if (s.kind == Kind::Bool)   return v >= 0.5f ? "On" : "Off";
    if (s.kind == Kind::Choice) return utf8 (s.options[juce::roundToInt (clamp01 (v) * (float) (s.numOptions - 1))]);
    if (s.kind == Kind::Int)    return signedInt (juce::roundToInt (denormalise (s, v)));

    const float real = denormalise (s, v);
    switch (s.label)
    {
        case Label::Hz:      return real >= 1000.0f ? juce::String (real / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (real)) + " Hz";
        case Label::Time:    return real >= 1000.0f ? juce::String (real / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (real)) + " ms";
        case Label::Pan:     { const int c = juce::roundToInt (real); return c == 0 ? "C" : c < 0 ? juce::String (-c) + " L" : juce::String (c) + " R"; }
        case Label::Signed:  return signedInt (juce::roundToInt (real));
        case Label::ArpRate: { static const char* divs[] = { "1/32", "1/16", "1/8", "1/4" }; return divs[juce::jmin (3, (int) std::floor (clamp01 (v) * 4.0f))]; }
        case Label::None:    break;
    }
    if (std::isinf (real)) return "-inf";
    const auto text = juce::String (real, s.decimals);

    if (s.unit == nullptr)
        return text;

    // I gradi si scrivono attaccati al numero ("180°"), ogni altra unità staccata (cfr. mapping.ts).
    const auto unit = utf8 (s.unit);
    return unit == utf8 ("\u00b0") ? text + unit : text + " " + unit;
}

/** Parametro APVTS da una voce della tabella. I float sono normalizzati 0..1 nell'APVTS. */
inline std::unique_ptr<juce::RangedAudioParameter> makeParameter (const Spec& s)
{
    const juce::ParameterID id { s.id, 1 };
    switch (s.kind)
    {
        case Kind::Bool:
            return std::make_unique<juce::AudioParameterBool> (id, utf8 (s.name), s.def >= 0.5f);
        case Kind::Choice:
        {
            juce::StringArray labels;
            for (int i = 0; i < s.numOptions; ++i) labels.add (utf8 (s.options[i]));
            return std::make_unique<juce::AudioParameterChoice> (id, utf8 (s.name), labels, (int) s.def);
        }
        case Kind::Int:
            return std::make_unique<juce::AudioParameterInt> (id, utf8 (s.name), (int) s.min, (int) s.max, (int) s.def,
                juce::AudioParameterIntAttributes().withLabel (s.unit != nullptr ? utf8 (s.unit) : juce::String()));
        case Kind::Float:
            break;
    }
    return std::make_unique<juce::AudioParameterFloat> (id, utf8 (s.name),
        juce::NormalisableRange<float> { 0.0f, 1.0f }, s.def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([s] (float v, int) { return formatValue (s, v); })
            .withLabel (s.unit != nullptr ? utf8 (s.unit) : juce::String()));
}
} // namespace params
