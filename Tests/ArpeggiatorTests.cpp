#include "dsp/WavetableStore.h"
#include "engine/Arpeggiator.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"
#include "engine/VoiceManager.h"
#include "parameters/ParameterMapping.h"
#include "parameters/ParameterTable.h"
#include "parameters/StateTree.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kBpm = 120.0;

/** Campioni per quarto alle costanti qui sopra: 48000 x 60 / 120. Scritto come formula e non
    come 24000 perche' e' cio' che lega i numeri attesi dei test ai due parametri di partenza. */
constexpr double kSamplesPerBeat = kSampleRate * 60.0 / kBpm;

/** Il grezzo di `arpRate` che sceglie ognuna delle quattro divisioni. Gli estremi degli
    intervalli, non i centri: se la formula di selezione scivolasse di un indice si vedrebbe. */
constexpr float kRate32 = 0.1f;
constexpr float kRate16 = 0.3f;
constexpr float kRate8 = 0.6f;
constexpr float kRate4 = 0.95f;

/** Un evento MIDI da consegnare all'arp a un campione assoluto della passata. */
struct Input
{
    int sample { 0 };
    juce::MidiMessage message;
};

/** Una nota uscita dall'arp, con la sua posizione assoluta. */
struct Emitted
{
    int sample { 0 };
    bool on { false };
    int note { 0 };
    float velocity { 0.0f };
};

/**
 * Fa girare l'arpeggiatore su `totalSamples` campioni, a blocchi di `blockSize`, consegnandogli
 * gli eventi di `input` al campione giusto e raccogliendo cio' che esce.
 *
 * I blocchi non sono un dettaglio: l'arp riscrive un MidiBuffer per blocco e tiene le sue
 * posizioni relative all'inizio di quello corrente, quindi tutta l'aritmetica dei confini si
 * esercita solo se la passata e' spezzata. I test importanti girano con piu' di una lunghezza di
 * blocco, perche' e' il modo di accorgersi se qualcosa dipende da dove cade il confine.
 */
std::vector<Emitted> runArp (engine::Arpeggiator& arp, const engine::ArpConfig& cfg,
                             const std::vector<Input>& input, int totalSamples, int blockSize,
                             bool playing, double startPpq = 0.0)
{
    std::vector<Emitted> out;
    int absolute = 0;
    size_t next = 0;

    while (absolute < totalSamples)
    {
        const auto numSamples = std::min (blockSize, totalSamples - absolute);

        juce::MidiBuffer midi;

        while (next < input.size() && input[next].sample < absolute + numSamples)
        {
            midi.addEvent (input[next].message, std::max (0, input[next].sample - absolute));
            ++next;
        }

        engine::ArpTransport transport;
        transport.bpm = kBpm;
        transport.isPlaying = playing;
        transport.ppqPosition = startPpq + (double) absolute / kSamplesPerBeat;

        arp.process (midi, numSamples, cfg, transport);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn() || message.isNoteOff())
                out.push_back ({ absolute + metadata.samplePosition, message.isNoteOn(),
                                 message.getNoteNumber(), message.getFloatVelocity() });
        }

        absolute += numSamples;
    }

    return out;
}

/** I soli note-on della passata. */
std::vector<Emitted> onlyNoteOns (const std::vector<Emitted>& events)
{
    std::vector<Emitted> out;

    for (const auto& e : events)
        if (e.on)
            out.push_back (e);

    return out;
}

/** Un accordo premuto tutto a campione zero. */
std::vector<Input> chordAt (int sample, std::initializer_list<int> notes, bool on = true)
{
    std::vector<Input> out;

    for (auto n : notes)
        out.push_back ({ sample, on ? juce::MidiMessage::noteOn (1, n, 0.9f)
                                    : juce::MidiMessage::noteOff (1, n) });

    return out;
}

void append (std::vector<Input>& into, const std::vector<Input>& more)
{
    into.insert (into.end(), more.begin(), more.end());
}

/** La configurazione di partenza: acceso, 1/8, gate a meta', un'ottava, niente swing, ogni step
    acceso (steps == nullptr). I singoli test cambiano solo cio' che misurano. */
engine::ArpConfig baseConfig()
{
    engine::ArpConfig cfg;
    cfg.on = true;
    cfg.mode = engine::ArpMode::up;
    cfg.rateRaw = kRate8;
    cfg.gate01 = 0.5f;
    cfg.octaves = 1;
    cfg.swing01 = 0.0f;
    cfg.steps = nullptr;
    return cfg;
}

/** Il patch piu' semplice che sappia suonare, per il test delle note appese: cio' che si misura
    li' e' quante voci restano accese, e ogni stadio in piu' e' solo un modo di confondere la
    misura. */
engine::EngineParams plainPatch()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.level = 1.0f;
    p.unisonVoices = 1;
    p.filterOn = false;
    p.attackSeconds = 0.001f;
    p.decaySeconds = 0.001f;
    p.sustain = 1.0f;
    p.releaseSeconds = 0.02f;
    return p;
}

/** FNV-1a sui bit dei campioni: due render identici danno lo stesso numero, uno diverso di un
    solo ulp su un solo campione no. Stesso strumento di Tests/GlideVoiceModeTests.cpp. */
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

/** Il contenuto di un MidiBuffer come lista confrontabile: byte per byte e posizione per
    posizione. Serve a dire "non e' stato toccato" senza margini di interpretazione. */
