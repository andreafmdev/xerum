#include "dsp/WavetableStore.h"
#include "engine/Arpeggiator.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;

/** Il patch piu' semplice che sappia suonare, con un release lungo abbastanza da poter
    distinguere "in release" da "spenta" senza correre. */
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
    p.releaseSeconds = 0.5f;
    p.velocityAmount = 0.0f;
    p.pan = 0.0f;
    return p;
}

/** Un pool di voci pronto a suonare, reso a sotto-fette come fa SynthEngine. */
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
};
/** Un arpeggiatore che gira: rate fisso, gate a meta', niente swing, transport fermo. */
engine::ArpConfig arpConfig()
{
    engine::ArpConfig cfg;
    cfg.on = true;
    cfg.mode = engine::ArpMode::up;
    cfg.rateRaw = 0.3f;     // sedicesimi
    cfg.octaves = 1;
    cfg.gate01 = 0.5f;
    cfg.swing01 = 0.0f;
    return cfg;
}

/** Fa girare l'arp e ritorna i soli note-on che ne escono, con la posizione assoluta. */
std::vector<std::pair<int, int>> arpNoteOns (engine::Arpeggiator& arp, const engine::ArpConfig& cfg,
                                             const std::vector<std::pair<int, juce::MidiMessage>>& input,
                                             int totalSamples, int blockSize)
{
    std::vector<std::pair<int, int>> out;
    int absolute = 0;
    size_t next = 0;

    while (absolute < totalSamples)
    {
        const auto numSamples = std::min (blockSize, totalSamples - absolute);

        juce::MidiBuffer midi;

        while (next < input.size() && input[next].first < absolute + numSamples)
        {
            midi.addEvent (input[next].second, std::max (0, input[next].first - absolute));
            ++next;
        }

        engine::ArpTransport transport;
        transport.bpm = 120.0;
        transport.isPlaying = false;
        transport.ppqPosition = 0.0;

        arp.process (midi, numSamples, cfg, transport);

        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn())
                out.emplace_back (absolute + metadata.samplePosition, metadata.getMessage().getNoteNumber());

        absolute += numSamples;
    }

    return out;
}
/** Un SynthEngine pronto a suonare, guidato a blocchi con eventi MIDI a offset zero. */
struct Engine
{
    engine::SynthEngine synth;
    juce::AudioBuffer<float> buffer { 2, 128 };

    Engine (const engine::EngineParams& p, dsp::WavetableStore& store)
    {
        engine::EngineSpec spec;
        spec.sampleRate = kSampleRate;
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());
        synth.setParams (p);
        synth.setMasterGainLinear (1.0f);
    }

    /** Rende `blocks` blocchi da 128, consegnando `midi` al primo. Ritorna il picco assoluto. */
    float run (int blocks, const juce::MidiBuffer& midi = {})
    {
        float peak = 0.0f;

        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer thisBlock;

            if (b == 0)
                thisBlock = midi;

            buffer.clear();
            synth.process (buffer, thisBlock);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    peak = std::max (peak, std::abs (buffer.getSample (ch, i)));
        }

        return peak;
    }

    /**
     * Rende `blocks` blocchi e ritorna il picco degli **ultimi quattro**, non di tutti.
     *
     * La differenza conta: il picco sull'intera passata include la coda di release ancora
     * forte dei primi blocchi, quindi un "la nota si e' spenta?" misurato cosi' passerebbe
     * anche quando la nota non si e' spenta affatto.
     */
    float peakAtEnd (int blocks)
    {
        run (std::max (0, blocks - 4));
        return run (4);
    }

    /** Quante note risultano accese nella maschera che il keybed della UI legge. */
    int litNotes() const
    {
        return juce::BigInteger ((juce::int64) synth.getActiveNotesLo()).countNumberOfSetBits()
             + juce::BigInteger ((juce::int64) synth.getActiveNotesHi()).countNumberOfSetBits();
    }

    bool isLit (int note) const
    {
        const auto word = note < 64 ? synth.getActiveNotesLo() : synth.getActiveNotesHi();
        return (word & (1ull << (note % 64))) != 0;
    }
};

