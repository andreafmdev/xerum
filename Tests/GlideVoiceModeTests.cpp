#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"
#include "parameters/ParamCollect.h"
#include "parameters/ParameterTable.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace
{
constexpr double kSampleRate = 48000.0;

/** La frequenza di un numero di nota, ricalcolata qui invece che chiesta al motore: se il test
    usasse la stessa funzione che verifica non proverebbe niente. */
double noteHz (double note)
{
    return 440.0 * std::pow (2.0, (note - 69.0) / 12.0);
}

/**
 * Un patch che accende tutto cio' che il glide e i modi di voce potrebbero disturbare: unison
 * con detune, keytracking del filtro, drive, una route del matrix su `fine` (l'unico target che
 * si somma a tuningSemitones_, cioe' l'unico che condivide l'aritmetica con il glide) e un
 * inviluppo con sustain intermedio, cosi' che il livello di una nota in release sia misurabile.
 */
engine::EngineParams glidePatch()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.framePosition = 0.25f;
    p.octave = 0;
    p.semitones = 0;
    p.fineCents = 0.0f;
    p.level = 0.9f;
    p.unisonVoices = 4;
    p.detuneCents = 12.0f;
    p.filterOn = true;
    p.filterType = dsp::StateVariableFilter::Type::lowPass;
    p.filterStages = 2;
    p.cutoffHz = 3000.0f;
    p.resonanceQ = 2.0f;
    p.driveGain = 2.0f;
    p.keyTrack = 0.5f;
    p.attackSeconds = 0.005f;
    p.decaySeconds = 0.08f;
    p.sustain = 0.6f;
    p.releaseSeconds = 0.2f;
    p.velocityAmount = 0.5f;
    p.attack2Seconds = 0.01f;
    p.decay2Seconds = 0.1f;
    p.sustain2 = 0.8f;
    p.release2Seconds = 0.1f;
    p.pan = 0.2f;
    p.bypass = false;
    return p;
}

/** Il patch piu' semplice che sappia suonare: una copia sola, niente filtro, sustain pieno.
    Tutto cio' che questi test misurano e' l'intonazione e il livello dell'inviluppo, e ogni
    stadio in piu' e' solo un modo di confondere la misura. */
engine::EngineParams plainPatch()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.framePosition = 0.0f;
    p.level = 1.0f;
    p.unisonVoices = 1;
    p.detuneCents = 0.0f;
    p.filterOn = false;
    p.cutoffHz = 20000.0f;
    p.resonanceQ = 0.707f;
    p.driveGain = 1.0f;
    p.keyTrack = 0.0f;
    p.attackSeconds = 0.001f;
    p.decaySeconds = 0.001f;
    p.sustain = 1.0f;
    p.releaseSeconds = 0.2f;
    p.velocityAmount = 0.0f;
    p.pan = 0.0f;
    return p;
}

/** FNV-1a sui bit dei campioni: due render identici danno lo stesso numero, uno diverso di un
    solo ulp su un solo campione no. */
struct Checksum
{
    std::uint64_t value { 1469598103934665603ull };

    void add (float sample) noexcept
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &sample, sizeof (bits));

        for (int byte = 0; byte < 4; ++byte)
        {
            value ^= (std::uint64_t) ((bits >> (byte * 8)) & 0xffu);
            value *= 1099511628211ull;
        }
    }
};

/**
 * Il render di riferimento: una sequenza di note sovrapposte, con eventi MIDI a offset che
 * *non* cadono sui confini delle sotto-fette di controllo, cosi' che il percorso spezzato di
 * renderControlSlices sia esercitato per davvero.
 */
std::uint64_t referenceChecksum (const engine::EngineParams& params, dsp::WavetableStore& store)
{
    engine::SynthEngine synth;
    engine::EngineSpec spec;
    spec.sampleRate = kSampleRate;
    spec.maximumBlockSize = 128;
    spec.numChannels = 2;
    synth.prepare (spec);
    synth.setWavetable (store.active());
    synth.setParams (params);
    synth.setMasterGainLinear (0.8f);

    struct Event { int block; int offset; bool on; int note; float velocity; };

    static constexpr Event events[] = {
        { 0, 17, true, 48, 0.9f },
        { 2, 5, true, 55, 0.7f },
        { 4, 71, true, 60, 1.0f },
        { 7, 33, false, 48, 0.0f },
        { 9, 3, true, 72, 0.5f },
        { 11, 90, false, 60, 0.0f },
        { 13, 45, false, 55, 0.0f },
        { 16, 12, true, 36, 0.8f },
        { 19, 64, false, 72, 0.0f },
        { 24, 7, false, 36, 0.0f },
    };

    Checksum checksum;
    juce::AudioBuffer<float> buffer (2, 128);

    for (int block = 0; block < 40; ++block)
    {
        juce::MidiBuffer midi;

        for (const auto& e : events)
            if (e.block == block)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.velocity)
                                    : juce::MidiMessage::noteOff (1, e.note),
                               e.offset);

        buffer.clear();
        synth.process (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                checksum.add (buffer.getSample (ch, i));
    }

    return checksum.value;
}