std::vector<std::pair<int, juce::String>> dumpMidi (const juce::MidiBuffer& midi)
{
    std::vector<std::pair<int, juce::String>> out;

    for (const auto metadata : midi)
    {
        juce::String bytes;

        for (int i = 0; i < metadata.numBytes; ++i)
            bytes << juce::String::toHexString (metadata.data[i]) << " ";

        out.emplace_back (metadata.samplePosition, bytes);
    }

    return out;
}
} // namespace

struct ArpeggiatorTimingTests final : juce::UnitTest
{
    ArpeggiatorTimingTests() : juce::UnitTest ("Arpeggiator timing", "engine") {}

    void runTest() override
    {
        beginTest ("le quattro divisioni sono quelle che il knob mostra");
        {
            // Non due tabelle che si assomigliano: qui si legge l'etichetta che l'host e la UI
            // vedono (params::formatValue, ramo Label::ArpRate) e la si converte in quarti, poi
            // la si confronta con engine::arpBeatsPerStep. Se una delle due scivola di un indice
            // il test lo dice — ed e' esattamente il difetto che riusare dsp::syncedRateHz, con
            // le sue sei divisioni diverse, avrebbe introdotto in silenzio.
            constexpr auto* spec = params::find ("arpRate");
            expect (spec != nullptr, "arpRate non e' in ParameterTable.h");

            for (auto raw : { 0.0f, kRate32, 0.24f, kRate16, 0.49f, kRate8, 0.74f, kRate4, 1.0f })
            {
                const auto text = params::formatValue (*spec, raw);
                expect (text.startsWith ("1/"), "l'etichetta di arpRate non e' una divisione: " + text);

                // "1/8" vuol dire un ottavo di semibreve, cioe' 4/8 quarti.
                const auto beatsFromLabel = 4.0 / text.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();

                expectWithinAbsoluteError (engine::arpBeatsPerStep (raw), beatsFromLabel, 1.0e-12,
                                           "la tabella C++ e l'etichetta non concordano a raw=" + juce::String (raw));
            }
        }

        beginTest ("a 120 BPM e 1/8 le note escono ogni 250 ms, campione-esatte");
        {
            // 0.5 quarti a 120 BPM sono 250 ms, cioe' 12000 campioni a 48 kHz. Il numero atteso e'
            // scritto come formula proprio per non poterlo far coincidere per caso.
            const auto step = (int) (0.5 * kSamplesPerBeat);
            expectEquals (step, 12000);

            for (auto blockSize : { 32, 64, 128, 512, 1000 })
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                const auto events = onlyNoteOns (runArp (arp, baseConfig(), chordAt (0, { 60 }),
                                                         4 * step, blockSize, false));

                expectEquals ((int) events.size(), 4, "blocchi da " + juce::String (blockSize));

                for (int i = 0; i < (int) events.size(); ++i)
                    expectEquals (events[(size_t) i].sample, i * step,
                                  "passo " + juce::String (i) + " fuori posto con blocchi da "
                                      + juce::String (blockSize));
            }
        }

        beginTest ("la divisione segue arpRate: 1/32, 1/16, 1/8, 1/4");
        {
            const std::pair<float, double> cases[] = {
                { kRate32, 0.125 }, { kRate16, 0.25 }, { kRate8, 0.5 }, { kRate4, 1.0 }
            };

            for (const auto& [raw, beats] : cases)
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                auto cfg = baseConfig();
                cfg.rateRaw = raw;

                const auto step = (int) (beats * kSamplesPerBeat);
                const auto events = onlyNoteOns (runArp (arp, cfg, chordAt (0, { 60 }), 3 * step, 128, false));

                expectEquals ((int) events.size(), 3);
                expectEquals (events[1].sample, step, "divisione " + juce::String (beats) + " quarti");
                expectEquals (events[2].sample, 2 * step);
            }
        }

        beginTest ("a swing 50 % i passi si alternano e la somma della coppia resta invariante");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.swing01 = 0.5f;

            const auto step = 0.5 * kSamplesPerBeat;             // 12000: il passo senza swing
            const auto longStep = (int) (step * 1.25);           // 15000
            const auto shortStep = (int) (step * 0.75);          // 9000

            const auto events = onlyNoteOns (runArp (arp, cfg, chordAt (0, { 60 }), 5 * (int) step, 128, false));

            expect (events.size() >= 5u);

            // Pari lunghi, dispari corti: la coppia dura sempre 2 T0, quindi lo swing sposta il
            // secondo ottavo senza cambiare il tempo. E' la proprieta' che il flip-flop di Surge
            // non garantisce.
            for (int i = 0; i + 1 < (int) events.size(); ++i)
            {
                const auto gap = events[(size_t) i + 1].sample - events[(size_t) i].sample;
                expectEquals (gap, i % 2 == 0 ? longStep : shortStep,
                              "intervallo " + juce::String (i) + " sbagliato");
            }

            for (int i = 0; i + 2 < (int) events.size(); i += 2)
                expectEquals (events[(size_t) i + 2].sample - events[(size_t) i].sample, (int) (2.0 * step),
                              "la somma della coppia non e' invariante");

            // E lo swing a zero deve dare la griglia regolare: e' il caso degenere che deve
            // restare gratis, non un ramo in piu'.
            engine::Arpeggiator plain;
            plain.prepare (kSampleRate);

            const auto regular = onlyNoteOns (runArp (plain, baseConfig(), chordAt (0, { 60 }),
                                                      5 * (int) step, 128, false));

            for (int i = 0; i + 1 < (int) regular.size(); ++i)
                expectEquals (regular[(size_t) i + 1].sample - regular[(size_t) i].sample, (int) step);
        }

        beginTest ("a gate 50 % la nota dura meta' del passo");
        {
            const auto step = (int) (0.5 * kSamplesPerBeat);

            for (auto gate : { 0.25f, 0.5f, 0.75f, 1.0f })
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                auto cfg = baseConfig();
                cfg.gate01 = gate;

                const auto events = runArp (arp, cfg, chordAt (0, { 60 }), 3 * step, 128, false);

                // Il primo note-on e il primo note-off: la distanza e' il gate.
                int on = -1, off = -1;

                for (const auto& e : events)
                {
                    if (on < 0 && e.on) on = e.sample;
                    else if (on >= 0 && off < 0 && ! e.on) off = e.sample;
                }

                expect (on == 0 && off > 0, "gate " + juce::String (gate));
                expectEquals (off - on, (int) (gate * (float) step),
                              "durata sbagliata a gate " + juce::String (gate));
            }
        }

        beginTest ("con lo swing il gate resta una frazione del passo in cui cade");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.swing01 = 0.5f;
            cfg.gate01 = 0.5f;

            const auto step = 0.5 * kSamplesPerBeat;
            const auto events = runArp (arp, cfg, chordAt (0, { 60 }), 4 * (int) step, 128, false);

            std::vector<int> durations;
            int pendingOn = -1;

            for (const auto& e : events)
            {
                if (e.on) pendingOn = e.sample;
                else if (pendingOn >= 0) { durations.push_back (e.sample - pendingOn); pendingOn = -1; }
            }

            expect (durations.size() >= 2u);
            expectEquals (durations[0], (int) (step * 1.25 * 0.5), "il passo lungo");
            expectEquals (durations[1], (int) (step * 0.75 * 0.5), "il passo corto");
        }
    }
};

