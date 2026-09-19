#pragma once

#include "dsp/StateVariableFilter.h"
#include "engine/EngineParams.h"
#include "parameters/ParamSlot.h"
#include "parameters/ParameterDenormalise.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>

namespace params
{
/** `ftype` è un AudioParameterChoice: il valore grezzo è già l'indice 0/1/2. */
inline dsp::StateVariableFilter::Type filterTypeFromChoice (float rawIndex) noexcept
{
    switch ((int) rawIndex)
    {
        case 1:  return dsp::StateVariableFilter::Type::highPass;
        case 2:  return dsp::StateVariableFilter::Type::bandPass;
        default: return dsp::StateVariableFilter::Type::lowPass;
    }
}

/**
 * `unison` e' un AudioParameterChoice come `ftype`: il valore grezzo e' gia' l'indice, e le
 * quattro opzioni sono le etichette "1", "2", "4", "8". Lo switch traduce l'indice nel numero
 * di copie invece di fidarsi di una formula (1 << index): se un domani la lista di opzioni
 * cambiasse, qui si vedrebbe subito che va aggiornata anche questa funzione.
 */
inline int unisonVoicesFromChoice (float rawIndex) noexcept
{
    switch ((int) rawIndex)
    {
        case 1:  return 2;
        case 2:  return 4;
        case 3:  return 8;
        default: return 1;
    }
}

/** detune 0..100 -> cent. Non e' un target modulabile: nessuna base in modBase. */
inline float detuneCentsFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("detune");
    static_assert (spec != nullptr, "detune non e' in ParameterTable.h");
    return params::denormalise (*spec, raw);
}

/**
 * Da valore normalizzato 0..1 a valore reale, un target per funzione.
 *
 * Esistono come funzioni e non in linea dentro collectEngineParams perche' il percorso
 * modulato (SynthVoice, quando il mod matrix ha una route su quel parametro) deve applicare
 * esattamente la stessa aritmetica: due copie della stessa formula divergono alla prima
 * modifica, e il sintomo sarebbe un cutoff che finisce altrove a seconda che lo muova un LFO
 * o la mano dell'utente.
 */
inline float cutoffHzFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("cutoff");
    static_assert (spec != nullptr, "cutoff non e' in ParameterTable.h");
    return params::denormalise (*spec, raw);
}

inline float resonanceQFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("res");
    static_assert (spec != nullptr, "res non e' in ParameterTable.h");

    // res 0..100 % -> Q, esponenziale da Butterworth (0.707) a 12. Esponenziale e non lineare
    // perche' Q e' percepito in rapporti: con una mappa lineare la meta' bassa della corsa era
    // gia' tutta risonante (a res 30 % il vecchio jmap dava Q 6.5, +16 dB di picco) e la meta'
    // alta non cambiava quasi nulla. Il tetto scende da 20 a 12: sopra, il filtro e' di fatto
    // un oscillatore e il picco non e' piu' governabile. std::pow gira una volta per blocco.
    return dsp::StateVariableFilter::kButterworthQ
               * std::pow (12.0f / dsp::StateVariableFilter::kButterworthQ,
                           params::denormalise (*spec, raw) * 0.01f);
}

/** wtpos e' gia' 0..1 sul set di frame: nessuna denormalizzazione. */
inline float framePositionFromRaw (float raw) noexcept { return params::clamp01 (raw); }

/** `level` ha mappa Db ma il valore grezzo e' gia' il guadagno lineare (vedi
    WebUI/src/synth/mapping.ts): denormalise() qui darebbe un dB, sbagliato. */
inline float levelGainFromRaw (float raw) noexcept { return params::clamp01 (raw); }

inline float panFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("pan");
    static_assert (spec != nullptr, "pan non e' in ParameterTable.h");
    return params::denormalise (*spec, raw) * 0.02f; // -50..50 -> -1..1
}

inline float fineCentsFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("fine");
    static_assert (spec != nullptr, "fine non e' in ParameterTable.h");
    return params::denormalise (*spec, raw);
}

/** drive 0..24 dB -> guadagno lineare pre-saturazione. */
inline float driveGainFromRaw (float raw) noexcept
{
    constexpr auto* spec = params::find ("drive");
    static_assert (spec != nullptr, "drive non e' in ParameterTable.h");
    return juce::Decibels::decibelsToGain (params::denormalise (*spec, raw));
}

/**
 * Costruisce una engine::EngineParams leggendo i parametri grezzi (normalizzati 0..1)
 * tramite `rawFor(slot)`. E' la stessa aritmetica di PluginProcessor::collectParams, ma
 * isolata da juce_audio_processors: cosi' la si esercita con un accessor finto nei test,
 * senza dover linkare l'APVTS. Gira una volta per blocco sul thread audio.
 *
 * `rawFor` prende uno `ParamSlot` (un indice), non il nome del parametro: non c'e' hashing
 * ne' confronto di stringhe a runtime da nessuna parte in questa funzione ne' in chi la
 * chiama, solo una lettura d'array. Gli spec (`params::find` su un letterale, con kTable
 * constexpr) restano invece risolti sul nome, ma a tempo di compilazione: sono variabili
 * locali `constexpr`, quindi la loro inizializzazione non costa nulla a runtime — non e' una
 * ricerca nella tabella "una volta per blocco", e' un indirizzo gia' fisso nel binario.
 * Nessuna allocazione, nessun lock, nessuna I/O.
 */
