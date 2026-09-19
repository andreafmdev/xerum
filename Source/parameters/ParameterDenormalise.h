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

/**
 * Normalizzato 0..1 dal valore reale: la gemella esatta di denormalise(), riga per riga la
 * stessa cosa che fa normalise() in WebUI/src/synth/mapping.ts. Le due implementazioni sono due
 * file in due linguaggi diversi e non c'e' modo di condividerle: se cambia una formula qui,
 * cambia anche la', e viceversa.
 *
 * Non e' difensiva piu' di quanto lo sia la gemella: un `real` fuori dal range della mappa puo'
 * uscire come NaN (log di un negativo, radice di un negativo) esattamente come in TypeScript.
 * Il clamp c'e' solo sul risultato, dove ce l'ha anche lei.
 */
inline float normalise (const Spec& s, float real) noexcept
{
    switch (s.map)
    {
        case Map::Linear:    return clamp01 ((real - s.min) / (s.max - s.min));
        case Map::Log:       return clamp01 (std::log (real / s.min) / std::log (s.max / s.min));
        case Map::Db:        return std::isinf (real) && real < 0.0f ? 0.0f
                                                                    : clamp01 (std::pow (10.0f, (real - s.offset) / 20.0f));
        case Map::MsSquared: return clamp01 (std::sqrt ((real - s.min) / (s.max - s.min)));
        case Map::None:      break;
    }
    return real;
}

/**
 * Il valore naturale di un parametro a partire dal suo grezzo, cioe' da cio' che
 * AudioProcessorValueTreeState::getRawParameterValue restituisce davvero per quel Kind.
 *
 * Esiste perche' "grezzo" non vuol dire "normalizzato": lo vuol dire solo per i Kind::Float, che
 * ParameterMapping.h crea con NormalisableRange {0, 1}. Un Kind::Int e' un AudioParameterInt nel
 * suo range naturale (-3..3, -12..12) e arriva gia' in unita' naturali; un Kind::Choice arriva
 * come indice dell'opzione, un Kind::Bool come 0 o 1. Denormalizzare un Int come se fosse 0..1
 * e' il bug delle quattro ottave: al default (0) dava -3 ottave, e siccome denormalise() clampa
 * l'ingresso a 0..1 il knob poteva produrre *solo* i due estremi del suo range.
 *
 * Una funzione sola, usata sia da collectEngineParams sia dal ciclo di ParameterSeamTests che
 * la confronta con i default dichiarati in parameters.json: finche' resta una sola, un errore
 * qui dentro e' un test rosso, non un semitono sbagliato.
 */
inline float naturalFromRaw (const Spec& s, float raw) noexcept
{
    switch (s.kind)
    {
        // roundToInt e non un cast troncante: il valore arriva come float e gli arrotondamenti
        // possono lasciarlo appena sotto l'intero vero (7.999998), dove un cast troncherebbe
        // verso zero sbagliando di un semitono.
        case Kind::Int:    return (float) juce::roundToInt (raw);
        case Kind::Bool:
        case Kind::Choice: return raw;
        case Kind::Float:  break;
    }
    return denormalise (s, raw);
}

/** L'inversa di naturalFromRaw: il grezzo che l'APVTS deve contenere per quel valore naturale. */
inline float rawFromNatural (const Spec& s, float natural) noexcept
{
    return s.kind == Kind::Float ? normalise (s, natural) : natural;
}

/** Il default di parameters.json letto in unita' naturali, qualunque sia il Kind. */
inline float naturalDefault (const Spec& s) noexcept
{
    return naturalFromRaw (s, s.def);
}
} // namespace params