static ArpeggiatorTimingTests arpeggiatorTimingTests;

struct ArpeggiatorPatternTests final : juce::UnitTest
{
    ArpeggiatorPatternTests() : juce::UnitTest ("Arpeggiator pattern", "engine") {}

    void runTest() override
    {
        beginTest ("il doppio indice: tre tasti su una griglia di periodo quattro si ripetono dopo dodici passi");
        {
            // La griglia ha quattro passi accesi (0, 4, 8, 12), quindi il suo *contenuto* ha
            // periodo quattro; la sequenza di note ne ha tre. Il pattern completo si ripete al
            // minimo comune multiplo, dodici passi — non tre e non quattro. E' tutto il "doppio
            // indice" di Odin 2, e il contatore che avanza **anche sui passi spenti** e' cio' che
            // lo rende possibile.
            engine::ArpSnapshot snapshot;

            for (int i = 0; i < engine::kArpSteps; ++i)
                snapshot.steps[(size_t) i] = (i % 4 == 0) ? 0.8f : 0.0f;

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate32;                 // passi corti: 24 passi entrano in 1.5 s
            cfg.steps = &snapshot;

            const auto step = (int) (0.125 * kSamplesPerBeat); // 3000 campioni
            const auto events = onlyNoteOns (runArp (arp, cfg, chordAt (0, { 60, 64, 67 }),
                                                     24 * step, 128, false));

            // Sei note in ventiquattro passi: una ogni quattro.
            expectEquals ((int) events.size(), 6);

            const int expectedNotes[] = { 60, 64, 67, 60, 64, 67 };

            for (int i = 0; i < 6; ++i)
            {
                expectEquals (events[(size_t) i].sample, i * 4 * step);
                expectEquals (events[(size_t) i].note, expectedNotes[i]);
            }

            // Si ripete dopo dodici passi...
            for (int i = 0; i < 3; ++i)
            {
                expectEquals (events[(size_t) i + 3].note, events[(size_t) i].note);
                expectEquals (events[(size_t) i + 3].sample - events[(size_t) i].sample, 12 * step);
            }

            // ...e non dopo quattro: il passo 0 e il passo 4 suonano note diverse. (Dopo tre non
            // si ripete per costruzione: il passo 3 e' spento.)
            expect (events[0].note != events[1].note, "il pattern si ripeterebbe dopo quattro passi");
        }

        beginTest ("i quattro modi leggono la stessa lista di tasti");
        {
            const auto step = (int) (0.125 * kSamplesPerBeat);

            const auto notesFor = [&] (engine::ArpMode mode, int octaves)
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                auto cfg = baseConfig();
                cfg.rateRaw = kRate32;
                cfg.mode = mode;
                cfg.octaves = octaves;

                std::vector<int> notes;

                // I tasti arrivano in disordine: la lista dell'arp li tiene ordinati da se',
                // altrimenti "Up" salirebbe nell'ordine in cui la mano ha premuto.
                for (const auto& e : onlyNoteOns (runArp (arp, cfg, chordAt (0, { 67, 60, 64 }), 8 * step, 128, false)))
                    notes.push_back (e.note);

                return notes;
            };

            const auto up = notesFor (engine::ArpMode::up, 1);
            expectEquals ((int) up.size(), 8);
            expectEquals (up[0], 60); expectEquals (up[1], 64); expectEquals (up[2], 67);
            expectEquals (up[3], 60); expectEquals (up[4], 64); expectEquals (up[5], 67);

            const auto down = notesFor (engine::ArpMode::down, 1);
            expectEquals (down[0], 67); expectEquals (down[1], 64); expectEquals (down[2], 60);
            expectEquals (down[3], 67);

            // UpDn non ripete gli estremi: 60 64 67 64, lunga 2N-2.
            const auto updown = notesFor (engine::ArpMode::upDown, 1);
            expectEquals (updown[0], 60); expectEquals (updown[1], 64);
            expectEquals (updown[2], 67); expectEquals (updown[3], 64);
            expectEquals (updown[4], 60); expectEquals (updown[5], 64);

            // Due ottave: la lista si allunga di N, trasposta di dodici semitoni.
            const auto twoOctaves = notesFor (engine::ArpMode::up, 2);
            expectEquals (twoOctaves[0], 60); expectEquals (twoOctaves[1], 64);
            expectEquals (twoOctaves[2], 67); expectEquals (twoOctaves[3], 72);
            expectEquals (twoOctaves[4], 76); expectEquals (twoOctaves[5], 79);
            expectEquals (twoOctaves[6], 60);

            // Random: deterministico (xorshift con seme fisso) ma non monotono, e sempre dentro
            // l'accordo premuto. E' la prova che non c'e' nessuno shuffle e nessuna sorpresa.
            const auto random = notesFor (engine::ArpMode::random, 1);
            bool monotone = true;

            for (auto n : random)
                expect (n == 60 || n == 64 || n == 67, "Rand ha inventato una nota: " + juce::String (n));

            for (int i = 0; i + 1 < (int) random.size(); ++i)
                if (random[(size_t) i + 1] <= random[(size_t) i])
                    monotone = false;

            expect (! monotone, "Rand sta salendo come Up");
        }

