#pragma once

#include "dsp/Lfo.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/StereoDelay.h"
#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "parameters/ParameterDenormalise.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <iterator>

namespace params
{
namespace detail
{
/** `value` + ".xwt" == `file`, carattere per carattere. */
constexpr bool sameTableFile (const char* value, const char* file) noexcept
{
    while (*value != 0 && *value == *file) { ++value; ++file; }
    if (*value != 0) return false;
    const char* ext = ".xwt";
    while (*ext != 0 && *ext == *file) { ++ext; ++file; }
    return *ext == 0 && *file == 0;
}

/** Il primo indice in cui il file incorporato non e' l'opzione di `wtIndex`, -1 se nessuno. */
constexpr int firstMismatchedTable() noexcept
{
    for (int i = 0; i < (int) std::size (dsp::kWavetableFiles); ++i)
        if (! sameTableFile (kOptionValues_wtIndex[i], dsp::kWavetableFiles[i]))
            return i;

    return -1;
}
} // namespace detail

static_assert (std::size (dsp::kWavetableFiles) == std::size (kOptionValues_wtIndex),
               "dsp::kWavetableFiles e le opzioni di wtIndex in parameters.json devono avere lo stesso numero di voci");
static_assert (detail::firstMismatchedTable() < 0,
               "dsp::kWavetableFiles[i] non e' l'opzione i di wtIndex: un riordino in parameters.json cambierebbe tavola in silenzio");

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

/**
 * `voiceMode` e' un AudioParameterChoice come `ftype` e `unison`: il grezzo e' gia' l'indice, e
 * le tre opzioni sono "Poly", "Mono", "Legato" nell'ordine di engine::VoiceMode.
 *
 * Lo switch, invece di un cast diretto sull'intero, e' la stessa scelta di
 * unisonVoicesFromChoice: un domani che la lista di opzioni cambiasse, qui si vedrebbe subito
 * che va aggiornata anche questa funzione, mentre un cast avrebbe continuato a compilare
 * mappando l'opzione nuova su un modo sbagliato.
 */
inline engine::VoiceMode voiceModeFromChoice (float rawIndex) noexcept
{
    switch ((int) rawIndex)
    {
        case 1:  return engine::VoiceMode::mono;
        case 2:  return engine::VoiceMode::legato;
        default: return engine::VoiceMode::poly;
    }
}

/**
 * `arpMode` e' un AudioParameterChoice come `ftype`, `unison` e `voiceMode`: il grezzo e' gia'
 * l'indice, e le quattro opzioni sono "Up", "Down", "UpDn", "Rand" nell'ordine di engine::ArpMode.
 *
 * Lo switch invece del cast diretto e' la stessa scelta di unisonVoicesFromChoice: se la lista di
 * opzioni cambiasse, qui si vedrebbe subito che va aggiornata anche questa funzione, mentre un
 * cast avrebbe continuato a compilare mappando l'opzione nuova su un modo sbagliato.
 */
inline engine::ArpMode arpModeFromChoice (float rawIndex) noexcept
{
    switch ((int) rawIndex)
    {
        case 1:  return engine::ArpMode::down;
        case 2:  return engine::ArpMode::upDown;
        case 3:  return engine::ArpMode::random;
        default: return engine::ArpMode::up;
    }
}

/** `lrate`: Hz liberi dalla mappa, o la divisione sincronizzata al tempo dell'host. Va chiamata
    una volta per blocco (SynthEngine::process), non una per voce: e' uno std::pow. */
inline float lfoRateHzFromRaw (float raw, bool sync, float bpm) noexcept
{
    constexpr auto* spec = params::find ("lrate");
    static_assert (spec != nullptr, "lrate non e' in ParameterTable.h");
    return sync ? (float) dsp::syncedRateHz (raw, (double) bpm) : params::denormalise (*spec, raw);
}

/** `fxOrder` e' un AudioParameterChoice: il grezzo e' l'indice, nell'ordine di engine::FxOrder. */
inline engine::FxOrder fxOrderFromChoice (float rawIndex) noexcept
{
    switch ((int) rawIndex)
    {
        case 1:  return engine::FxOrder::delayChorusReverb;
        case 2:  return engine::FxOrder::chorusReverbDelay;
        default: return engine::FxOrder::chorusDelayReverb;
    }
}

/**
 * Il tempo del delay in secondi: la mappa log del knob, oppure — con `sync` — la stessa
 * tabella di divisioni dell'LFO (dsp::syncedRateHz, 1/16..2 battute) al tempo dell'host,
 * limitata alla linea (dsp::StereoDelay::kMaxDelaySeconds). Una tabella sola per LFO e delay.
 */
inline float delayTimeSecondsFromRaw (float raw, bool sync, float bpm) noexcept
{
    constexpr auto* spec = params::find ("dlTime");
    static_assert (spec != nullptr, "dlTime non e' in ParameterTable.h");
    const auto seconds = sync ? 1.0f / juce::jmax (1.0e-3f, dsp::syncedRateHz (raw, (double) bpm))
                              : params::denormalise (*spec, raw) * 0.001f;
    return juce::jlimit (0.0f, dsp::StereoDelay::kMaxDelaySeconds, seconds);
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

/** warp 0..100 % -> 0..1: il grezzo e' gia' la quantita' di sync. */
inline float warpFromRaw (float raw) noexcept { return params::clamp01 (raw); }

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

    constexpr auto* specKeytrk = params::find ("keytrk");
    constexpr auto* specAtt = params::find ("att");
    constexpr auto* specDec = params::find ("dec");
    constexpr auto* specSus = params::find ("sus");
    constexpr auto* specRel = params::find ("rel");
    constexpr auto* specEnvVel = params::find ("envVel");
    constexpr auto* specEnvCurve = params::find ("envCurve");
    constexpr auto* specAtt2 = params::find ("att2");
    constexpr auto* specDec2 = params::find ("dec2");
    constexpr auto* specSus2 = params::find ("sus2");
    constexpr auto* specRel2 = params::find ("rel2");

    // Se uno di questi manca vuol dire che parameters.json/ParameterTable.h e' cambiato
    // sotto i piedi: meglio un errore di compilazione qui che una dereferenziazione di un
    // puntatore nullo a runtime. Gli otto target modulabili non compaiono qui: le loro spec
    // stanno dentro le funzioni di conversione sopra, con lo stesso static_assert.
    static_assert (specKeytrk != nullptr && specAtt != nullptr && specDec != nullptr
                       && specSus != nullptr && specRel != nullptr && specEnvVel != nullptr && specEnvCurve != nullptr
                       && specAtt2 != nullptr && specDec2 != nullptr && specSus2 != nullptr
                       && specRel2 != nullptr,
                   "una spec di parametro usata da collectEngineParams non e' in ParameterTable.h");

    p.oscOn = rawFor (ParamSlot::oscOn) >= 0.5f;
    p.framePosition = framePositionFromRaw (rawFor (ParamSlot::wtpos));

    // `oct` e `semi` sono gli unici Kind::Int del progetto, e ParameterMapping.h li crea come
    // juce::AudioParameterInt nel loro range naturale (-3..3 e -12..12). La conversione da
    // grezzo a naturale non e' scritta qui ma in params::naturalFromRaw, che sceglie in base al
    // Kind: e' la stessa funzione che il ciclo di Tests/ParameterSeamTests.cpp confronta con i
    // default dichiarati in parameters.json, parametro per parametro, sull'APVTS vero. Scriverla
    // in linea qui vorrebbe dire riaverne due copie — ed e' su una copia divergente che e'
    // vissuto per mesi il bug delle quattro ottave (vedi il commento di naturalFromRaw).
    constexpr auto& specOct = specForSlot (ParamSlot::oct);
    constexpr auto& specSemi = specForSlot (ParamSlot::semi);
    static_assert (specOct.kind == Kind::Int && specSemi.kind == Kind::Int,
                   "oct e semi devono restare Kind::Int: se diventassero Float la conversione qui sotto cambierebbe");

    p.octave = juce::roundToInt (naturalFromRaw (specOct, rawFor (ParamSlot::oct)));
    p.semitones = juce::roundToInt (naturalFromRaw (specSemi, rawFor (ParamSlot::semi)));

    // Stessa trappola di oct e semi, stessa difesa: Kind::Int vive nel suo range naturale.
    constexpr auto& specPbRange = specForSlot (ParamSlot::pbRange);
    static_assert (specPbRange.kind == Kind::Int,
                   "pbRange deve restare Kind::Int: da Float la conversione qui sotto cambierebbe");
    p.pitchBendRangeSemitones = juce::roundToInt (naturalFromRaw (specPbRange, rawFor (ParamSlot::pbRange)));
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
    p.envCurve = params::denormalise (*specEnvCurve, rawFor (ParamSlot::envCurve)) * 0.01f; // -100..100 -> -1..1
    p.warp = warpFromRaw (rawFor (ParamSlot::warp));

    // Il secondo inviluppo: stesse mappe dei quattro d'ampiezza (ms-squared per i tempi,
    // percentuale per il sustain), quindi stesse conversioni. Non ha un `envVel` proprio — la
    // velocity resta una sorgente del matrix, non un ingresso nascosto di questo inviluppo.
    p.attack2Seconds = params::denormalise (*specAtt2, rawFor (ParamSlot::att2)) * 0.001f;
    p.decay2Seconds = params::denormalise (*specDec2, rawFor (ParamSlot::dec2)) * 0.001f;
    p.sustain2 = params::denormalise (*specSus2, rawFor (ParamSlot::sus2)) * 0.01f;
    p.release2Seconds = params::denormalise (*specRel2, rawFor (ParamSlot::rel2)) * 0.001f;

    p.pan = panFromRaw (rawFor (ParamSlot::pan));
    p.bypass = rawFor (ParamSlot::bypass) >= 0.5f;

    // Glide e modo di voce. Fino a oggi avevano "slot": false e nessuno li leggeva: i due
    // controlli si muovevano nella UI e non succedeva niente — non una funzione mancante, una
    // funzione rotta (punto 14 di docs/research/2026-09-19-confronto-synth-open-source.md).
    //
    // Passare a "slot": true sposta gli indici di params::ParamSlot ma **non** quelli esposti
    // all'host, che sono l'ordine di kTable, cioe' del file: nessuna automazione salvata si
    // sposta. E' la stessa manovra gia' fatta per i parametri del riverbero.
    constexpr auto* specGlide = params::find ("glide");
    static_assert (specGlide != nullptr, "glide non e' in ParameterTable.h");

    p.glideSeconds = params::denormalise (*specGlide, rawFor (ParamSlot::glide)) * 0.001f; // la mappa e' in ms
    p.voiceMode = voiceModeFromChoice (rawFor (ParamSlot::voiceMode));

    // --- stadio FX ------------------------------------------------------------------------
    // Fino a oggi questi quattro avevano "slot": false e il motore non li leggeva: il tab FX
    // disegnava tre knob perfettamente funzionanti attaccati a niente, con l'interruttore
    // acceso di default. Il quinto, chFeedback, e' nuovo e sta in **coda** a parameters.json
    // apposta: l'ordine del file e' l'ordine con cui i parametri vengono esposti all'host,
    // quindi inserirlo in mezzo avrebbe spostato l'indice di tutto cio' che lo segue e rotto
    // le automazioni gia' salvate.
    constexpr auto* specChRate = params::find ("chRate");
    constexpr auto* specChDepth = params::find ("chDepth");
    constexpr auto* specChMix = params::find ("chMix");
    constexpr auto* specChFeedback = params::find ("chFeedback");
    static_assert (specChRate != nullptr && specChDepth != nullptr && specChMix != nullptr
                       && specChFeedback != nullptr,
                   "i parametri del chorus non sono in ParameterTable.h");

    p.chorusOn = rawFor (ParamSlot::fx1On) >= 0.5f;
    p.chorusRateHz = params::denormalise (*specChRate, rawFor (ParamSlot::chRate));
    p.chorusDepth01 = params::denormalise (*specChDepth, rawFor (ParamSlot::chDepth)) * 0.01f;
    p.chorusMix01 = params::denormalise (*specChMix, rawFor (ParamSlot::chMix)) * 0.01f;
    p.chorusFeedback01 = params::denormalise (*specChFeedback, rawFor (ParamSlot::chFeedback)) * 0.01f;

    // Il riverbero. `rvPredelay` e `rvDecay` sono nuovi e stanno anche loro in **coda** al
    // file, per la stessa ragione di chFeedback; i quattro che c'erano gia' passano da
    // "slot": false a true, il che sposta gli indici di params::ParamSlot ma **non** quelli
    // esposti all'host, che sono l'ordine di kTable, cioe' del file.
    //
    // Qui non c'e' nessuna corsa musicale: si dividono delle percentuali per cento. Le corse
    // vere — 0.25..0.75 di sizeRatio, 0.10..0.80 di coefficiente di decadimento, 20 kHz..500 Hz
    // di damping — stanno in dsp::PlateReverb, accanto alla ragione per cui sono quelle.
    constexpr auto* specRvSize = params::find ("rvSize");
    constexpr auto* specRvDamp = params::find ("rvDamp");
    constexpr auto* specRvMix = params::find ("rvMix");
    constexpr auto* specRvPredelay = params::find ("rvPredelay");
    constexpr auto* specRvDecay = params::find ("rvDecay");
    static_assert (specRvSize != nullptr && specRvDamp != nullptr && specRvMix != nullptr
                       && specRvPredelay != nullptr && specRvDecay != nullptr,
                   "i parametri del riverbero non sono in ParameterTable.h");

    p.reverbOn = rawFor (ParamSlot::fx2On) >= 0.5f;
    p.reverbSize01 = params::denormalise (*specRvSize, rawFor (ParamSlot::rvSize)) * 0.01f;
    p.reverbDamp01 = params::denormalise (*specRvDamp, rawFor (ParamSlot::rvDamp)) * 0.01f;
    p.reverbMix01 = params::denormalise (*specRvMix, rawFor (ParamSlot::rvMix)) * 0.01f;
    p.reverbDecay01 = params::denormalise (*specRvDecay, rawFor (ParamSlot::rvDecay)) * 0.01f;
    p.reverbPredelaySeconds = params::denormalise (*specRvPredelay, rawFor (ParamSlot::rvPredelay)) * 0.001f;

    // Il delay, terzo effetto: percentuali divise per cento come per gli altri; il tempo in
    // secondi lo calcola SynthEngine::process, che conosce il bpm (delayTimeSecondsFromRaw).
    constexpr auto* specDlFeedback = params::find ("dlFeedback");
    constexpr auto* specDlDamp = params::find ("dlDamp");
    constexpr auto* specDlMix = params::find ("dlMix");
    static_assert (specDlFeedback != nullptr && specDlDamp != nullptr && specDlMix != nullptr,
                   "i parametri del delay non sono in ParameterTable.h");

    p.delayOn = rawFor (ParamSlot::fx3On) >= 0.5f;
    p.delayTimeRaw = params::clamp01 (rawFor (ParamSlot::dlTime));
    p.delaySync = rawFor (ParamSlot::dlSync) >= 0.5f;
    p.delayFeedback01 = params::denormalise (*specDlFeedback, rawFor (ParamSlot::dlFeedback)) * 0.01f;
    p.delayDamp01 = params::denormalise (*specDlDamp, rawFor (ParamSlot::dlDamp)) * 0.01f;
    p.delayMix01 = params::denormalise (*specDlMix, rawFor (ParamSlot::dlMix)) * 0.01f;
    p.delayPingPong = rawFor (ParamSlot::dlPingPong) >= 0.5f;
    p.fxOrder = fxOrderFromChoice (rawFor (ParamSlot::fxOrder));

    // --- arpeggiatore ---------------------------------------------------------------------
    // Fino a oggi questi sei avevano "slot": false e il motore non li leggeva: il tab Arp
    // disegnava un interruttore, quattro knob, un selettore di modo e sedici step perfettamente
    // funzionanti attaccati a niente. Come per il glide e per lo stadio FX, passare a
    // "slot": true sposta gli indici di params::ParamSlot ma **non** quelli esposti all'host,
    // che sono l'ordine di kTable, cioe' del file: nessuna automazione salvata si sposta.
    //
    // La sequenza dei sedici step non passa di qui: vive nel nodo ARP del ValueTree, viene
    // tradotta in un engine::ArpSnapshot sul message thread e arriva al motore come puntatore
    // (engine::SynthEngine::setArpSteps), esattamente come le assegnazioni del mod matrix.
    constexpr auto* specArpGate = params::find ("arpGate");
    constexpr auto* specArpOct = params::find ("arpOct");
    constexpr auto* specArpSwing = params::find ("arpSwing");
    static_assert (specArpGate != nullptr && specArpOct != nullptr && specArpSwing != nullptr,
                   "i parametri dell'arpeggiatore non sono in ParameterTable.h");

    p.arp.on = rawFor (ParamSlot::arpOn) >= 0.5f;
    p.arp.mode = arpModeFromChoice (rawFor (ParamSlot::arpMode));

    // Grezzo, come `lrate`: la divisione non e' una denormalizzazione ma la scelta di una voce in
    // una tabella di quattro, e quella tabella vive in engine::arpBeatsPerStep accanto alla
    // ragione per cui non e' quella dell'LFO.
    p.arp.rateRaw = rawFor (ParamSlot::arpRate);

    p.arp.gate01 = params::denormalise (*specArpGate, rawFor (ParamSlot::arpGate)) * 0.01f;
    p.arp.octaves = juce::roundToInt (params::denormalise (*specArpOct, rawFor (ParamSlot::arpOct)));
    p.arp.swing01 = params::denormalise (*specArpSwing, rawFor (ParamSlot::arpSwing)) * 0.01f;

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
    p.modBase[(size_t) engine::modTargetIndexFor (ParamSlot::warp)] = rawFor (ParamSlot::warp);

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
