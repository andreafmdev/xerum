#pragma once

#include "dsp/StateVariableFilter.h"
#include "engine/Arpeggiator.h"
#include "engine/ModMatrix.h"

#include <array>

namespace engine
{
/**
 * Come il pool di voci reagisce a una nota nuova. E' il choice `voiceMode`, nello stesso ordine
 * delle sue tre opzioni: l'indice grezzo dell'APVTS e' direttamente questo enum.
 *
 * - `poly`: com'e' sempre stato, una voce per nota fino a VoiceManager::maxVoices.
 * - `mono`: una voce sola, e una nota nuova **ritriggera** l'inviluppo.
 * - `legato`: una voce sola, e una nota nuova suonata mentre un'altra e' ancora tenuta **non**
 *   ritriggera l'inviluppo: cambia solo l'intonazione, con il glide se e' attivo.
 *
 * La differenza fra i due modi monofonici e' quindi una sola riga, e riguarda solo il caso in
 * cui un altro tasto sia gia' premuto: con nessun tasto premuto (la nota precedente e' in
 * release) anche il legato ritriggera, perche' non c'e' niente a cui legarsi.
 */
enum class VoiceMode { poly = 0, mono = 1, legato = 2 };

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

    /** Copie dell'oscillatore per voce: 1, 2, 4 o 8 (il choice `unison`). */
    int unisonVoices { 1 };

    /** Ampiezza dello scordamento, in cent: le copie coprono +-detuneCents attorno alla nota. */
    float detuneCents { 0.0f };

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

    /**
     * Il secondo inviluppo, quello che non governa l'ampiezza: alimenta la sorgente
     * engine::ModSource::env2 e nient'altro. Stesse mappe e stessi default dei quattro
     * corrispondenti d'ampiezza, cosi' un patch che non lo tocca si comporta in modo
     * prevedibile; la velocity non lo scala (il suo picco e' sempre 1), perche' `vel` e' gia'
     * una sorgente per conto suo e moltiplicare le due qui renderebbe impossibile separarle.
     */
    float attack2Seconds { 0.01f };
    float decay2Seconds { 0.1f };
    float sustain2 { 1.0f };
    float release2Seconds { 0.1f };

    float pan { 0.0f };              // -1..1
    bool bypass { false };

    // --- glide e modo di voce ---

    /**
     * Quanto dura il glide **per ottava**, in secondi. Zero (il default del parametro `glide`)
     * lo spegne del tutto.
     *
     * "Per ottava" e non "per salto": il tempo vero e' `glideSeconds * |dnota| / 12`, cioe' il
     * *constant rate* di Vital (`kPortamentoScale`) e di Surge (`porta_constrate`). Con un tempo
     * fisso un semitono e due ottave impiegherebbero lo stesso, il che suona sbagliato non
     * appena l'intervallo cambia dentro una frase. La motivazione per esteso sta accanto a
     * SynthVoice::beginGlide(), e l'unita' del parametro lo dice: "ms/oct".
     *
     * Un tempo diverso da zero non basta a far scivolare *ogni* nota: il glide si applica solo
     * fra note legate. Chi decide e' VoiceManager::canGlideFromLastNote(), che e' anche dove sta
     * scritto perche'.
     */
    float glideSeconds { 0.0f };

    /** Poly, Mono o Legato. Il default e' `poly`: e' il valore che rende questa struct, usata
        come punto di partenza dai test scritti a mano, identica a com'era prima del glide. */
    VoiceMode voiceMode { VoiceMode::poly };

    // --- stadio FX ---

    /**
     * Il chorus. Sta in fondo alla catena, fra la fine del rendering delle voci e il gain
     * master: non e' un parametro di voce e nessuna voce lo legge.
     *
     * `chorusOn` e' **falso** qui e **vero** in parameters.json, e la differenza e' voluta.
     * Questa struct e' il valore di partenza che usano i test quando costruiscono un motore a
     * mano: lasciandolo falso, ogni test scritto prima dello stadio FX continua a misurare
     * esattamente la catena di prima, campione per campione. Il default che l'utente vede e'
     * quello del file di parametri, e ci arriva da params::collectEngineParams.
     */
    bool chorusOn { false };
    float chorusRateHz { 1.6f };     // 0.1..5.1 Hz
    float chorusDepth01 { 0.0f };    // 0..1, frazione del ritardo base (vedi dsp::Chorus)
    float chorusMix01 { 0.0f };      // 0..1, proporzione di bagnato con regola sin3dB
    float chorusFeedback01 { 0.0f }; // 0..1, scalato da dsp::Chorus::kMaxFeedback