/** Un pool di voci pronto a suonare. */
struct Pool
{
    engine::VoiceManager voices;
    juce::AudioBuffer<float> scratch { 2, engine::SynthEngine::kControlBlockSamples };

    Pool (const engine::EngineParams& p, dsp::WavetableStore& store)
    {
        voices.prepare (kSampleRate);
        voices.setWavetable (store.active());
        voices.setParams (p);
    }

    /**
     * Rende `numSamples` campioni **in sotto-fette da kControlBlockSamples**, come fa
     * SynthEngine::renderControlSlices.
     *
     * Non e' un dettaglio del test: il glide avanza una volta per sotto-fetta, quindi renderne
     * 4800 in un colpo solo o in centocinquanta fette e' la differenza fra misurare il tasso di
     * controllo vero e misurarne uno inventato qui dentro.
     */
    void render (int numSamples)
    {
        for (int done = 0; done < numSamples; )
        {
            const auto slice = std::min (engine::SynthEngine::kControlBlockSamples, numSamples - done);
            scratch.clear();
            voices.render (scratch.getWritePointer (0), scratch.getWritePointer (1), slice);
            done += slice;
        }
    }

    /** La voce che sta suonando una certa nota, o nullptr. */
    const engine::SynthVoice* voiceFor (int midiNote) const
    {
        for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
        {
            const auto& voice = voices.getVoice (i);

            if (voice.isActive() && ! voice.isFading() && voice.getMidiNote() == midiNote)
                return &voice;
        }

        return nullptr;
    }

    /** La prima voce che occupa un posto nella polifonia, o nullptr. */
    const engine::SynthVoice* sounding() const
    {
        for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
            if (voices.getVoice (i).isActive() && ! voices.getVoice (i).isFading())
                return &voices.getVoice (i);

        return nullptr;
    }

    int countFading() const
    {
        int count = 0;

        for (int i = 0; i < engine::VoiceManager::poolSize; ++i)
            if (voices.getVoice (i).isFading())
                ++count;

        return count;
    }
};

/** Accessor finto per collectEngineParams: slot -> valore grezzo. */
struct FakeRaw
{
    std::map<params::ParamSlot, float> values;

    float operator() (params::ParamSlot slot) const
    {
        const auto it = values.find (slot);
        return it != values.end() ? it->second : 0.0f;
    }
};
} // namespace

/**
 * Punto 14 della ricerca (docs/research/2026-09-19-confronto-synth-open-source.md): glide, mono
 * e legato. I due controlli esistevano nella UI e non erano cablati — non una funzione mancante,
 * una funzione rotta.
 */