        beginTest ("il livello dello step e' la velocity della nota, e zero la spegne");
        {
            engine::ArpSnapshot snapshot;
            snapshot.steps[0] = 1.0f;
            snapshot.steps[1] = 0.0f;
            snapshot.steps[2] = 0.5f;
            snapshot.steps[3] = 0.25f;

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate32;
            cfg.steps = &snapshot;

            const auto step = (int) (0.125 * kSamplesPerBeat);
            const auto events = onlyNoteOns (runArp (arp, cfg, chordAt (0, { 60 }), 4 * step, 128, false));

            expectEquals ((int) events.size(), 3, "lo step a zero deve restare muto");
            expectWithinAbsoluteError (events[0].velocity, 1.0f, 0.01f);
            expectEquals (events[0].sample, 0);
            expectWithinAbsoluteError (events[1].velocity, 0.5f, 0.01f);
            expectEquals (events[1].sample, 2 * step);
            expectWithinAbsoluteError (events[2].velocity, 0.25f, 0.01f);
            expectEquals (events[2].sample, 3 * step);
        }

        beginTest ("la sequenza si legge dal nodo ARP del ValueTree");
        {
            juce::ValueTree root { "PARAMS" };
            state::ensureChildren (root);

            engine::ArpSnapshot snapshot;
            engine::buildArpSnapshot (root.getChildWithName (state::ids::ARP), snapshot);

            // Il default di state::ensureChildren, letto passo per passo.
            const float expected[] = { 0.8f, 0.0f, 0.6f, 0.9f, 0.0f, 0.7f, 0.0f, 0.5f,
                                       0.8f, 0.0f, 0.6f, 0.0f, 0.9f, 0.4f, 0.0f, 0.7f };

            for (int i = 0; i < engine::kArpSteps; ++i)
                expectWithinAbsoluteError (snapshot.steps[(size_t) i], expected[i], 1.0e-6f);

            // Una stringa piu' corta: il resto resta a zero, non eredita dallo snapshot di prima.
            auto arpNode = root.getChildWithName (state::ids::ARP);
            arpNode.setProperty (state::ids::steps, "1,1", nullptr);
            engine::buildArpSnapshot (arpNode, snapshot);

            expectWithinAbsoluteError (snapshot.steps[0], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (snapshot.steps[1], 1.0f, 1.0e-6f);

            for (int i = 2; i < engine::kArpSteps; ++i)
                expectWithinAbsoluteError (snapshot.steps[(size_t) i], 0.0f, 1.0e-6f);

            // Valori fuori scala (preset scritto a mano, UI futura): limitati, non creduti.
            arpNode.setProperty (state::ids::steps, "5,-3,0.5", nullptr);
            engine::buildArpSnapshot (arpNode, snapshot);

            expectWithinAbsoluteError (snapshot.steps[0], 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (snapshot.steps[1], 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (snapshot.steps[2], 0.5f, 1.0e-6f);

            // Nodo assente: sequenza tutta a zero invece di una lettura di memoria a caso.
            engine::buildArpSnapshot ({}, snapshot);

            for (int i = 0; i < engine::kArpSteps; ++i)
                expectWithinAbsoluteError (snapshot.steps[(size_t) i], 0.0f, 1.0e-6f);
        }

        beginTest ("l'indice di griglia pubblicato e' quello del passo in corso");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            expectEquals (arp.currentStep(), 0);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate32;

            const auto step = (int) (0.125 * kSamplesPerBeat);
            const auto input = chordAt (0, { 60 });

            // Venti passi, uno per blocco: l'indice deve avvolgersi su sedici e ricominciare.
            int absolute = 0;
            size_t next = 0;
            std::vector<int> seen;

            for (int s = 0; s < 20; ++s)
            {
                juce::MidiBuffer midi;

                while (next < input.size() && input[next].sample < absolute + step)
                {
                    midi.addEvent (input[next].message, input[next].sample - absolute);
                    ++next;
                }

                engine::ArpTransport transport;
                transport.bpm = kBpm;
                arp.process (midi, step, cfg, transport);
                seen.push_back (arp.currentStep());
                absolute += step;
            }

            for (int s = 0; s < 20; ++s)
                expectEquals (seen[(size_t) s], s % engine::kArpSteps,
                              "indice di griglia al passo " + juce::String (s));
        }
    }
};

static ArpeggiatorPatternTests arpeggiatorPatternTests;

struct ArpeggiatorTransportTests final : juce::UnitTest
{
    ArpeggiatorTransportTests() : juce::UnitTest ("Arpeggiator transport", "engine") {}

