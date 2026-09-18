#pragma once

#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"

#include <cstring>

namespace params
{
/**
 * Default normalizzato 0..1 di uno spec quando il preset non lo elenca. Per Kind::Float e
 * Kind::Bool, `spec.def` e' gia' normalizzato (l'APVTS li tiene in 0..1 di natura); per
 * Kind::Int e Kind::Choice, `spec.def` e' il valore grezzo (l'intero, l'indice) con cui
 * ParameterMapping.h costruisce AudioParameterInt/AudioParameterChoice, quindi va mappato
 * nello stesso modo in cui JUCE calcolerebbe RangedAudioParameter::getDefaultValue()
 * (convertTo0to1 sul range del parametro). Stessa regola di defaultNormalised() in
 * WebUI/src/juce/backend.ts (che a sua volta usa fromInt/fromIndex di mapping.ts).
 */
inline float normalisedDefault (const Spec& s) noexcept
{
    switch (s.kind)
    {
        case Kind::Bool:   return s.def >= 0.5f ? 1.0f : 0.0f;
        case Kind::Choice: return s.numOptions > 1 ? s.def / (float) (s.numOptions - 1) : 0.0f;
        case Kind::Int:    return s.max > s.min ? (s.def - s.min) / (s.max - s.min) : 0.0f;
        case Kind::Float:  break;
    }
    return s.def;
}

/**
 * Valore normalizzato 0..1 che un parametro riceve applicando `preset`: quello esplicito se
 * il preset lo elenca (anche se e' 0), altrimenti il default normalizzato dello spec — cosi'
 * un preset non eredita pezzi del suono precedente per i parametri che non menziona.
 *
 * Vive fuori da ParameterMapping.h apposta: non dipende da juce_audio_processors, quindi
 * XerumTests (che non linka quel modulo e non compila i sorgenti di Source/bridge) puo' esercitare
 * davvero la regola che StateChannel::applyPreset applica, invece di una sua copia a mano
 * come succedeva prima. Rispecchiata in WebUI/src/juce/fake-backend.ts::loadPreset: le due
 * implementazioni restano comunque due file diversi in due linguaggi diversi, quindi vanno
 * tenute sincronizzate a mano se questa regola cambia.
 */
inline float presetValue (const Preset& preset, const Spec& s) noexcept
{
    for (int i = 0; i < preset.numValues; ++i)
        if (std::strcmp (preset.values[i].id, s.id) == 0)
            return preset.values[i].value;

    return normalisedDefault (s);
}
} // namespace params