struct GlideVoiceModeTests final : juce::UnitTest
{
    GlideVoiceModeTests() : juce::UnitTest ("glide e modi di voce", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("i due parametri arrivano al motore");
        {
            // `glide` e `voiceMode` avevano "slot": false: collectEngineParams non li leggeva
            // nemmeno. Questo e' il test che fallisce per primo se qualcuno li rimette a false.
            FakeRaw raw;
            const auto* specGlide = params::find ("glide");

            // Map::MsSquared: il grezzo e' la radice della frazione della corsa. 0.5 -> 500 ms
            // su 0..2000, che e' anche il caso dichiarato in WebUI/src/synth/mapping.test.ts.
            raw.values[params::ParamSlot::glide] = 0.5f;
            raw.values[params::ParamSlot::voiceMode] = 1.0f;

            const auto p = params::collectEngineParams (raw);

            expect (specGlide != nullptr);
            expectWithinAbsoluteError (p.glideSeconds, 0.5f, 1.0e-6f);
            expect (p.voiceMode == engine::VoiceMode::mono);

            raw.values[params::ParamSlot::voiceMode] = 2.0f;
            expect (params::collectEngineParams (raw).voiceMode == engine::VoiceMode::legato);

            raw.values[params::ParamSlot::voiceMode] = 0.0f;
            raw.values[params::ParamSlot::glide] = 0.0f;

            const auto off = params::collectEngineParams (raw);
            expect (off.voiceMode == engine::VoiceMode::poly, "il default deve restare Poly");
            expectWithinAbsoluteError (off.glideSeconds, 0.0f, 0.0f);
        }

        beginTest ("il glide interpola il numero di nota, non gli hertz");
        {
            // Il criterio che separa la nostra implementazione da quella di Odin 2 (un polo
            // singolo sulla frequenza in hertz), ed e' misurabile in un numero solo: a meta'
            // strada fra DO3 e DO4 si deve sentire il FA#, cioe' il rapporto radice di due, non
            // la media aritmetica delle due frequenze — che sarebbe un SOL abbondante, 92 cent
            // piu' su, un errore che qualunque orecchio sente come una nota diversa.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.glideSeconds = 0.1f; // per ottava, e il salto e' di un'ottava esatta: 100 ms

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render (256);

            const auto* voice = pool.sounding();
            expect (voice != nullptr);
            expectWithinAbsoluteError ((double) voice->getFrequencyHz(), noteHz (60), 0.01);

            pool.voices.noteOn (72, 1.0f);
            expect (voice->isGliding(), "il glide deve essere partito");

            // Meta' del tempo: 2400 campioni a 48 kHz, cioe' 75 sotto-fette esatte.
            pool.render (2400);

            const auto midpoint = (double) voice->getFrequencyHz();
            const auto geometric = noteHz (66);                       // FA#, radice di due
            const auto arithmetic = 0.5 * (noteHz (60) + noteHz (72)); // cio' che farebbe Odin

            logMessage ("a meta' glide: " + juce::String (midpoint, 3) + " Hz (FA# = "
                        + juce::String (geometric, 3) + ", media aritmetica = "
                        + juce::String (arithmetic, 3) + ")");

            expectWithinAbsoluteError ((double) voice->getGlideNote(), 66.0, 1.0e-4);
            expectWithinAbsoluteError (midpoint, geometric, 0.05);
            expectWithinAbsoluteError (midpoint / noteHz (60), std::sqrt (2.0), 1.0e-4);

            // E il contrappeso: le due ipotesi sono davvero distinguibili da questa misura.
            expect (std::abs (midpoint - arithmetic) > 20.0,
                    "la media aritmetica dista " + juce::String (std::abs (midpoint - arithmetic), 3)
                        + " Hz: il test non distinguerebbe le due implementazioni");

            // E all'arrivo si arriva davvero, non a un ulp di distanza.
            pool.render (2400 + engine::SynthEngine::kControlBlockSamples);
            expect (! voice->isGliding(), "il glide deve essere finito");
            expectWithinAbsoluteError ((double) voice->getGlideNote(), 72.0, 0.0);
            expectWithinAbsoluteError ((double) voice->getFrequencyHz(), noteHz (72), 0.01);
        }

        beginTest ("constant rate: due ottave impiegano il doppio di una");
        {
            // `T' = T * |dnota| / 12`. Senza, un salto di due ottave e uno di un semitono
            // durerebbero uguale: il semitono striscerebbe e l'ottava sembrerebbe istantanea.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.glideSeconds = 0.2f;

            /** Quanti campioni servono perche' il glide da `from` a `to` arrivi. */
            auto glideSamples = [&] (int from, int to)
            {
                Pool pool (p, store);
                pool.voices.noteOn (from, 1.0f);
                pool.render (engine::SynthEngine::kControlBlockSamples);

                const auto* voice = pool.sounding();
                pool.voices.noteOn (to, 1.0f);

                int samples = 0;

                while (voice->isGliding() && samples < (int) (kSampleRate * 4.0))
                {
                    pool.render (engine::SynthEngine::kControlBlockSamples);
                    samples += engine::SynthEngine::kControlBlockSamples;
                }

                return samples;
            };

            const auto oneOctave = glideSamples (48, 60);
            const auto twoOctaves = glideSamples (48, 72);
            const auto semitone = glideSamples (48, 49);

            logMessage ("glide 200 ms/ottava: un semitono " + juce::String (semitone)
                        + " campioni, un'ottava " + juce::String (oneOctave) + ", due ottave "
                        + juce::String (twoOctaves));

            // Una sotto-fetta di tolleranza: il glide finisce *dentro* una fetta e la misura si
            // accorge solo al confine, quindi ogni durata e' arrotondata per eccesso al
            // multiplo di 32 campioni successivo. E' la risoluzione dello strumento, non un
            // margine concesso al risultato: 32 campioni su 9600 sono lo 0.3 %.
            const auto slice = engine::SynthEngine::kControlBlockSamples;
            expectWithinAbsoluteError ((float) oneOctave, (float) (kSampleRate * 0.2), (float) slice);
            expectWithinAbsoluteError ((float) twoOctaves, (float) (kSampleRate * 0.4), (float) slice);
            expectWithinAbsoluteError ((float) semitone, (float) (kSampleRate * 0.2 / 12.0), (float) slice);
            expect (std::abs (twoOctaves - 2 * oneOctave) <= slice,
                    "due ottave sono costate " + juce::String (twoOctaves) + " campioni contro "
                        + juce::String (2 * oneOctave) + ", cioe' il doppio di una");

            // La stessa affermazione senza la quantizzazione di mezzo, e piu' stretta: dopo i
            // 200 ms che il knob promette per un'ottava, un glide di due ottave deve trovarsi
            // **esattamente a meta'** — cioe' sulla nota di mezzo, il DO centrale. A tempo fisso
            // sarebbe gia' arrivato al DO di sopra.
            {
                Pool pool (p, store);
                pool.voices.noteOn (48, 1.0f);
                pool.render (slice);
                pool.voices.noteOn (72, 1.0f);
                pool.render ((int) (kSampleRate * 0.2));

                expectWithinAbsoluteError ((double) pool.sounding()->getGlideNote(), 60.0, 1.0e-3);
            }
        }

        beginTest ("Poly: due note, due voci");
        {
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::poly;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.voices.noteOn (64, 1.0f);
            pool.render (512);

            expectEquals (pool.voices.countActive(), 2, "in Poly ogni nota ha la sua voce");
            expectEquals (pool.countFading(), 0, "nessun furto con due note su sedici");
        }

        beginTest ("in Poly un accordo non scivola nota per nota");
        {
            // Il glide si applica solo fra note **legate** (canGlideFromLastNote, cioe' il
            // kPortamentoForce di Vital con force spento). Le tre note di un accordo arrivano
            // come note-on consecutivi senza nessun render in mezzo: nessuna delle tre ha
            // sentito la precedente, quindi nessuna delle tre scivola. Senza questa regola
            // l'accordo entrerebbe smerdato — con `glide` a un secondo per ottava, la terza nota
            // partirebbe quattro semitoni sotto e ci metterebbe un terzo di secondo ad arrivare.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::poly;
            p.glideSeconds = 1.0f;

            const int chord[] = { 60, 64, 67 };

            Pool pool (p, store);

            for (auto note : chord)
                pool.voices.noteOn (note, 1.0f);

            pool.render (128);

            expectEquals (pool.voices.countActive(), 3);

            for (auto note : chord)
            {
                const auto* voice = pool.voiceFor (note);
                expect (voice != nullptr, "manca la voce della nota " + juce::String (note));
                expect (! voice->isGliding(), "la nota " + juce::String (note) + " sta scivolando");
                expectWithinAbsoluteError ((double) voice->getFrequencyHz(), noteHz (note), 0.01,
                                           "la nota " + juce::String (note) + " non parte alla sua intonazione");
            }
        }

        beginTest ("in Poly due note legate scivolano invece si'");
        {
            // Il contrappeso del test sopra: la stessa coppia di note, lo stesso patch, e
            // l'unica differenza e' che fra le due passa del tempo con il primo tasto ancora
            // premuto. Senza questo, "l'accordo non scivola" sarebbe soddisfatto anche da un
            // glide poly che non funziona affatto.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::poly;
            p.glideSeconds = 1.0f;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render (1024); // il DO suona, e il tasto resta premuto
            pool.voices.noteOn (64, 1.0f);

            const auto* second = pool.voiceFor (64);
            expect (second != nullptr);
            expect (second->isGliding(), "la seconda nota di una frase legata deve scivolare");
            expectWithinAbsoluteError (second->getGlideNote(), 60.0f, 0.0f,
                                       "e deve partire dalla nota ancora premuta");

            // La prima resta dov'e': il glide della seconda non la tocca.
            expect (! pool.voiceFor (60)->isGliding());
        }

        beginTest ("una nota dopo il silenzio non scivola da quella di prima");
        {
            // L'altro caso che la regola toglie di mezzo: una frase nuova, dieci secondi dopo,
            // non deve arrivarci strisciando dall'ultima nota della frase precedente.
            for (auto mode : { engine::VoiceMode::poly, engine::VoiceMode::mono })
            {
                auto p = plainPatch();
                p.voiceMode = mode;
                p.glideSeconds = 1.0f;
                p.releaseSeconds = 0.01f;

                Pool pool (p, store);

                pool.voices.noteOn (48, 1.0f);
                pool.render ((int) (kSampleRate * 0.1));
                pool.voices.noteOff (48);
                pool.render ((int) (kSampleRate * 0.5)); // la voce si spegne del tutto

                expectEquals (pool.voices.countSounding(), 0);
                expectEquals (pool.voices.countHeld(), 0);

                pool.voices.noteOn (72, 1.0f);

                const auto* voice = pool.sounding();
                expect (voice != nullptr);
                expect (! voice->isGliding(), "nessun tasto era premuto: non c'e' niente da cui scivolare");
                expectWithinAbsoluteError ((double) voice->getFrequencyHz(), noteHz (72), 0.01);
            }
        }

        beginTest ("in Mono due note staccate saltano, due sovrapposte scivolano");
        {
            // Lo *slide* del 303, e il caso in cui la regola convive con il riuso della voce in
            // release: la voce viene ripresa lo stesso — inviluppo, fase e filtro non azzerati —
            // ma l'intonazione salta, perche' fra le due note il tasto era stato lasciato.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.glideSeconds = 1.0f;
            p.releaseSeconds = 0.5f; // lunga: la voce e' ancora viva quando arriva la seconda nota

            {
                Pool pool (p, store);

                pool.voices.noteOn (48, 1.0f);
                pool.render ((int) (kSampleRate * 0.1));
                pool.voices.noteOff (48);
                pool.render ((int) (kSampleRate * 0.05)); // in release, ma viva

                expect (pool.sounding() != nullptr && pool.sounding()->isReleasing());

                pool.voices.noteOn (72, 1.0f);

                expectEquals (pool.voices.countSounding(), 1, "la voce in release va comunque ripresa");
                expect (! pool.sounding()->isGliding(), "note staccate: l'intonazione deve saltare");
                expectWithinAbsoluteError ((double) pool.sounding()->getFrequencyHz(), noteHz (72), 0.01);
            }

            {
                Pool pool (p, store);

                pool.voices.noteOn (48, 1.0f);
                pool.render ((int) (kSampleRate * 0.1));
                pool.voices.noteOn (72, 1.0f); // il primo tasto e' ancora giu': sovrapposte

                expect (pool.sounding()->isGliding(), "note sovrapposte: deve scivolare");
                expectWithinAbsoluteError (pool.sounding()->getGlideNote(), 48.0f, 0.0f);
            }
        }

        beginTest ("Mono: una voce sola, e l'inviluppo riparte");
        {
            // "Riparte" e' misurabile senza guardare dentro l'inviluppo: con sustain a 0.35 la
            // prima nota si assesta li', e un ritrigger rimette lo stadio in attacco verso il
            // picco, cioe' il livello risale sopra il sustain. Senza ritrigger resterebbe fermo.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.attackSeconds = 0.005f;
            p.decaySeconds = 0.03f;
            p.sustain = 0.35f;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render ((int) (kSampleRate * 0.2)); // ben oltre attacco e decay

            const auto* voice = pool.sounding();
            expect (voice != nullptr);

            const auto atSustain = voice->getAmplitudeLevel();
            expectWithinAbsoluteError (atSustain, 0.35f, 0.02f);

            pool.voices.noteOn (67, 1.0f);
            pool.render ((int) (kSampleRate * 0.004)); // dentro l'attacco

            expectEquals (pool.voices.countActive(), 1, "in Mono suona una voce sola");
            expectEquals (pool.voices.countSounding(), 1, "e nemmeno code di furto");
            expectEquals (pool.sounding()->getMidiNote(), 67);
            expect (pool.sounding()->getAmplitudeLevel() > atSustain + 0.05f,
                    "il livello e' " + juce::String (pool.sounding()->getAmplitudeLevel())
                        + ", partito da " + juce::String (atSustain) + ": l'inviluppo non e' ripartito");
        }

        beginTest ("Legato: una voce sola, e l'inviluppo non riparte");
        {
            // Lo stesso patch e la stessa sequenza del test sopra: l'unica differenza e' il
            // modo, e deve bastare a cambiare il risultato. Il livello resta incollato al
            // sustain — non risale, e soprattutto non torna a zero passando per un note-off.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::legato;
            p.attackSeconds = 0.005f;
            p.decaySeconds = 0.03f;
            p.sustain = 0.35f;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render ((int) (kSampleRate * 0.2));

            const auto atSustain = pool.sounding()->getAmplitudeLevel();
            expectWithinAbsoluteError (atSustain, 0.35f, 0.02f);

            pool.voices.noteOn (67, 1.0f);

            // Campionato fetta per fetta: un ritrigger si vedrebbe come una risalita in
            // qualunque punto dell'attacco, e guardare solo alla fine potrebbe mancarla.
            auto lowest = 1.0f;
            auto highest = 0.0f;

            for (int i = 0; i < 200; ++i)
            {
                pool.render (engine::SynthEngine::kControlBlockSamples);
                lowest = std::min (lowest, pool.sounding()->getAmplitudeLevel());
                highest = std::max (highest, pool.sounding()->getAmplitudeLevel());
            }

            expectEquals (pool.voices.countActive(), 1, "in Legato suona una voce sola");
            expectEquals (pool.sounding()->getMidiNote(), 67, "l'intonazione invece cambia");
            expect (lowest > 0.3f, "il livello e' sceso a " + juce::String (lowest)
                                       + ": l'inviluppo e' ripartito da capo");
            expect (highest < atSustain + 0.02f,
                    "il livello e' salito a " + juce::String (highest) + " da "
                        + juce::String (atSustain) + ": l'inviluppo e' ripartito");
        }

        beginTest ("Legato: senza nessun tasto premuto anche il legato ritriggera");
        {
            // Il rovescio del test sopra, e la ragione per cui la condizione e' "un altro tasto
            // era gia' giu'" e non "il modo e' Legato": a tastiera vuota non c'e' niente a cui
            // legarsi, e una nota che entrasse senza attacco sarebbe solo una nota senza attacco.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::legato;
            p.attackSeconds = 0.005f;
            p.decaySeconds = 0.03f;
            p.sustain = 0.35f;
            p.releaseSeconds = 0.5f;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render ((int) (kSampleRate * 0.2));
            pool.voices.noteOff (60);
            pool.render ((int) (kSampleRate * 0.05)); // in release, ma ancora viva

            const auto beforeRelease = pool.sounding()->getAmplitudeLevel();
            expect (beforeRelease > 0.05f && beforeRelease < 0.35f,
                    "la nota deve essere a meta' del release, e' a " + juce::String (beforeRelease));

            pool.voices.noteOn (67, 1.0f);
            pool.render ((int) (kSampleRate * 0.004));

            expect (pool.sounding()->getAmplitudeLevel() > beforeRelease,
                    "senza tasti premuti la nota nuova deve riattaccare");
        }

        beginTest ("una nota nuova mentre la precedente e' in release riprende la stessa voce");
        {
            // Il caso che distingue un'implementazione buona da una approssimativa. Surge fa
            // cosi' (`reclaimVoiceFor`): la voce si riprende dov'e', gli inviluppi non si
            // azzerano, e nessuno slot nuovo viene aperto. L'alternativa — una voce nuova — si
            // sentirebbe come due note sovrapposte in un modo che si chiama *mono*.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.attackSeconds = 0.005f;
            p.decaySeconds = 0.03f;
            p.sustain = 0.5f;
            p.releaseSeconds = 0.4f;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render ((int) (kSampleRate * 0.2));

            const auto* voice = pool.sounding();
            const auto* slot = voice;

            pool.voices.noteOff (60);
            pool.render ((int) (kSampleRate * 0.1));

            const auto inRelease = voice->getAmplitudeLevel();
            expect (voice->isReleasing(), "la voce deve essere in release");
            expect (inRelease > 0.05f, "e ancora udibile, e' a " + juce::String (inRelease));

            pool.voices.noteOn (67, 1.0f);

            expectEquals (pool.voices.countSounding(), 1,
                          "nessuna voce nuova: la release si riprende dov'era");
            expect (pool.sounding() == slot, "e deve essere proprio lo stesso slot del pool");
            expect (! voice->isReleasing(), "che non e' piu' in release");
            expectEquals (voice->getMidiNote(), 67);

            // Gli inviluppi non azzerati: il livello riparte **da quello del release**, non da
            // zero. Il primo campione reso dopo il ritrigger non puo' quindi stare sotto.
            pool.render (engine::SynthEngine::kControlBlockSamples);
            expect (voice->getAmplitudeLevel() >= inRelease,
                    "livello " + juce::String (voice->getAmplitudeLevel()) + " contro "
                        + juce::String (inRelease) + " prima: l'inviluppo e' stato azzerato");
        }

        beginTest ("un glide interrotto riparte da dove era arrivato");
        {
            // L'altra meta' di `reclaimVoiceFor`: se il glide precedente non era finito, la
            // sorgente del nuovo e' la posizione corrente, non la nota nominale di partenza ne'
            // quella d'arrivo. Senza, un trillo suonato piu' veloce del tempo di glide
            // salterebbe indietro a ogni nota.
            //
            // Il tasto 60 resta premuto per tutta la prova, e non e' un dettaglio del setup: e'
            // la condizione che fa scivolare sia la nota 72 sia la 65 (canGlideFromLastNote
            // vuole un tasto gia' giu'). Con la 60 rilasciata questo sarebbe il caso staccato —
            // la voce verrebbe comunque ripresa, ma l'intonazione salterebbe, e lo verifica
            // "in Mono due note staccate saltano".
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.glideSeconds = 0.4f; // per ottava: il salto di un'ottava dura 400 ms

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render (engine::SynthEngine::kControlBlockSamples);

            const auto* voice = pool.sounding();

            pool.voices.noteOn (72, 1.0f);
            pool.render ((int) (kSampleRate * 0.1)); // un quarto del tempo: siamo al MI bemolle

            const auto interrupted = voice->getGlideNote();
            expectWithinAbsoluteError ((double) interrupted, 63.0, 0.05);

            // Il caso completo: la nota in corso viene anche rilasciata, quindi la voce e' in
            // release *e* a meta' glide quando arriva quella nuova.
            pool.voices.noteOff (72);
            pool.voices.noteOn (65, 1.0f);

            expectEquals (pool.voices.countSounding(), 1);
            expectWithinAbsoluteError (pool.sounding()->getGlideNote(), interrupted, 1.0e-4f,
                                       "il glide nuovo deve partire da dove era arrivato il vecchio");

            // E prosegue da li' verso la nota nuova, non verso quella di prima.
            pool.render ((int) (kSampleRate * 0.5));
            expect (! pool.sounding()->isGliding());
            expectWithinAbsoluteError (pool.sounding()->getGlideNote(), 65.0f, 0.0f);
        }

        beginTest ("lasciando un tasto si torna all'ultimo ancora premuto");
        {
            // Il modello di Odin 2 (PluginProcessorMidi.cpp): la lista dei tasti tenuti e' cio'
            // che rende un trillo su una nota tenuta una cosa suonabile.
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::legato;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.render (128);
            pool.voices.noteOn (64, 1.0f);
            pool.render (128);
            pool.voices.noteOn (67, 1.0f);
            pool.render (128);

            expectEquals (pool.voices.countHeld(), 3);
            expectEquals (pool.sounding()->getMidiNote(), 67);

            // Si lascia quella che **non** sta suonando: non deve succedere niente.
            pool.voices.noteOff (60);
            pool.render (128);
            expectEquals (pool.sounding()->getMidiNote(), 67, "lasciare un tasto di sotto non muove la voce");
            expectEquals (pool.voices.countHeld(), 2);

            // Si lascia quella che sta suonando: si torna sull'ultima rimasta.
            pool.voices.noteOff (67);
            pool.render (128);
            expectEquals (pool.sounding()->getMidiNote(), 64, "si deve tornare sull'ultimo tasto premuto");
            expect (! pool.sounding()->isReleasing(), "e la voce non deve andare in release");

            // Si lascia l'ultima: adesso si spegne.
            pool.voices.noteOff (64);
            pool.render (128);
            expectEquals (pool.voices.countHeld(), 0);
            expect (pool.sounding()->isReleasing(), "senza tasti premuti la voce deve rilasciare");
        }

        beginTest ("in Mono e Legato il furto di voce non scatta mai");
        {
            // La polifonia e' uno per costruzione, quindi countActive() non puo' arrivare a
            // maxVoices e chooseVictim() non viene mai chiamata. E' una proprieta' da
            // verificare, non da assumere: il percorso del furto e' quello che kill()a le voci.
            for (auto mode : { engine::VoiceMode::mono, engine::VoiceMode::legato })
            {
                auto p = plainPatch();
                p.voiceMode = mode;
                p.releaseSeconds = 2.0f; // lunghissimo: le code si accumulerebbero, se ce ne fossero

                Pool pool (p, store);

                for (int i = 0; i < 64; ++i)
                {
                    pool.voices.noteOn (36 + (i * 7) % 60, 0.5f + 0.005f * (float) i);
                    pool.render (64);

                    if (i % 3 == 0)
                        pool.voices.noteOff (36 + (i * 7) % 60);

                    expect (pool.voices.countActive() <= 1,
                            "voci attive: " + juce::String (pool.voices.countActive()));
                    expectEquals (pool.countFading(), 0, "nessuna dissolvenza da furto");
                }

                pool.voices.allNotesOff();
                expectEquals (pool.voices.countHeld(), 0);
            }
        }

        beginTest ("il glide muove la nota base, e il detune dell'unison resta relativo");
        {
            // L'unison ha fino a otto oscillatori con il proprio rapporto di detune, calcolato
            // in updateUnison() e moltiplicato per la frequenza base. Il glide deve entrare
            // *nella base*: se per errore entrasse dopo, i battimenti cambierebbero durante il
            // glide. Si verifica che la base a meta' glide sia la stessa con una copia e con
            // otto — cioe' che l'unison non la sposti — e che l'uscita resti sana.
            auto mono = plainPatch();
            mono.voiceMode = engine::VoiceMode::mono;
            mono.glideSeconds = 0.1f;

            auto wide = mono;
            wide.unisonVoices = 8;
            wide.detuneCents = 25.0f;

            auto midpointHz = [&] (const engine::EngineParams& p)
            {
                Pool pool (p, store);
                pool.voices.noteOn (60, 1.0f);
                pool.render (engine::SynthEngine::kControlBlockSamples);
                pool.voices.noteOn (72, 1.0f);
                pool.render (2400);
                return pool.sounding()->getFrequencyHz();
            };

            expectWithinAbsoluteError (midpointHz (wide), midpointHz (mono), 0.0f,
                                       "la nota base a meta' glide non deve dipendere dall'unison");
        }

        beginTest ("glide e modulazione di `fine` si compongono");
        {
            // `fine` e' l'unico target del matrix che finisce in tuningSemitones_, cioe' l'unica
            // modulazione che condivide l'aritmetica con il glide. Devono sommarsi: un vibrato
            // su una nota che glidda oscilla attorno al punto in cui il glide e' arrivato. Se
            // uno dei due sovrascrivesse l'altro, la frequenza a meta' glide sarebbe quella del
            // FA# esatto (glide che vince) o quella del DO stonato (modulazione che vince).
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::mono;
            p.glideSeconds = 0.1f;
            p.velocityAmount = 0.0f;

            // fine e' Map::Linear su -100..100 cent: il grezzo 0.5 e' lo zero, 0.75 e' +50 cent.
            const auto fineTarget = engine::modTargetIndexFor (params::ParamSlot::fine);
            p.modBase[(size_t) fineTarget] = 0.5f;
            p.fineCents = 0.0f;

            engine::ModSnapshot snapshot;
            snapshot.count = 1;
            snapshot.routes[0] = { engine::ModSource::vel, fineTarget, 0.25f };
            p.mods = &snapshot;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f); // velocity 1 -> 0.5 + 0.25 = 0.75 -> +50 cent
            pool.render (engine::SynthEngine::kControlBlockSamples);
            expectWithinAbsoluteError ((double) pool.sounding()->getFrequencyHz(), noteHz (60.5), 0.01);

            pool.voices.noteOn (72, 1.0f);
            pool.render (2400);

            expectWithinAbsoluteError ((double) pool.sounding()->getGlideNote(), 66.0, 1.0e-4);
            expectWithinAbsoluteError ((double) pool.sounding()->getFrequencyHz(), noteHz (66.5), 0.01,
                                       "il mezzo semitono di `fine` deve sommarsi al glide, non sostituirlo");
        }