    void runTest() override
    {
        beginTest ("con l'host fermo l'arp gira libero e riparte sotto il dito");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            const auto step = (int) (0.5 * kSamplesPerBeat);

            // Il tasto scende a meta' di un passo: con l'host fermo non c'e' nessuna griglia a cui
            // obbedire, quindi la prima nota deve suonare **li'**, non al prossimo confine.
            const auto events = onlyNoteOns (runArp (arp, baseConfig(), chordAt (5000, { 60 }),
                                                     3 * step, 128, false));

            expect (events.size() >= 2u);
            expectEquals (events[0].sample, 5000, "la prima nota non e' sotto il dito");
            expectEquals (events[1].sample, 5000 + step);
        }

        beginTest ("quando il transport parte l'arp si aggancia al PPQ");
        {
            const auto step = (int) (0.5 * kSamplesPerBeat);

            // Transport in moto da PPQ 0: i confini sono i multipli di mezzo quarto della
            // timeline, e il primo cade a campione zero.
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                const auto events = onlyNoteOns (runArp (arp, baseConfig(), chordAt (0, { 60 }),
                                                         4 * step, 128, true, 0.0));

                expectEquals ((int) events.size(), 4);

                for (int i = 0; i < 4; ++i)
                    expectEquals (events[(size_t) i].sample, i * step);
            }

            // Transport agganciato **a meta' di un passo** (PPQ 0.3): il passo in corso non e'
            // ancora stato emesso, quindi parte subito, e il confine successivo e' quello della
            // griglia dell'host (0.5 quarti), non 12000 campioni piu' in la'.
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                const auto events = onlyNoteOns (runArp (arp, baseConfig(), chordAt (0, { 60 }),
                                                         3 * step, 128, true, 0.3));

                expect (events.size() >= 3u);
                expectEquals (events[0].sample, 0, "il passo in corso deve partire subito");
                expectEquals (events[1].sample, (int) (0.2 * kSamplesPerBeat),
                              "il secondo confine non e' quello del PPQ");
                expectEquals (events[2].sample, (int) (0.7 * kSamplesPerBeat));
            }

            // E il riancoraggio non deve **riemettere** il passo in corso a ogni blocco: con
            // blocchi da 128 campioni ci sono 93 blocchi dentro un passo, e senza il dedup su
            // lastFiredStep_ ne uscirebbero 93 note invece di una.
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                const auto events = onlyNoteOns (runArp (arp, baseConfig(), chordAt (0, { 60 }),
                                                         step, 128, true, 0.0));

                expectEquals ((int) events.size(), 1, "il riancoraggio sta riemettendo lo stesso passo");
            }
        }

        beginTest ("il PPQ comanda anche quando il blocco dell'host non e' una potenza di due");
        {
            const auto step = (int) (0.5 * kSamplesPerBeat);

            for (auto blockSize : { 37, 128, 441, 1024 })
            {
                engine::Arpeggiator arp;
                arp.prepare (kSampleRate);

                const auto events = onlyNoteOns (runArp (arp, baseConfig(), chordAt (0, { 60 }),
                                                         4 * step, blockSize, true, 0.0));

                expectEquals ((int) events.size(), 4, "blocchi da " + juce::String (blockSize));

                for (int i = 0; i < (int) events.size(); ++i)
                    expectEquals (events[(size_t) i].sample, i * step,
                                  "blocchi da " + juce::String (blockSize));
            }
        }

        beginTest ("un salto del cursore riaggancia invece di far derivare");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            const auto cfg = baseConfig();
            constexpr auto block = 512;

            std::vector<Emitted> events;
            int absolute = 0;
            bool pressed = false;

            // Sei blocchi da PPQ 0, poi il cursore salta a PPQ 4 (l'host ha ciclato): il passo in
            // cui atterra non e' mai stato emesso, quindi parte subito.
            for (int b = 0; b < 12; ++b)
            {
                juce::MidiBuffer midi;

                if (! pressed)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                    pressed = true;
                }

                engine::ArpTransport transport;
                transport.bpm = kBpm;
                transport.isPlaying = true;
                transport.ppqPosition = b < 6 ? (double) (b * block) / kSamplesPerBeat
                                              : 4.0 + (double) ((b - 6) * block) / kSamplesPerBeat;

                arp.process (midi, block, cfg, transport);

                for (const auto metadata : midi)
                    if (metadata.getMessage().isNoteOn())
                        events.push_back ({ absolute + metadata.samplePosition, true,
                                            metadata.getMessage().getNoteNumber(), 0.0f });

                absolute += block;
            }

            // Il salto cade dentro il settimo blocco: li' deve nascere una nota a offset zero.
            const auto atJump = std::count_if (events.begin(), events.end(),
                                               [] (const Emitted& e) { return e.sample == 6 * block; });

            expectEquals ((int) atJump, 1, "il salto del cursore non ha riagganciato l'arp");
            expect (events.size() >= 2u);
        }

        beginTest ("senza PPQ l'arp resta nel regime libero anche a 44100 Hz");
        {
            // Nessuna delle costanti del modulo e' in campioni: il passo deve venire dal tempo.
            engine::Arpeggiator arp;
            arp.prepare (44100.0);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate8;

            std::vector<Emitted> events;
            int absolute = 0;
            bool pressed = false;

            for (int b = 0; b < 60; ++b)
            {
                juce::MidiBuffer midi;

                if (! pressed)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                    pressed = true;
                }

                engine::ArpTransport transport;
                transport.bpm = kBpm;
                arp.process (midi, 512, cfg, transport);

                for (const auto metadata : midi)
                    if (metadata.getMessage().isNoteOn())
                        events.push_back ({ absolute + metadata.samplePosition, true, 0, 0.0f });

                absolute += 512;
            }

            // 0.25 s a 44100 Hz sono 11025 campioni.
            expect (events.size() >= 2u);
            expectEquals (events[1].sample - events[0].sample, 11025);
        }
    }
};