template <typename RawAccessor>
engine::EngineParams collectEngineParams (RawAccessor&& rawFor) noexcept
{
    engine::EngineParams p;

    constexpr auto* specOct = params::find ("oct");
    constexpr auto* specSemi = params::find ("semi");
    constexpr auto* specKeytrk = params::find ("keytrk");
    constexpr auto* specAtt = params::find ("att");
    constexpr auto* specDec = params::find ("dec");
    constexpr auto* specSus = params::find ("sus");
    constexpr auto* specRel = params::find ("rel");
    constexpr auto* specEnvVel = params::find ("envVel");

    // Se uno di questi manca vuol dire che parameters.json/ParameterTable.h e' cambiato
    // sotto i piedi: meglio un errore di compilazione qui che una dereferenziazione di un
    // puntatore nullo a runtime. I sette target modulabili non compaiono qui: le loro spec
    // stanno dentro le funzioni di conversione sopra, con lo stesso static_assert.
    static_assert (specOct != nullptr && specSemi != nullptr && specKeytrk != nullptr
                       && specAtt != nullptr && specDec != nullptr && specSus != nullptr
                       && specRel != nullptr && specEnvVel != nullptr,
                   "una spec di parametro usata da collectEngineParams non e' in ParameterTable.h");

    p.oscOn = rawFor (ParamSlot::oscOn) >= 0.5f;
    p.framePosition = framePositionFromRaw (rawFor (ParamSlot::wtpos));

    // roundToInt, non un cast troncante: denormalise() torna un float che per via degli
    // arrotondamenti in virgola mobile puo' cadere leggermente sotto l'intero vero (es.
    // 7.999998), e un cast tronca verso zero invece di arrotondare, sbagliando la nota di
    // un semitono/ottava (bug corretto nella Task 7).
    p.octave = juce::roundToInt (params::denormalise (*specOct, rawFor (ParamSlot::oct)));
    p.semitones = juce::roundToInt (params::denormalise (*specSemi, rawFor (ParamSlot::semi)));
    p.fineCents = fineCentsFromRaw (rawFor (ParamSlot::fine));
    p.level = levelGainFromRaw (rawFor (ParamSlot::level));
    p.unisonVoices = unisonVoicesFromChoice (rawFor (ParamSlot::unison));
    p.detuneCents = detuneCentsFromRaw (rawFor (ParamSlot::detune));

    p.filterOn = rawFor (ParamSlot::filtOn) >= 0.5f;
    p.filterType = filterTypeFromChoice (rawFor (ParamSlot::ftype));
    p.filterStages = rawFor (ParamSlot::slope) >= 0.5f ? 2 : 1;
    p.cutoffHz = cutoffHzFromRaw (rawFor (ParamSlot::cutoff));
    p.resonanceQ = resonanceQFromRaw (rawFor (ParamSlot::res));
    p.driveGain = driveGainFromRaw (rawFor (ParamSlot::drive));
    p.keyTrack = params::denormalise (*specKeytrk, rawFor (ParamSlot::keytrk)) * 0.01f;

    p.attackSeconds = params::denormalise (*specAtt, rawFor (ParamSlot::att)) * 0.001f;   // la mappa e' in ms
    p.decaySeconds = params::denormalise (*specDec, rawFor (ParamSlot::dec)) * 0.001f;
    p.sustain = params::denormalise (*specSus, rawFor (ParamSlot::sus)) * 0.01f;
    p.releaseSeconds = params::denormalise (*specRel, rawFor (ParamSlot::rel)) * 0.001f;
    p.velocityAmount = params::denormalise (*specEnvVel, rawFor (ParamSlot::envVel)) * 0.01f;

    p.pan = panFromRaw (rawFor (ParamSlot::pan));
    p.bypass = rawFor (ParamSlot::bypass) >= 0.5f;

    // Le basi normalizzate dei target modulabili: nessuna conversione, e' il valore grezzo
    // dell'APVTS. La denormalizzazione avviene dopo la somma delle modulazioni, dentro
    // SynthVoice, con le stesse funzioni usate qui sopra.
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::cutoff)] = rawFor (ParamSlot::cutoff);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::res)] = rawFor (ParamSlot::res);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::wtpos)] = rawFor (ParamSlot::wtpos);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::level)] = rawFor (ParamSlot::level);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::pan)] = rawFor (ParamSlot::pan);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::fine)] = rawFor (ParamSlot::fine);
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::drive)] = rawFor (ParamSlot::drive);

    constexpr auto* specLphase = params::find ("lphase");
    constexpr auto* specLfade = params::find ("lfade");
    static_assert (specLphase != nullptr && specLfade != nullptr,
                   "lphase/lfade non sono in ParameterTable.h");

    p.lfoShapeIndex = (int) rawFor (ParamSlot::lshape); // choice: il grezzo e' gia' l'indice
    p.lfoRateRaw = rawFor (ParamSlot::lrate);
    p.lfoSync = rawFor (ParamSlot::lsync) >= 0.5f;
    p.lfoPhaseOffset01 = params::denormalise (*specLphase, rawFor (ParamSlot::lphase)) / 360.0f;
    p.lfoFadeSeconds = params::denormalise (*specLfade, rawFor (ParamSlot::lfade)) * 0.001f;
    p.lfoRetrig = rawFor (ParamSlot::lretrig) >= 0.5f;

    return p;
}
} // namespace params