        beginTest ("cambiare modo di voce non lascia note appese");
        {
            auto p = plainPatch();
            p.voiceMode = engine::VoiceMode::poly;

            Pool pool (p, store);

            pool.voices.noteOn (60, 1.0f);
            pool.voices.noteOn (64, 1.0f);
            pool.render (256);
            expectEquals (pool.voices.countActive(), 2);

            p.voiceMode = engine::VoiceMode::mono;
            pool.voices.setParams (p);
            pool.render ((int) (kSampleRate * 0.5)); // oltre il release

            expectEquals (pool.voices.countSounding(), 0,
                          "le voci della modalita' precedente devono essersi spente");
            expectEquals (pool.voices.countHeld(), 0);
        }

        beginTest ("non-regressione: in Poly e con glide a zero l'uscita e' bit per bit quella di prima");
        {
            // Il numero non e' arbitrario: e' il checksum dello stesso render, e la proprieta'
            // che protegge e' che **nessuna operazione in piu'** entri sul percorso a glide
            // spento — un'addizione con zero, una moltiplicazione per uno che non e' esattamente
            // uno, un ordine di operazioni diverso in midiNoteToHz lo cambierebbero.
            //
            // E' stato riscritto una volta, dalla ritaratura del gain staging, e quello e'
            // l'unico modo legittimo di toccarlo: un cambiamento di suono **voluto e misurato**
            // altrove. Qui sono entrati due valori nuovi — kVoiceHeadroomGain da 0.71 a 0.63 e la
            // mappa di `drive`, che questo patch usa a 2.0 di guadagno — e il checksum e'
            // ricalcolato sul binario subito dopo. Il valore precedente era
            // 16826057911581881754, quello di prima che glide e voiceMode fossero cablati.
            //
            // Attenzione: il checksum e' sull'uscita bit per bit, quindi dipende dalla
            // configurazione di compilazione. Va preso dalla build Debug, che e' quella su cui
            // gira la suite (docs/build.md): in Release il riordinamento delle operazioni in
            // virgola mobile ne produce un altro, ugualmente valido e diverso.
            constexpr std::uint64_t before = 12591846455056921160ull;

            auto poly = glidePatch();
            expect (poly.voiceMode == engine::VoiceMode::poly);
            expectWithinAbsoluteError (poly.glideSeconds, 0.0f, 0.0f);

            const auto now = referenceChecksum (poly, store);
            logMessage ("checksum del render di riferimento: " + juce::String (now));

            expect (now == before, "checksum " + juce::String (now) + ", atteso "
                                       + juce::String (before) + ": l'uscita a glide spento e' cambiata");

            // E il contrappeso, senza il quale il test sopra sarebbe soddisfatto anche da un
            // glide che non fa niente: con il glide acceso il render **deve** cambiare.
            auto gliding = glidePatch();
            gliding.glideSeconds = 0.25f;

            expect (referenceChecksum (gliding, store) != before,
                    "con il glide acceso l'uscita e' identica: il glide non sta facendo niente");
        }
    }
};

static GlideVoiceModeTests glideVoiceModeTests;