static ArpeggiatorTransportTests arpeggiatorTransportTests;

struct ArpeggiatorHangingNotesTests final : juce::UnitTest
{
    ArpeggiatorHangingNotesTests() : juce::UnitTest ("Arpeggiator hanging notes", "engine") {}

    /** Ogni note-on deve avere il suo note-off, e nessun note-off deve avanzare. */
    void expectBalanced (const std::vector<Emitted>& events, const juce::String& what)
    {
        std::map<int, int> open;

        for (const auto& e : events)
        {
            if (e.on)
            {
                ++open[e.note];
                expect (open[e.note] <= 1, what + ": due note-on di seguito sulla nota "
                                               + juce::String (e.note));
            }
            else
            {
                --open[e.note];
                expect (open[e.note] >= 0, what + ": note-off senza note-on sulla nota "
                                               + juce::String (e.note));
            }
        }

        for (const auto& [note, count] : open)
            expectEquals (count, 0, what + ": la nota " + juce::String (note) + " e' rimasta appesa");
    }

    void runTest() override
    {
        beginTest ("cambiare accordo a meta' pattern e poi rilasciare tutto non lascia niente appeso");
        {
            const auto step = (int) (0.25 * kSamplesPerBeat); // 1/16: 6000 campioni

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate16;
            cfg.gate01 = 0.9f;
            cfg.octaves = 2;   // le note **emesse** non sono quelle premute: e' il caso che conta

            std::vector<Input> input;
            append (input, chordAt (0, { 60, 64, 67 }));

            // A meta' pattern, e non su un confine di passo: l'accordo cambia mentre delle note
            // emesse dal vecchio accordo stanno ancora suonando.
            append (input, chordAt (3 * step + 1234, { 60, 64, 67 }, false));
            append (input, chordAt (3 * step + 1234, { 62, 65, 69 }));

            append (input, chordAt (9 * step + 777, { 62, 65, 69 }, false));

            std::sort (input.begin(), input.end(),
                       [] (const Input& a, const Input& b) { return a.sample < b.sample; });

            const auto events = runArp (arp, cfg, input, 16 * step, 128, false);

            expectBalanced (events, "cambio d'accordo");

            // E dopo l'ultimo rilascio non deve uscire piu' niente.
            const auto lastOn = std::find_if (events.rbegin(), events.rend(),
                                              [] (const Emitted& e) { return e.on; });

            expect (lastOn != events.rend());
            expect (lastOn->sample < 11 * step, "l'arp ha continuato a suonare dopo il rilascio");
        }

        beginTest ("e nel motore vero non resta nessuna voce che suona");
        {
            // Lo stesso scenario, ma con le note consegnate a un VoiceManager: e' la verifica che
            // il criterio di accettazione chiede, countSounding() a zero.
            dsp::WavetableStore store;
            store.setActive (0);

            const auto step = (int) (0.25 * kSamplesPerBeat);

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate16;
            cfg.gate01 = 0.9f;
            cfg.octaves = 2;

            std::vector<Input> input;
            append (input, chordAt (0, { 60, 64, 67 }));
            append (input, chordAt (3 * step + 1234, { 60, 64, 67 }, false));
            append (input, chordAt (3 * step + 1234, { 62, 65, 69 }));
            append (input, chordAt (9 * step + 777, { 62, 65, 69 }, false));

            std::sort (input.begin(), input.end(),
                       [] (const Input& a, const Input& b) { return a.sample < b.sample; });

            const auto events = runArp (arp, cfg, input, 16 * step, 128, false);

            engine::VoiceManager voices;
            voices.prepare (kSampleRate);
            voices.setWavetable (store.active());
            voices.setParams (plainPatch());

            juce::AudioBuffer<float> scratch (2, engine::SynthEngine::kControlBlockSamples);
            int rendered = 0;

            const auto renderTo = [&] (int target)
            {
                while (rendered < target)
                {
                    const auto slice = std::min (engine::SynthEngine::kControlBlockSamples, target - rendered);
                    scratch.clear();
                    voices.render (scratch.getWritePointer (0), scratch.getWritePointer (1), slice);
                    rendered += slice;
                }
            };

            for (const auto& e : events)
            {
                renderTo (e.sample);

                if (e.on)
                    voices.noteOn (e.note, e.velocity);
                else
                    voices.noteOff (e.note);
            }

            // Un secondo abbondante dopo l'ultimo evento: il release dura 20 ms, la coda di un
            // eventuale furto 8. Se qualcosa suona ancora qui, e' appeso.
            renderTo (rendered + (int) kSampleRate);

            expectEquals (voices.countSounding(), 0, "una voce e' rimasta appesa");
        }

        beginTest ("spegnere l'arp a meta' pattern chiude le note che aveva aperto");
        {
            const auto step = (int) (0.25 * kSamplesPerBeat);

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate16;
            cfg.gate01 = 1.0f;   // una nota e' sempre in suono: spegnendo, c'e' sempre qualcosa da chiudere

            std::vector<Emitted> events;
            int absolute = 0;
            const auto block = 128;

            for (int b = 0; b * block < 10 * step; ++b)
            {
                juce::MidiBuffer midi;

                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

                // A meta' della passata l'interruttore si spegne, con i tasti ancora premuti.
                auto blockCfg = cfg;
                blockCfg.on = absolute < 5 * step;

                engine::ArpTransport transport;
                transport.bpm = kBpm;
                arp.process (midi, block, blockCfg, transport);

                for (const auto metadata : midi)
                {
                    const auto message = metadata.getMessage();

                    if (message.isNoteOn() || message.isNoteOff())
                        events.push_back ({ absolute + metadata.samplePosition, message.isNoteOn(),
                                            message.getNoteNumber(), message.getFloatVelocity() });
                }

                absolute += block;
            }

            expectBalanced (events, "arp spento a caldo");
        }

        beginTest ("un all-notes-off passa intatto e chiude l'arpeggio");
        {
            const auto step = (int) (0.25 * kSamplesPerBeat);

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate16;

            std::vector<Input> input;
            append (input, chordAt (0, { 60, 64 }));
            input.push_back ({ 4 * step + 100, juce::MidiMessage::allNotesOff (1) });

            int absolute = 0;
            size_t next = 0;
            bool sawAllNotesOff = false;
            std::vector<Emitted> events;

            while (absolute < 10 * step)
            {
                juce::MidiBuffer midi;

                while (next < input.size() && input[next].sample < absolute + 128)
                {
                    midi.addEvent (input[next].message, input[next].sample - absolute);
                    ++next;
                }

                engine::ArpTransport transport;
                transport.bpm = kBpm;
                arp.process (midi, 128, cfg, transport);

                for (const auto metadata : midi)
                {
                    const auto message = metadata.getMessage();

                    if (message.isAllNotesOff())
                    {
                        sawAllNotesOff = true;
                        expectEquals (absolute + metadata.samplePosition, 4 * step + 100,
                                      "l'all-notes-off e' stato spostato");
                    }
                    else if (message.isNoteOn() || message.isNoteOff())
                    {
                        events.push_back ({ absolute + metadata.samplePosition, message.isNoteOn(),
                                            message.getNoteNumber(), message.getFloatVelocity() });
                    }
                }

                absolute += 128;
            }

            expect (sawAllNotesOff, "l'all-notes-off non e' arrivato alle voci");
            expectBalanced (events, "all-notes-off");

            const auto after = std::count_if (events.begin(), events.end(),
                                              [&] (const Emitted& e) { return e.on && e.sample > 4 * step + 100; });

            expectEquals ((int) after, 0, "l'arp ha continuato dopo l'all-notes-off");
        }

        beginTest ("con gate pieno e nota ripetuta il note-off precede il note-on");
        {
            // E' il bug di Odin: al confine di passo il note-on della nota nuova arrivava prima
            // del note-off di quella vecchia, e con la stessa altezza spegneva la voce appena
            // avviata. Qui si guarda l'ordine dentro il buffer, non solo il bilancio.
            const auto step = (int) (0.25 * kSamplesPerBeat);

            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            auto cfg = baseConfig();
            cfg.rateRaw = kRate16;
            cfg.gate01 = 1.0f;

            // Il tasto si rilascia prima della fine della passata: con gate pieno la nota in
            // suono finisce esattamente dove comincerebbe la successiva, quindi senza rilascio il
            // bilancio resterebbe aperto per costruzione e non proverebbe niente.
            std::vector<Input> input;
            append (input, chordAt (0, { 60 }));
            append (input, chordAt (3 * step + 10, { 60 }, false));

            const auto events = runArp (arp, cfg, input, 6 * step, 128, false);

            expectBalanced (events, "gate pieno, nota ripetuta");

            // Quattro passi, quattro note: il rilascio cade dentro il quarto.
            expectEquals ((int) onlyNoteOns (events).size(), 4);

            for (int i = 0; i + 1 < (int) events.size(); ++i)
                if (events[(size_t) i].sample == events[(size_t) i + 1].sample)
                    expect (! events[(size_t) i].on || events[(size_t) i + 1].on,
                            "un note-on precede il note-off della stessa nota allo stesso campione");
        }
    }
};

