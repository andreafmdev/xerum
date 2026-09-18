#pragma once

#include "dsp/StateVariableFilter.h"
#include "engine/EngineParams.h"
#include "parameters/ParameterDenormalise.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace params
{
/**
 * Identifica un parametro grezzo per `collectEngineParams` senza passare per il suo nome:
 * l'accessore (`RawAccessor` sotto) prende uno `ParamSlot`, non una stringa, cosi' non c'e'
 * niente da instradare a runtime — chi implementa l'accessore risolve `id -> puntatore` una
 * volta sola, alla costruzione (vedi PluginProcessor::paramSlots_), e qui dentro e' solo un
 * indice di array. Ordine arbitrario, ma stabile: e' un dettaglio interno fra questo header e
 * chi scrive l'accessore, non un ABI pubblico.
 */
enum class ParamSlot : int
{
    oscOn, wtpos, oct, semi, fine, level, filtOn, ftype, slope, cutoff,
    res, drive, keytrk, att, dec, sus, rel, envVel, pan, bypass,
    count
};

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
    constexpr auto* specFine = params::find ("fine");
    constexpr auto* specCutoff = params::find ("cutoff");
    constexpr auto* specRes = params::find ("res");
    constexpr auto* specDrive = params::find ("drive");
    constexpr auto* specKeytrk = params::find ("keytrk");
    constexpr auto* specAtt = params::find ("att");
    constexpr auto* specDec = params::find ("dec");
    constexpr auto* specSus = params::find ("sus");
    constexpr auto* specRel = params::find ("rel");
    constexpr auto* specEnvVel = params::find ("envVel");
    constexpr auto* specPan = params::find ("pan");

    // Se uno di questi manca vuol dire che parameters.json/ParameterTable.h e' cambiato
    // sotto i piedi: meglio un errore di compilazione qui che una dereferenziazione di un
    // puntatore nullo a runtime.
    static_assert (specOct != nullptr && specSemi != nullptr && specFine != nullptr
                       && specCutoff != nullptr && specRes != nullptr && specDrive != nullptr
                       && specKeytrk != nullptr && specAtt != nullptr && specDec != nullptr
                       && specSus != nullptr && specRel != nullptr && specEnvVel != nullptr
                       && specPan != nullptr,
                   "una spec di parametro usata da collectEngineParams non e' in ParameterTable.h");

    p.oscOn = rawFor (ParamSlot::oscOn) >= 0.5f;
    p.framePosition = rawFor (ParamSlot::wtpos); // gia' 0..1 sul set di frame, nessuna denormalizzazione

    // roundToInt, non un cast troncante: denormalise() torna un float che per via degli
    // arrotondamenti in virgola mobile puo' cadere leggermente sotto l'intero vero (es.
    // 7.999998), e un cast tronca verso zero invece di arrotondare, sbagliando la nota di
    // un semitono/ottava (bug corretto nella Task 7).
    p.octave = juce::roundToInt (params::denormalise (*specOct, rawFor (ParamSlot::oct)));
    p.semitones = juce::roundToInt (params::denormalise (*specSemi, rawFor (ParamSlot::semi)));
    p.fineCents = params::denormalise (*specFine, rawFor (ParamSlot::fine));

    // `level` ha mappa Db, ma il valore grezzo e' gia' il guadagno lineare (vedi
    // WebUI/src/synth/mapping.ts): denormalise() qui darebbe un dB, sbagliato.
    p.level = rawFor (ParamSlot::level);

    p.filterOn = rawFor (ParamSlot::filtOn) >= 0.5f;
    p.filterType = filterTypeFromChoice (rawFor (ParamSlot::ftype));
    p.filterStages = rawFor (ParamSlot::slope) >= 0.5f ? 2 : 1;
    p.cutoffHz = params::denormalise (*specCutoff, rawFor (ParamSlot::cutoff));

    // res 0..100 % -> Q 0.707 (Butterworth) ... 20 (autoscillante quasi).
    p.resonanceQ = juce::jmap (params::denormalise (*specRes, rawFor (ParamSlot::res)) * 0.01f, 0.707f, 20.0f);

    // drive 0..24 dB -> guadagno lineare pre-saturazione.
    p.driveGain = juce::Decibels::decibelsToGain (params::denormalise (*specDrive, rawFor (ParamSlot::drive)));
    p.keyTrack = params::denormalise (*specKeytrk, rawFor (ParamSlot::keytrk)) * 0.01f;

    p.attackSeconds = params::denormalise (*specAtt, rawFor (ParamSlot::att)) * 0.001f;   // la mappa e' in ms
    p.decaySeconds = params::denormalise (*specDec, rawFor (ParamSlot::dec)) * 0.001f;
    p.sustain = params::denormalise (*specSus, rawFor (ParamSlot::sus)) * 0.01f;
    p.releaseSeconds = params::denormalise (*specRel, rawFor (ParamSlot::rel)) * 0.001f;
    p.velocityAmount = params::denormalise (*specEnvVel, rawFor (ParamSlot::envVel)) * 0.01f;

    p.pan = params::denormalise (*specPan, rawFor (ParamSlot::pan)) * 0.02f;             // -50..50 -> -1..1
    p.bypass = rawFor (ParamSlot::bypass) >= 0.5f;

    return p;
}
} // namespace params
