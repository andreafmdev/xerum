#pragma once

#include "dsp/StateVariableFilter.h"

namespace engine
{
/**
 * I parametri già denormalizzati, riempiti una volta per blocco dal processore.
 * Le voci leggono questa struct: nessun atomico e nessuna mappatura per campione.
 */
struct EngineParams
{
    bool oscOn { true };
    float framePosition { 0.0f };    // 0..1 sul set di frame
    int octave { 0 };
    int semitones { 0 };
    float fineCents { 0.0f };
    float level { 1.0f };            // guadagno lineare

    bool filterOn { true };
    dsp::StateVariableFilter::Type filterType { dsp::StateVariableFilter::Type::lowPass };
    int filterStages { 2 };
    float cutoffHz { 1000.0f };
    float resonanceQ { 0.707f };
    float driveGain { 1.0f };        // guadagno lineare pre-filtro
    float keyTrack { 0.0f };         // 0..1

    float attackSeconds { 0.01f };
    float decaySeconds { 0.1f };
    float sustain { 1.0f };
    float releaseSeconds { 0.1f };
    float velocityAmount { 0.0f };   // 0..1: quanto la velocity scala il picco

    float pan { 0.0f };              // -1..1
    bool bypass { false };
};
} // namespace engine