static ArpeggiatorHangingNotesTests arpeggiatorHangingNotesTests;

struct ArpeggiatorBypassTests final : juce::UnitTest
{
    ArpeggiatorBypassTests() : juce::UnitTest ("Arpeggiator bypass", "engine") {}

    void runTest() override
    {
        beginTest ("con arpOn falso il MidiBuffer non viene toccato");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            engine::ArpConfig cfg = baseConfig();
            cfg.on = false;

            for (int block = 0; block < 8; ++block)
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 3);
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 17);
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 99);

                const auto before = dumpMidi (midi);

                engine::ArpTransport transport;
                transport.bpm = kBpm;
                arp.process (midi, 128, cfg, transport);

                expect (dumpMidi (midi) == before, "l'arp spento ha toccato il buffer");
            }
        }

        beginTest ("e nel motore l'uscita e' bit per bit quella di prima che l'arp esistesse");
        {
            // Con l'arp spento nessuno dei suoi parametri deve poter raggiungere il segnale: si
            // rende la stessa passata due volte, una con la ArpConfig di default e una con ogni
            // manopola dell'arp spostata a un valore diverso, e le due impronte devono coincidere.
            dsp::WavetableStore store;
            store.setActive (0);

            const auto render = [&store] (const engine::ArpConfig& arpConfig, const engine::ArpSnapshot* steps)
            {
                engine::SynthEngine synth;
                engine::EngineSpec spec;
                spec.sampleRate = kSampleRate;
                spec.maximumBlockSize = 128;
                spec.numChannels = 2;
                synth.prepare (spec);
                synth.setWavetable (store.active());

                auto params = engine::EngineParams {};
                params.oscOn = true;
                params.level = 1.0f;
                params.filterOn = true;
                params.cutoffHz = 6000.0f;
                params.attackSeconds = 0.002f;
                params.decaySeconds = 0.05f;
                params.sustain = 0.7f;
                params.releaseSeconds = 0.1f;
                params.arp = arpConfig;
                params.arp.steps = steps;
                params.ppqPosition = 3.25;
                params.transportPlaying = true;

                synth.setParams (params);
                synth.setMasterGainLinear (0.8f);

                struct Event { int block; int offset; bool on; int note; };
                static constexpr Event events[] = {
                    { 0, 17, true, 48 }, { 2, 5, true, 55 }, { 4, 71, true, 60 },
                    { 7, 33, false, 48 }, { 11, 90, false, 60 }, { 13, 45, false, 55 },
                };

                Checksum checksum;
                juce::AudioBuffer<float> buffer (2, 128);

                for (int block = 0; block < 30; ++block)
                {
                    juce::MidiBuffer midi;

                    for (const auto& e : events)
                        if (e.block == block)
                            midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, 0.9f)
                                                : juce::MidiMessage::noteOff (1, e.note),
                                           e.offset);

                    buffer.clear();
                    synth.process (buffer, midi);

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        for (int i = 0; i < buffer.getNumSamples(); ++i)
                            checksum.add (buffer.getSample (ch, i));
                }

                return checksum.value;
            };

            engine::ArpSnapshot snapshot;

            for (int i = 0; i < engine::kArpSteps; ++i)
                snapshot.steps[(size_t) i] = 0.5f + 0.02f * (float) i;

            engine::ArpConfig off;               // tutti i default, `on` falso
            engine::ArpConfig loaded = baseConfig();
            loaded.on = false;                   // ...ma ogni altra manopola spostata
            loaded.mode = engine::ArpMode::upDown;
            loaded.rateRaw = kRate32;
            loaded.gate01 = 0.93f;
            loaded.octaves = 4;
            loaded.swing01 = 0.7f;

            const auto plain = render (off, nullptr);

            expectEquals (juce::String::toHexString ((juce::int64) render (loaded, &snapshot)),
                          juce::String::toHexString ((juce::int64) plain),
                          "un parametro dell'arp spento e' arrivato al segnale");

            // E acceso, invece, deve cambiare qualcosa: altrimenti il test sopra proverebbe solo
            // che l'arp non e' cablato.
            auto on = loaded;
            on.on = true;

            expect (render (on, &snapshot) != plain, "l'arp acceso non cambia niente");
        }

        beginTest ("i messaggi che non sono note passano intatti anche con l'arp acceso");
        {
            engine::Arpeggiator arp;
            arp.prepare (kSampleRate);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 11);
            midi.addEvent (juce::MidiMessage::pitchWheel (1, 9000), 40);

            engine::ArpTransport transport;
            transport.bpm = kBpm;
            arp.process (midi, 128, baseConfig(), transport);

            int seen = 0;

            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();

                if (message.isController())
                {
                    ++seen;
                    expectEquals (metadata.samplePosition, 11);
                    expectEquals (message.getControllerValue(), 64);
                }
                else if (message.isPitchWheel())
                {
                    ++seen;
                    expectEquals (metadata.samplePosition, 40);
                    expectEquals (message.getPitchWheelValue(), 9000);
                }
            }

            expectEquals (seen, 2, "l'arp ha mangiato un messaggio che non e' una nota");
        }
    }
};

static ArpeggiatorBypassTests arpeggiatorBypassTests;