    /**
     * Il riverbero, in serie **dopo** il chorus dentro lo stesso stadio FX.
     *
     * `reverbOn` e' falso qui e vero in parameters.json per la stessa ragione di `chorusOn`:
     * questa struct e' il punto di partenza dei test scritti a mano, e lasciandolo falso ogni
     * test scritto prima del riverbero continua a misurare la catena di prima, campione per
     * campione. Il default che l'utente vede arriva da params::collectEngineParams.
     *
     * I tre valori normalizzati non sono percentuali travestite: le corse vere (sizeRatio,
     * coefficiente di decadimento, frequenza di damping) vivono in dsp::PlateReverb, che e'
     * anche il solo posto dove si puo' motivarle. Qui arriva la posizione del knob, 0..1.
     */
    bool reverbOn { false };
    float reverbSize01 { 0.6f };           // 0..1 -> dsp::PlateReverb::kMinSizeRatio..kMaxSizeRatio
    float reverbDecay01 { 0.5f };          // 0..1 -> kMinDecay..kMaxDecay
    float reverbDamp01 { 0.4f };           // 0..1 -> kDampingMaxHz..kDampingMinHz, logaritmica
    float reverbPredelaySeconds { 0.02f }; // 0..dsp::PlateReverb::kMaxPredelaySeconds
    float reverbMix01 { 0.0f };            // 0..1, proporzione di bagnato con regola sin3dB

    // --- modulazione ---

    /**
     * Valori normalizzati 0..1 dei sette target modulabili, indicizzati da engine::kModTargets.
     * Sono la base su cui SynthVoice somma depth × livello sorgente **prima** di denormalizzare:
     * la stessa aritmetica di liveValue() in WebUI/src/synth/mod.ts, cosi' l'anello del knob
     * nella UI e il suono raccontano la stessa storia.
     *
     * Convivono con i campi denormalizzati sopra e non li sostituiscono: senza nessuna route
     * attiva il percorso resta quello di prima, campione per campione.
     */
    std::array<float, (size_t) kNumModTargets> modBase {};

    /** Le assegnazioni attive, pubblicate dal message thread. nullptr: nessuna modulazione. */
    const ModSnapshot* mods { nullptr };

    float bpm { 120.0f };            // dal playhead dell'host; 120 quando non c'e'

    /**
     * La posizione della testina dell'host in quarti, e se il transport sta girando.
     *
     * Li legge il solo arpeggiatore, e la differenza fra i due regimi e' tutta qui: con
     * `transportPlaying` falso l'arp gira libero su un contatore suo, con `transportPlaying` vero
     * i suoi confini vengono da `ppqPosition` e si riancorano a ogni blocco. Senza playhead
     * (Standalone, qualche render offline) restano a 0/false, cioe' il regime libero: e' il
     * comportamento giusto, non un ripiego.
     *
     * `bpm` qui sopra serve a tutti e due i regimi — la divisione dell'arp e' sempre a tempo — e
     * resta uno solo per non avere due verita' sul tempo dentro la stessa struct.
     */
    double ppqPosition { 0.0 };
    bool transportPlaying { false };

    /** I sei parametri dell'arp piu' il puntatore alla sequenza pubblicata dal message thread.
        Vedi engine::Arpeggiator: con `on` falso il MidiBuffer non viene toccato. */
    ArpConfig arp {};
    float modWheel { 0.0f };         // CC 1, 0..1

    /** Ampiezza del pitch bend in semitoni a fondo corsa, dal parametro `pbRange`. Non e' una
        quantita' audio-rate: la posizione della rotella e' `pitchBend`, questo e' quanto vale. */
    int pitchBendRangeSemitones { 2 };

    /** Posizione della rotella di pitch, -1..1. Non e' un parametro dell'APVTS: la scrive
        SynthEngine leggendo i messaggi MIDI, come fa con `modWheel`. */
    float pitchBend { 0.0f };
    float globalLfoLevel { 0.0f };   // LFO libero, usato dalle voci quando lfoRetrig e' falso

    int lfoShapeIndex { 0 };
    float lfoRateRaw { 0.0f };       // grezzo: diventa Hz o divisione a seconda di lfoSync
    /** `lfoRateRaw` gia' convertito in Hz (params::lfoRateHzFromRaw), una volta per blocco in
        SynthEngine::process: le voci leggono questo e non rifanno la conversione. */
    float lfoRateHz { 1.0f };
    bool lfoSync { false };
    float lfoPhaseOffset01 { 0.0f };
    float lfoFadeSeconds { 0.0f };
    bool lfoRetrig { true };
};
} // namespace engine