juce::MidiBuffer midiOf (std::initializer_list<juce::MidiMessage> messages)
{
    juce::MidiBuffer out;

    for (const auto& m : messages)
        out.addEvent (m, 0);

    return out;
}
} // namespace

/**
 * Il pedale di sustain (CC 64). Prima non esisteva: handleMidiEvent leggeva il solo CC 1 e ogni
 * altro controller cadeva nel vuoto, quindi un note-off a pedale giu' spegneva la nota.
 */
struct SustainPedalTests final : juce::UnitTest
{
    SustainPedalTests() : juce::UnitTest ("pedale di sustain", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (0);

        beginTest ("a pedale giu' il note-off non manda la voce in release");
        {
            Pool pool (plainPatch(), store);

            pool.voices.setSustainPedal (true);
            pool.voices.noteOn (60, 0.9f);
            pool.render (480);

            const auto* voice = pool.voiceFor (60);
            expect (voice != nullptr);

            pool.voices.noteOff (60);
            pool.render (480);

            voice = pool.voiceFor (60);
            expect (voice != nullptr, "la voce si e' spenta con il pedale giu'");
            expect (! voice->isReleasing(), "la voce e' in release con il pedale giu'");
        }

        beginTest ("alzare il pedale rilascia le note che il pedale teneva");
        {
            Pool pool (plainPatch(), store);

            pool.voices.setSustainPedal (true);
            pool.voices.noteOn (60, 0.9f);
            pool.render (480);
            pool.voices.noteOff (60);
            pool.render (480);

            pool.voices.setSustainPedal (false);
            pool.render (480);

            const auto* voice = pool.voiceFor (60);
            expect (voice != nullptr, "la voce e' sparita di colpo invece di andare in release");
            expect (voice->isReleasing(), "alzare il pedale non ha rilasciato la nota");
        }

        beginTest ("un tasto ancora premuto non viene rilasciato dal pedale che si alza");
        {
            Pool pool (plainPatch(), store);

            pool.voices.setSustainPedal (true);
            pool.voices.noteOn (60, 0.9f);
            pool.voices.noteOn (64, 0.9f);
            pool.render (480);

            pool.voices.noteOff (60);   // dito alzato: la tiene il pedale
            pool.render (480);

            pool.voices.setSustainPedal (false);
            pool.render (480);

            const auto* released = pool.voiceFor (60);
            const auto* stillHeld = pool.voiceFor (64);

            expect (released != nullptr && released->isReleasing(), "60 doveva essere rilasciata");
            expect (stillHeld != nullptr && ! stillHeld->isReleasing(),
                    "64 e' ancora premuta: il pedale non deve toccarla");
        }

        beginTest ("ripremere un tasto a pedale giu' lo toglie dalle note differite");
        {
            Pool pool (plainPatch(), store);

            pool.voices.setSustainPedal (true);
            pool.voices.noteOn (60, 0.9f);
            pool.render (480);
            pool.voices.noteOff (60);
            pool.render (480);
            pool.voices.noteOn (60, 0.9f);   // il dito ha ripreso il tasto
            pool.render (480);

            pool.voices.setSustainPedal (false);
            pool.render (480);

            const auto* voice = pool.voiceFor (60);
            expect (voice != nullptr && ! voice->isReleasing(),
                    "il tasto e' premuto: alzare il pedale non deve rilasciarlo");
        }

        beginTest ("in Mono il pedale che si alza non ritriggera l'inviluppo");
        {
            // Il caso per cui releaseSustainedNotes() rilascia dal tasto piu' vecchio. Tre note
            // tenute dal pedale: rilasciandole dalla piu' recente, ognuna farebbe tornare la
            // voce sul tasto precedente e ritriggerare, cioe' due attacchi di troppo prima del
            // silenzio. Si misura sul contatore dei ritrigger della voce.
            auto patch = plainPatch();
            patch.voiceMode = engine::VoiceMode::mono;
            patch.attackSeconds = 0.05f;   // un attacco udibile: un ritrigger si vedrebbe

            Pool pool (patch, store);

            pool.voices.setSustainPedal (true);
            pool.voices.noteOn (60, 0.9f);
            pool.render (480);
            pool.voices.noteOn (64, 0.9f);
            pool.render (480);
            pool.voices.noteOn (67, 0.9f);
            pool.render (480);

            pool.voices.noteOff (60);
            pool.voices.noteOff (64);
            pool.voices.noteOff (67);
            pool.render (480);

            expect (pool.voiceFor (67) != nullptr, "la voce mono doveva restare sul 67");

            const auto levelBefore = pool.voiceFor (67)->getAmplitudeLevel();

            pool.voices.setSustainPedal (false);
            pool.render (240);

            // Un ritrigger riporterebbe l'inviluppo all'attacco, cioe' **su**. Il rilascio
            // corretto lo porta giu' e basta.
            const auto* voice = pool.voiceFor (67);
            expect (voice != nullptr, "la voce e' sparita di colpo invece di andare in release");
            expect (voice->isReleasing(), "alzare il pedale non ha rilasciato la nota");
            expect (voice->getAmplitudeLevel() <= levelBefore,
                    "il livello e' risalito: l'inviluppo e' stato ritriggerato");
            expect (pool.voiceFor (60) == nullptr && pool.voiceFor (64) == nullptr,
                    "in Mono deve restare una voce sola");
        }

        beginTest ("allNotesOff azzera le note differite");
        {
            Pool pool (plainPatch(), store);

            pool.voices.setSustainPedal (true);
            pool.voices.noteOn (60, 0.9f);
            pool.render (480);
            pool.voices.noteOff (60);
            pool.render (480);

            pool.voices.allNotesOff();
            pool.render (480);

            pool.voices.noteOn (60, 0.9f);
            pool.render (480);
            pool.voices.setSustainPedal (false);
            pool.render (480);

            const auto* voice = pool.voiceFor (60);
            expect (voice != nullptr && ! voice->isReleasing(),
                    "una nota differita sopravvissuta al panico ha rilasciato la nota nuova");
        }

        beginTest ("una nota uscita dalla lista dei tasti viene rilasciata lo stesso");
        {
            // `held_` tiene sedici tasti e a tastiera piena scarta il piu' vecchio, mentre il
            // furto sceglie la voce con lo startOrder piu' basso. Normalmente sono lo stesso
            // tasto, e la nota che esce dalla lista e' anche quella che viene rubata — quindi
            // non resta appesa comunque. I due ordini divergono se un tasto viene **ribattuto**:
            // ribattere lo sposta in cima a `held_` ma non gli cambia lo startOrder.
            //
            // Qui si costruisce proprio quella divergenza. Senza la spazzata dei bit rimasti
            // fuori da `held_`, la nota 62 resterebbe a suonare per sempre.
            Pool pool (plainPatch(), store);

            pool.voices.setSustainPedal (true);

            for (int note = 61; note <= 76; ++note)   // sedici tasti: la lista e' piena
            {
                pool.voices.noteOn (note, 0.9f);
                pool.render (32);
            }

            pool.voices.noteOn (61, 0.9f);   // ribattuto: in cima a held_, startOrder invariato
            pool.render (32);

            pool.voices.noteOn (77, 0.9f);   // il diciassettesimo: ruba 61, scarta 62 da held_
            pool.render (32);

            for (int note = 61; note <= 77; ++note)
                pool.voices.noteOff (note);

            pool.render (32);

            expect (pool.voiceFor (62) != nullptr, "il caso non si e' costruito: 62 non suona piu'");

            pool.voices.setSustainPedal (false);
            pool.render (32);

            const auto* orphan = pool.voiceFor (62);
            expect (orphan != nullptr && orphan->isReleasing(),
                    "62 e' uscita da held_ e nessuno le ha mandato il note-off: nota appesa");
        }

        beginTest ("con l'arp acceso il pedale fa latch dei tasti");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            const auto cfg = arpConfig();

            // Accordo premuto, pedale giu', poi **tutte** le dita alzate. L'arp deve continuare
            // a girare sulle tre note, perche' il pedale le trattiene nella sua lista.
            std::vector<std::pair<int, juce::MidiMessage>> input {
                { 0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) },
                { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) },
                { 0, juce::MidiMessage::noteOn (1, 67, 0.9f) },
                { 6000, juce::MidiMessage::noteOff (1, 60) },
                { 6000, juce::MidiMessage::noteOff (1, 64) },
                { 6000, juce::MidiMessage::noteOff (1, 67) },
            };

            const auto notes = arpNoteOns (arp, cfg, input, 96000, 128);

            int afterRelease = 0;

            for (const auto& [sample, note] : notes)
                if (sample > 12000)
                    ++afterRelease;

            expect (afterRelease > 0, "l'arp si e' fermato a dita alzate: il latch non c'e'");

            // E le tre note sono ancora tutte nella sequenza, non solo una.
            bool saw60 = false, saw64 = false, saw67 = false;

            for (const auto& [sample, note] : notes)
                if (sample > 12000)
                {
                    saw60 = saw60 || note == 60;
                    saw64 = saw64 || note == 64;
                    saw67 = saw67 || note == 67;
                }

            expect (saw60 && saw64 && saw67, "il latch ha perso qualche nota dell'accordo");
        }

        beginTest ("alzare il pedale ferma l'arp se nessun tasto e' premuto");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            const auto cfg = arpConfig();

            std::vector<std::pair<int, juce::MidiMessage>> input {
                { 0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) },
                { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) },
                { 6000, juce::MidiMessage::noteOff (1, 60) },
                { 6000, juce::MidiMessage::noteOff (1, 64) },
                { 24000, juce::MidiMessage::controllerEvent (1, 64, 0) },
            };

            const auto notes = arpNoteOns (arp, cfg, input, 96000, 128);

            int beforePedalUp = 0;
            int afterPedalUp = 0;

            for (const auto& [sample, note] : notes)
                (sample < 24000 ? beforePedalUp : afterPedalUp) += 1;

            expect (beforePedalUp > 0, "l'arp non stava nemmeno girando");
            expect (afterPedalUp == 0, "l'arp gira ancora dopo che il pedale e' salito");
        }

        beginTest ("un tasto ancora premuto sopravvive al pedale che si alza");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            const auto cfg = arpConfig();

            // 60 viene lasciato a pedale giu' (lo tiene il latch), 64 resta premuto per davvero.
            std::vector<std::pair<int, juce::MidiMessage>> input {
                { 0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) },
                { 0, juce::MidiMessage::noteOn (1, 64, 0.9f) },
                { 6000, juce::MidiMessage::noteOff (1, 60) },
                { 24000, juce::MidiMessage::controllerEvent (1, 64, 0) },
            };

            const auto notes = arpNoteOns (arp, cfg, input, 96000, 128);

            bool saw60 = false, saw64 = false;

            for (const auto& [sample, note] : notes)
                if (sample > 30000)
                {
                    saw60 = saw60 || note == 60;
                    saw64 = saw64 || note == 64;
                }

            expect (saw64, "il tasto ancora premuto e' stato tolto dal pedale che si alza");
            expect (! saw60, "il tasto lasciato e' sopravvissuto al pedale che si alza");
        }

        beginTest ("ripremere un tasto in latch lo riporta sotto il dito");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            const auto cfg = arpConfig();

            std::vector<std::pair<int, juce::MidiMessage>> input {
                { 0, juce::MidiMessage::controllerEvent (1, 64, 127) },
                { 0, juce::MidiMessage::noteOn (1, 60, 0.9f) },
                { 6000, juce::MidiMessage::noteOff (1, 60) },      // lo tiene il latch
                { 12000, juce::MidiMessage::noteOn (1, 60, 0.9f) }, // il dito lo riprende
                { 24000, juce::MidiMessage::controllerEvent (1, 64, 0) },
            };

            const auto notes = arpNoteOns (arp, cfg, input, 96000, 128);

            int afterPedalUp = 0;

            for (const auto& [sample, note] : notes)
                if (sample > 30000)
                    ++afterPedalUp;

            expect (afterPedalUp > 0, "il pedale ha tolto un tasto che il dito stava premendo");
        }

        beginTest ("CC 64 tiene la nota e la lascia accesa sul keybed");
        {
            Engine e (plainPatch(), store);

            e.run (4, midiOf ({ juce::MidiMessage::controllerEvent (1, 64, 127),
                                juce::MidiMessage::noteOn (1, 60, 0.9f) }));

            const auto heldPeak = e.run (4);
            expect (heldPeak > 0.05f, "la nota non sta suonando");
            expect (e.isLit (60), "il keybed non mostra la nota premuta");

            e.run (4, midiOf ({ juce::MidiMessage::noteOff (1, 60) }));

            // Piu' di un release intero (0.5 s = 187 blocchi): senza il pedale, qui non ci
            // sarebbe piu' niente. Misurare subito dopo il note-off proverebbe solo che la coda
            // di release non e' istantanea.
            expect (e.peakAtEnd (300) > 0.05f, "il dito si e' alzato e la nota si e' spenta: il pedale non tiene");
            expect (e.isLit (60), "il keybed ha spento una nota che il pedale sta ancora tenendo");
        }

        beginTest ("alzare il CC 64 spegne la nota e il suo tasto sul keybed");
        {
            Engine e (plainPatch(), store);

            e.run (4, midiOf ({ juce::MidiMessage::controllerEvent (1, 64, 127),
                                juce::MidiMessage::noteOn (1, 60, 0.9f) }));
            e.run (4, midiOf ({ juce::MidiMessage::noteOff (1, 60) }));
            e.run (4, midiOf ({ juce::MidiMessage::controllerEvent (1, 64, 0) }));

            expect (! e.isLit (60), "il keybed mostra ancora una nota che nessuno tiene");

            // Un release di mezzo secondo: dopo un secondo non deve restare niente.
            expect (e.peakAtEnd (400) < 0.01f, "la nota non si e' spenta dopo che il pedale e' salito");
        }

        beginTest ("con l'arp acceso il pedale non accumula le note che l'arp emette");
        {
            // Senza arbitro il pedale terrebbe *ogni* nota che l'arp emette, e la maschera
            // crescerebbe a ogni passo fino a coprire tutta la sequenza. Con l'arp acceso il
            // sustain sulle voci deve restare spento: se ne occupa il latch dell'arpeggiatore.
            auto patch = plainPatch();
            patch.arp = arpConfig();

            Engine e (patch, store);

            e.run (4, midiOf ({ juce::MidiMessage::controllerEvent (1, 64, 127),
                                juce::MidiMessage::noteOn (1, 60, 0.9f),
                                juce::MidiMessage::noteOn (1, 64, 0.9f),
                                juce::MidiMessage::noteOn (1, 67, 0.9f) }));

            int worst = 0;

            for (int i = 0; i < 40; ++i)
            {
                e.run (4);
                worst = std::max (worst, e.litNotes());
            }

            // Gate a meta' passo: al piu' una nota alla volta, due nell'istante di sovrapposizione.
            expect (worst <= 2, "le note dell'arp si stanno accumulando: ne risultano accese "
                                    + juce::String (worst));
        }

        beginTest ("accendere l'arp a pedale gia' giu' non lascia il sustain armato");
        {
            auto patch = plainPatch();

            Engine e (patch, store);

            // Pedale giu' con l'arp spento: da qui il sustain sulle voci e' armato.
            e.run (4, midiOf ({ juce::MidiMessage::controllerEvent (1, 64, 127),
                                juce::MidiMessage::noteOn (1, 60, 0.9f) }));
            e.run (4, midiOf ({ juce::MidiMessage::noteOff (1, 60) }));

            // E adesso l'arp si accende, con il piede ancora sul pedale.
            patch.arp = arpConfig();
            e.synth.setParams (patch);

            e.run (4, midiOf ({ juce::MidiMessage::noteOn (1, 64, 0.9f),
                                juce::MidiMessage::noteOn (1, 67, 0.9f) }));

            int worst = 0;

            for (int i = 0; i < 40; ++i)
            {
                e.run (4);
                worst = std::max (worst, e.litNotes());
            }

            expect (worst <= 2, "il sustain e' rimasto armato sotto l'arp: note accese "
                                    + juce::String (worst));
        }
    }
};

static SustainPedalTests sustainPedalTests;
