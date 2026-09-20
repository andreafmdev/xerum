#include "EngineTestHelpers.h"

#include "dsp/MipTable.h"
#include "dsp/Saturation.h"
#include "dsp/PlateReverb.h"
#include "dsp/WavetableBlob.h"
#include "dsp/WavetableStore.h"
#include "dsp/WavetableOscillator.h"
#include "engine/EngineParams.h"
#include "engine/ModMatrix.h"
#include "engine/SynthEngine.h"
#include "engine/SynthVoice.h"
#include "engine/VoiceManager.h"
#include "engine/ParamCollect.h"
#include "parameters/ParameterTable.h"
#include "parameters/PresetTable.h"
#include "parameters/PresetValue.h"
#include "state/StateToEngine.h"
#include "state/StateTree.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace
{
using harness::defaultParams;
using harness::measureFundamentalHz;
using harness::prepareEngine;
using harness::renderPeak;
using harness::renderRms;
using harness::denormaliseLinear;
using harness::ClipperProbe;
using harness::measureAtClipper;
using harness::rawFromNormalised;
using harness::PresetPatch;
using harness::patchFromPreset;
} // namespace

/**
 * Task 9: il mod matrix a pieno carico.
 *
 * Non e' un test di correttezza timbrica — con trentadue route addosso non esiste un valore
 * "giusto" da confrontare. E' la rete di sicurezza real-time: quando ogni target e' tirato da
 * piu' sorgenti insieme e i parametri si muovono sotto, ogni campione deve restare finito e
 * dentro il fondo scala, e nessuna voce deve restare appesa.
 *
 * Piu' severo del test degli estremi (EngineRobustnessTests): li' i parametri sono fissi per
 * l'intera variante, qui cambiano *fra un blocco e l'altro* mentre la modulazione e' attiva, e
 * il contratto verificato non e' solo il picco ma ogni singolo campione.
 */
struct ModulationStressTests final : juce::UnitTest
{
    ModulationStressTests() : juce::UnitTest ("mod matrix a pieno carico", "engine") {}

    /**
     * Riempie lo snapshot fino al tetto di kMaxRoutes (32). Le prime 4 x 7 = 28 route sono
     * tutte le combinazioni sorgente x target; le quattro che avanzano ricominciano da capo,
     * quindi sono route *doppie* — stessa sorgente, stesso target, stesso segno. Non e'
     * riempitivo: e' il caso che mette alla prova la somma e il clamp, non la sola capienza
     * dell'array.
     *
     * I depth alternano +1 e -1 e il passo fra due sorgenti (7) e' dispari, quindi ogni target
     * riceve due route positive e due negative da sorgenti diverse: le modulazioni si
     * contendono il parametro invece di spingerlo tutte dalla stessa parte.
     */
    /** I sette target che spostano guadagno e timbro del percorso classico. `warp` (l'ottavo) e'
        sync: cambia la forma d'onda, non il livello, e ha la sua suite (WarpModulationTests);
        tenerlo fuori da qui lascia intatta la taratura 1.4..2.512 del picco al clipper, misurata
        su questo esatto riempimento, e conserva il passo dispari da cui dipende l'alternanza dei
        segni descritta sopra. */
    static constexpr int kGainTargets = 7;
    static_assert (kGainTargets <= engine::kNumModTargets, "kGainTargets non puo' superare i target del motore");

    static engine::ModSnapshot fullSnapshot() noexcept
    {
        engine::ModSnapshot mods;

        for (int i = 0; i < engine::ModSnapshot::kMaxRoutes; ++i)
        {
            const auto src = (engine::ModSource) ((i / kGainTargets) % (int) engine::ModSource::count);
            mods.routes[i] = { src, i % kGainTargets, (i % 2 == 0) ? 1.0f : -1.0f };
        }

        mods.count = engine::ModSnapshot::kMaxRoutes;
        return mods;
    }

    struct Config
    {
        double sampleRate { 48000.0 };
        int numNotes { 16 };
        bool lfoSync { false };
        bool lfoRetrig { true };
        dsp::StateVariableFilter::Type filterType { dsp::StateVariableFilter::Type::lowPass };
        int filterStages { 2 };
        bool republishEachBlock { false };
        int unisonVoices { 1 };
        float detuneCents { 0.0f };

        /** Lo stadio FX acceso, con il chorus a tutti gli estremi: mix e feedback a fondo corsa.
            E' uno stadio di guadagno dopo il filtro, quindi appartiene a questa passata quanto
            il filtro stesso — vedi docs/architecture.md, "anything that ... adds a gain stage
            after the filter". */
        bool chorus { false };
        float chorusRateHz { 5.1f };
        float chorusDepth01 { 1.0f };
        float chorusMix01 { 1.0f };
        float chorusFeedback01 { 1.0f };

        /** Il riverbero, con gli stessi criteri: acceso significa a fondo corsa ovunque. */
        bool reverb { false };
        float reverbSize01 { 1.0f };
        float reverbDecay01 { 1.0f };
        float reverbDamp01 { 0.0f };
        float reverbPredelaySeconds { 0.0f };
        float reverbMix01 { 1.0f };

        /**
         * Fattore applicato al gain master, cioe' all'**unico** stadio che sta fra lo stadio FX
         * e il soft clipper.
         *
         * Serve a misurare il picco presentato al clipper, che e' il numero di cui parla
         * docs/architecture.md, e non si puo' ottenere invertendo il clipper: sopra soglia
         * l'inversa e' malcondizionata da far spavento — 0.9983 in uscita risale a 2.36
         * all'ingresso e 0.9990 a 3.40, cioe' tre decibel e mezzo di differenza fra due numeri
         * che differiscono alla quarta cifra. Abbassando il gain finche' il clipper resta
         * spento, invece, il picco d'uscita **e'** il picco d'ingresso diviso per il fattore, e
         * la misura e' esatta.
         *
         * Tutto cio' che precede e' lineare nel gain master (le voci e lo stadio FX stanno prima
         * di lui), quindi il fattore non cambia una virgola di cio' che si sta misurando.
         */
        float masterGainScale { 1.0f };
    };

    static size_t targetOf (params::ParamSlot slot) noexcept
    {
        return (size_t) engine::modTargetIndexFor (slot);
    }

    /**
     * Una passata completa: matrix pieno, note tenute, parametri che si muovono a ogni blocco.
     * Controlla ogni campione qui dentro (finito e |x| <= 1), scarica la coda dopo un
     * all-notes-off e ritorna il picco.
     */
    float sweep (dsp::WavetableStore& store, const Config& cfg, int numBlocks, const juce::String& what)
    {
        engine::SynthEngine synth;

        engine::EngineSpec spec;
        spec.sampleRate = cfg.sampleRate;
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());
        synth.setMods (fullSnapshot());

        auto p = defaultParams();
        p.filterOn = true;
        p.filterType = cfg.filterType;
        p.filterStages = cfg.filterStages;
        p.keyTrack = 0.5f;
        p.velocityAmount = 1.0f;
        p.attackSeconds = 0.005f;
        p.decaySeconds = 0.2f;
        p.sustain = 0.9f;
        p.releaseSeconds = 0.08f;
        p.lfoRateRaw = 1.0f;     // il massimo della mappa: la sorgente lfo si muove a ogni blocco
        p.lfoSync = cfg.lfoSync;
        p.lfoRetrig = cfg.lfoRetrig;
        p.lfoFadeSeconds = 0.0f;
        p.bpm = 200.0f;          // sync a tempo alto: divisioni brevi, quindi LFO veloce
        p.unisonVoices = cfg.unisonVoices;
        p.detuneCents = cfg.detuneCents;
        p.chorusOn = cfg.chorus;
        p.chorusRateHz = cfg.chorusRateHz;
        p.chorusDepth01 = cfg.chorusDepth01;
        p.chorusMix01 = cfg.chorusMix01;
        p.chorusFeedback01 = cfg.chorusFeedback01;

        p.reverbOn = cfg.reverb;
        p.reverbSize01 = cfg.reverbSize01;
        p.reverbDecay01 = cfg.reverbDecay01;
        p.reverbDamp01 = cfg.reverbDamp01;
        p.reverbPredelaySeconds = cfg.reverbPredelaySeconds;
        p.reverbMix01 = cfg.reverbMix01;
        p.modBase.fill (0.5f);

        juce::MidiBuffer midi;

        for (int i = 0; i < cfg.numNotes; ++i)
            midi.addEvent (juce::MidiMessage::noteOn (1, 24 + (i * 5) % 96, 0.3f + 0.7f * (float) (i % 3) * 0.5f), 0);

        float peak = 0.0f;
        int badBlock = -1;
        int badSample = -1;
        float badValue = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            // Triangolo 0..1 di periodo `period`. I periodi sono primi fra loro, cosi' le sette
            // basi non tornano mai in fase e la passata attraversa molte combinazioni diverse.
            const auto ramp = [b] (int period) noexcept
            {
                const auto phase = (float) (b % period) / (float) period;
                return phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f;
            };

            // Gradino: 0 o 1, niente vie di mezzo. res e drive non passano da nessuno
            // SmoothedValue, quindi il salto arriva intero al filtro e alla saturazione — che
            // e' esattamente il caso da verificare, non da evitare.
            const auto step = [b] (int period) noexcept { return ((b / period) % 2) == 0 ? 0.0f : 1.0f; };

            p.modBase[targetOf (params::ParamSlot::cutoff)] = ramp (7);
            p.modBase[targetOf (params::ParamSlot::wtpos)] = ramp (11);
            p.modBase[targetOf (params::ParamSlot::level)] = 0.5f + 0.5f * ramp (13);
            p.modBase[targetOf (params::ParamSlot::pan)] = ramp (17);
            p.modBase[targetOf (params::ParamSlot::fine)] = ramp (19);
            p.modBase[targetOf (params::ParamSlot::res)] = step (5);
            p.modBase[targetOf (params::ParamSlot::drive)] = step (3);

            // Gli stessi valori anche nei campi denormalizzati: se un target smettesse di
            // passare dal percorso modulato (modMask_ spento) il test deve restare severo lo
            // stesso, invece di ritrovarsi i default innocui di defaultParams().
            p.cutoffHz = params::cutoffHzFromRaw (p.modBase[targetOf (params::ParamSlot::cutoff)]);
            p.framePosition = params::framePositionFromRaw (p.modBase[targetOf (params::ParamSlot::wtpos)]);
            p.level = params::levelGainFromRaw (p.modBase[targetOf (params::ParamSlot::level)]);
            p.pan = params::panFromRaw (p.modBase[targetOf (params::ParamSlot::pan)]);
            p.fineCents = params::fineCentsFromRaw (p.modBase[targetOf (params::ParamSlot::fine)]);
            p.resonanceQ = params::resonanceQFromRaw (p.modBase[targetOf (params::ParamSlot::res)]);
            p.driveGain = params::driveGainFromRaw (p.modBase[targetOf (params::ParamSlot::drive)]);

            p.lfoShapeIndex = (b / 9) % 5; // tutte e cinque le forme, sample & hold compresa

            synth.setParams (p);

            // Anche il master gain si muove: il ramp fra previousMasterGain_ e masterGain_ e'
            // l'ultimo stadio prima del soft clipper.
            synth.setMasterGainLinear (cfg.masterGainScale * ((b % 2) == 0 ? 1.0f : 0.6f));

            // Ripubblicare a ogni blocco fa girare l'anello di quattro slot: un giro completo
            // ogni quattro blocchi, con il lettore che nel frattempo sta usando uno di essi.
            if (cfg.republishEachBlock)
                synth.setMods (fullSnapshot());

            // Mod wheel a scatti: e' la quarta sorgente, e a differenza delle altre tre arriva
            // da un evento MIDI a meta' buffer.
            if ((b % 12) == 0)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, (b % 24) == 0 ? 127 : 0), 64);

            // Una nota che riparte a meta' blocco: costringe process() a spezzare il render in
            // due fette, e applyModulation() a rigirare su una voce che sta gia' suonando.
            if ((b % 16) == 8)
            {
                const int note = 24 + (b * 7) % 96;
                midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.2f + 0.8f * ramp (3)), 100);
            }

            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, midi);
            midi.clear();

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);

                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    const auto value = data[i];

                    // Una sola expect() alla fine invece di una per campione: un NaN che
                    // dura mezzo secondo produrrebbe altrimenti migliaia di righe di log
                    // identiche e nasconderebbe il primo punto in cui e' comparso.
                    if (badBlock < 0 && (! std::isfinite (value) || std::abs (value) > 1.0f))
                    {
                        badBlock = b;
                        badSample = i;
                        badValue = value;
                    }

                    peak = juce::jmax (peak, std::abs (value));
                }
            }
        }

        expect (badBlock < 0,
                what + ": campione fuori contratto al blocco " + juce::String (badBlock) + ", indice "
                    + juce::String (badSample) + ", valore " + juce::String (badValue));

        // Il test e' privo di valore se il motore stesse restituendo silenzio: qualunque
        // soglia superiore la passerebbe.
        expect (peak > 0.05f, what + ": uscita troppo bassa (" + juce::String (peak)
                                  + "): la passata non sta verificando niente");

        // Nessuna voce appesa, nemmeno con il matrix pieno: una route con depth negativo su
        // `level` puo' portare il parametro a zero, ma e' l'inviluppo — non il livello — a
        // decidere quando la voce si libera.
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        {
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            synth.process (buffer, off);
        }

        // release e' 80 ms: si scarta il decadimento (che sta ancora suonando) prima di
        // misurare il silenzio vero. Il numero di blocchi segue il sample rate.
        //
        // Con lo stadio FX acceso si aspetta di piu': gli effetti hanno una coda loro, e
        // confonderla con una voce appesa vorrebbe dire trasformare un comportamento voluto in un
        // fallimento. Un secondo basta al chorus (feedback a fondo corsa piu' mezzo secondo di
        // ringout); il riverbero a size e decay a fondo corsa ne chiede dodici, che e' la coda
        // dichiarata all'host piu' un margine — e che questo numero sia cosi' grande e' proprio
        // cio' che getTailLengthSeconds() e' andato a dire.
        const auto tailSeconds = cfg.reverb ? 12.08 : (cfg.chorus ? 1.08 : 0.08);
        const int releaseBlocks = (int) std::ceil (tailSeconds * cfg.sampleRate / 128.0) + 10;
        renderPeak (synth, releaseBlocks, *this);
        const auto tail = renderPeak (synth, 20, *this);
        expect (tail < 1.0e-4f, what + ": voce appesa dopo il release, picco " + juce::String (tail));

        return peak;
    }

    /**
     * Passata casuale ma riproducibile: matrix di lunghezza e contenuto qualunque, parametri
     * ridisegnati a ogni blocco, blocchi di lunghezza variabile, eventi MIDI in posizioni
     * arbitrarie dentro il buffer. Stesso contratto delle passate guidate — ogni campione
     * finito e dentro il fondo scala — ma percorso in un modo che nessuno ha scelto a mano.
     */
    void fuzz (dsp::WavetableStore& store, int seed, int numBlocks)
    {
        juce::Random rng (seed);

        engine::SynthEngine synth;
        engine::EngineSpec spec;
        spec.sampleRate = (double) juce::jmap (rng.nextFloat(), 44100.0f, 96000.0f);
        spec.maximumBlockSize = 128;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());

        auto p = defaultParams();
        float peak = 0.0f;
        int badBlock = -1;
        int badSample = -1;
        float badValue = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            // Una ripubblicazione ogni quattro blocchi in media: abbastanza frequente da far
            // girare l'anello, abbastanza rara da lasciare che uno snapshot resti in uso per
            // piu' blocchi di fila.
            if (rng.nextInt (4) == 0)
            {
                engine::ModSnapshot mods;
                mods.count = rng.nextInt (engine::ModSnapshot::kMaxRoutes + 1);

                for (int i = 0; i < mods.count; ++i)
                    mods.routes[i] = { (engine::ModSource) rng.nextInt ((int) engine::ModSource::count),
                                       rng.nextInt (engine::kNumModTargets),
                                       rng.nextFloat() * 2.0f - 1.0f };

                synth.setMods (mods);
            }

            for (auto& base : p.modBase)
                base = rng.nextFloat();

            p.cutoffHz = params::cutoffHzFromRaw (p.modBase[targetOf (params::ParamSlot::cutoff)]);
            p.resonanceQ = params::resonanceQFromRaw (p.modBase[targetOf (params::ParamSlot::res)]);
            p.framePosition = params::framePositionFromRaw (p.modBase[targetOf (params::ParamSlot::wtpos)]);
            p.level = params::levelGainFromRaw (p.modBase[targetOf (params::ParamSlot::level)]);
            p.pan = params::panFromRaw (p.modBase[targetOf (params::ParamSlot::pan)]);
            p.fineCents = params::fineCentsFromRaw (p.modBase[targetOf (params::ParamSlot::fine)]);
            p.driveGain = params::driveGainFromRaw (p.modBase[targetOf (params::ParamSlot::drive)]);

            p.oscOn = rng.nextInt (16) != 0;
            p.filterOn = rng.nextInt (8) != 0;
            p.filterType = (dsp::StateVariableFilter::Type) rng.nextInt (3);
            p.filterStages = 1 + rng.nextInt (2);
            p.keyTrack = rng.nextFloat();
            p.octave = rng.nextInt (7) - 3;
            p.semitones = rng.nextInt (25) - 12;
            p.attackSeconds = rng.nextFloat() * 0.05f;
            p.decaySeconds = rng.nextFloat() * 0.3f;
            p.sustain = rng.nextFloat();
            p.releaseSeconds = 0.01f + rng.nextFloat() * 0.1f;
            p.velocityAmount = rng.nextFloat();
            p.lfoShapeIndex = rng.nextInt (5);
            p.lfoRateRaw = rng.nextFloat();
            p.lfoSync = rng.nextBool();
            p.lfoRetrig = rng.nextBool();
            p.lfoPhaseOffset01 = rng.nextFloat();
            p.lfoFadeSeconds = rng.nextFloat() * 2.0f;
            p.bpm = 20.0f + rng.nextFloat() * 280.0f;
            p.bypass = rng.nextInt (64) == 0; // raro: serve che le voci suonino davvero

            // Lo stadio FX entra ed esce mentre tutto il resto si muove: e' l'unico posto della
            // suite dove la dissolvenza di bypass, il riempimento della linea e il ringout si
            // incrociano con blocchi di lunghezza qualunque e con il bypass generale.
            p.chorusOn = rng.nextInt (3) != 0;
            p.chorusRateHz = 0.1f + rng.nextFloat() * 5.0f;
            p.chorusDepth01 = rng.nextFloat();
            p.chorusMix01 = rng.nextFloat();
            p.chorusFeedback01 = rng.nextFloat();

            p.reverbOn = rng.nextInt (3) != 0;
            p.reverbSize01 = rng.nextFloat();
            p.reverbDecay01 = rng.nextFloat();
            p.reverbDamp01 = rng.nextFloat();
            p.reverbPredelaySeconds = rng.nextFloat() * dsp::PlateReverb::kMaxPredelaySeconds;
            p.reverbMix01 = rng.nextFloat();

            synth.setParams (p);
            synth.setMasterGainLinear (rng.nextFloat());

            // Blocchi di lunghezza variabile: l'host non ne garantisce una fissa, e con blocchi
            // corti l'LFO e gli SmoothedValue avanzano di pochi campioni per volta.
            const int numSamples = 1 + rng.nextInt (128);
            juce::MidiBuffer midi;

            for (int e = rng.nextInt (4); --e >= 0;)
            {
                const int pos = rng.nextInt (numSamples);
                const int roll = rng.nextInt (10);

                if (roll < 5)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 12 + rng.nextInt (108), rng.nextFloat()), pos);
                else if (roll < 8)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 12 + rng.nextInt (108)), pos);
                else
                    midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rng.nextInt (128)), pos);
            }

            juce::AudioBuffer<float> buffer (2, numSamples);
            buffer.clear();
            synth.process (buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                const auto* data = buffer.getReadPointer (ch);

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto value = data[i];

                    if (badBlock < 0 && (! std::isfinite (value) || std::abs (value) > 1.0f))
                    {
                        badBlock = b;
                        badSample = i;
                        badValue = value;
                    }

                    peak = juce::jmax (peak, std::abs (value));
                }
            }
        }

        const auto what = "fuzz seme " + juce::String (seed);
        expect (badBlock < 0, what + ": campione fuori contratto al blocco " + juce::String (badBlock)
                                  + ", indice " + juce::String (badSample) + ", valore " + juce::String (badValue));
        expect (peak > 0.01f, what + ": uscita troppo bassa (" + juce::String (peak) + ")");
    }

    void runTest() override
    {
        dsp::WavetableStore store;
        store.setActive (1);

        beginTest ("matrix pieno e parametri in movimento: ogni campione resta nel contratto");
        {
            // Due passate identiche, una a gain pieno e una a gain ridotto, perche' le due
            // domande sono diverse e una sola misura non risponde a tutte e due.
            //
            // A gain pieno si verifica il **contratto d'uscita**: ogni campione finito e dentro
            // il fondo scala, nessuna voce appesa. Lo fa sweep() campione per campione.
            const auto out = sweep (store, {}, 240, "passata lunga");

            // A gain ridotto si misura il **picco presentato al clipper**, che e' l'unico numero
            // confrontabile fra una versione e l'altra — vedi il commento di Config::masterGainScale.
            constexpr float kScale = 0.15f;
            Config probe;
            probe.masterGainScale = kScale;
            const auto in = (double) sweep (store, probe, 240, "passata lunga, gain ridotto") / kScale;

            // **Perche' questa assertione non guarda piu' l'uscita.** Prima diceva
            // `peak > 0.95`, cioe' "il clipper e' entrato in funzione", ed era giusta finche' il
            // picco d'uscita stava vicino alla soglia. Non lo e' piu': con l'ingresso a 2.0 il
            // clipper restituisce 0.9977, e fra "il clipper lavora" e "il clipper e' saturo" ci
            // sono quattro decimillesimi di uscita. Un'assertione a 0.95 sull'uscita passerebbe
            // identica con un ingresso di 1.0 (mezzo decibel di riduzione, il clipper che sfiora)
            // e con uno di 10 (venti decibel, il clipper diventato un muro): ha smesso di
            // distinguere proprio le due cose che era stata scritta per distinguere.
            //
            // Sull'ingresso invece la domanda torna ben posta, ed e' a due lati:
            //
            //   - sotto 1.4 il clipper toglierebbe meno di 3 dB, cioe' la passata non lo
            //     starebbe esercitando davvero e questo test non verificherebbe niente;
            //   - sopra 2.512 (+8 dBFS) sarebbe il gain staging ad aver ceduto: quello e' il
            //     tetto che kVoiceHeadroomGain e' tarato per rispettare (vedi il test del caso
            //     peggiore qui sotto).
            //
            // Misurato 2.002 (+6.03 dBFS), cioe' 3.2 dB di riduzione: il clipper lavora, e non
            // e' un muro.
            expect (in > 1.4, "ingresso al clipper " + juce::String (in, 3)
                                  + ": la passata non sta esercitando il clipper (meno di 3 dB di riduzione)");
            expect (in < 2.512, "ingresso al clipper " + juce::String (in, 3)
                                    + " (" + juce::String (juce::Decibels::gainToDecibels (in), 2)
                                    + " dBFS): oltre il tetto di +8 dBFS del gain staging");

            expect (out <= 1.0f, "picco d'uscita " + juce::String (out) + ": il clipper deve tenere 0 dBFS");

            logMessage ("matrix pieno, 16 note, 240 blocchi: ingresso al clipper " + juce::String (in, 3)
                        + " (" + juce::String (juce::Decibels::gainToDecibels (in), 2) + " dBFS), uscita "
                        + juce::String (out) + " ("
                        + juce::String (juce::Decibels::gainToDecibels (out), 2) + " dBFS), riduzione "
                        + juce::String (juce::Decibels::gainToDecibels ((double) out / in), 2) + " dB");
        }

        beginTest ("il caso peggiore resta sotto il tetto di +8 dBFS");
        {
            // Il bersaglio dichiarato del gain staging, e il numero che fissa
            // kVoiceHeadroomGain insieme al margine dell'accordo: matrix pieno, `res` modulato a
            // fondo corsa, sedici note, unison 8. Sopra +14 dBFS il soft clipper smetterebbe di
            // essere una rete e diventerebbe uno stadio di distorsione a tempo pieno; +8 e' il
            // tetto che ci si e' dati per la catena **ai valori di fabbrica**, con sei decibel
            // di margine su quel punto.
            //
            // Si misurano tutti e tre gli stati dello stadio FX, e non solo quello di fabbrica,
            // perche' spegnere gli effetti e' una configurazione che l'utente ha a disposizione
            // — ed e' quella che presenta **di piu'**. Con tutto a fondo corsa il riverbero
            // *sostituisce* i picchi risonanti con una coda diffusa, quindi il caso peggiore non
            // e' la somma dei due effetti: e' lo stadio spento.
            //
            // L'ultima riga e' la vera cima raggiungibile dello strumento, e ha un tetto diverso
            // perche' e' una domanda diversa: il chorus a mix **e** feedback a fondo corsa con il
            // riverbero spento, cioe' l'unica combinazione in cui uno stadio FX aggiunge invece
            // di restituire. Quella non deve stare sotto +8 — nessuna taratura del guadagno per
            // voce ce la terrebbe senza buttare via tre decibel di strumento per una posizione di
            // knob che quasi nessuno usa — ma deve restare sotto i +14 oltre i quali il clipper
            // smette di essere una rete.
            constexpr float kScale = 0.15f;
            constexpr double kFactoryCeiling = 2.5118864;  // +8 dBFS
            constexpr double kAbsoluteCeiling = 5.0118723; // +14 dBFS

            struct Case { const char* what; bool factoryFx; bool extremeFx; bool chorusOnly; double ceiling; };

            for (const auto& c : { Case { "FX spenti", false, false, false, kFactoryCeiling },
                                   Case { "FX ai valori di fabbrica", true, false, false, kFactoryCeiling },
                                   Case { "chorus e riverbero a fondo corsa", false, true, false, kFactoryCeiling },
                                   Case { "solo chorus, mix e feedback a fondo corsa", false, true, true, kAbsoluteCeiling } })
            {
                Config cfg;
                cfg.masterGainScale = kScale;
                cfg.unisonVoices = 8;
                cfg.detuneCents = 18.0f;

                if (c.factoryFx)
                {
                    cfg.chorus = true;
                    cfg.chorusRateHz = 1.6f;
                    cfg.chorusDepth01 = 0.4f;
                    cfg.chorusMix01 = 0.3f;
                    cfg.chorusFeedback01 = 0.0f;

                    cfg.reverb = true;
                    cfg.reverbSize01 = 0.6f;
                    cfg.reverbDecay01 = 0.5f;
                    cfg.reverbDamp01 = 0.4f;
                    cfg.reverbPredelaySeconds = 0.02f;
                    cfg.reverbMix01 = 0.25f;
                }

                if (c.extremeFx)
                {
                    cfg.chorus = true;              // Config accende tutto a fondo corsa per conto suo
                    cfg.reverb = ! c.chorusOnly;
                }

                const auto in = (double) sweep (store, cfg, 240,
                                                juce::String ("caso peggiore, ") + c.what) / kScale;

                logMessage (juce::String ("caso peggiore (matrix pieno, res a fondo, unison 8), ") + c.what
                            + ": " + juce::String (in, 3) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (in), 2) + " dBFS), tetto "
                            + juce::String (juce::Decibels::gainToDecibels (c.ceiling), 0) + " dBFS");

                expect (in < c.ceiling,
                        juce::String ("caso peggiore, ") + c.what + ": " + juce::String (in, 3) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (in), 2)
                            + " dBFS) supera il tetto di "
                            + juce::String (juce::Decibels::gainToDecibels (c.ceiling), 0) + " dBFS");
            }
        }

        beginTest ("matrix pieno nelle tre configurazioni di filtro e a tre sample rate");
        {
            // Il picco di risonanza dipende dal sample rate (il warping di tan()) e dal numero
            // di stadi; il passa-alto e il passa-banda hanno un percorso diverso dentro l'SVF.
            // Con `res` modulato a fondo corsa e' proprio qui che il soft clipper va verificato.
            for (const auto rate : { 44100.0, 48000.0, 96000.0 })
                for (const auto type : { dsp::StateVariableFilter::Type::lowPass,
                                         dsp::StateVariableFilter::Type::highPass,
                                         dsp::StateVariableFilter::Type::bandPass })
                    for (const auto stages : { 1, 2 })
                    {
                        Config cfg;
                        cfg.sampleRate = rate;
                        cfg.filterType = type;
                        cfg.filterStages = stages;

                        const auto what = juce::String (rate, 0) + " Hz, tipo " + juce::String ((int) type)
                                              + ", " + juce::String (stages) + " stadi";
                        const auto peak = sweep (store, cfg, 96, what);
                        logMessage (what + ": picco " + juce::String (peak));
                    }
        }

        beginTest ("matrix pieno con LFO in sync e senza retrigger");
        {
            // Senza retrigger la sorgente `lfo` e' quella libera del motore, condivisa da tutte
            // le voci: un percorso diverso da quello per voce, e va stressato anch'esso.
            for (const auto sync : { false, true })
                for (const auto retrig : { false, true })
                {
                    Config cfg;
                    cfg.lfoSync = sync;
                    cfg.lfoRetrig = retrig;
                    sweep (store, cfg, 96,
                           juce::String ("sync ") + (sync ? "on" : "off") + ", retrig "
                               + (retrig ? "on" : "off"));
                }
        }

        beginTest ("matrix pieno e voice stealing: ventiquattro note su un pool da sedici");
        {
            // Oltre la polifonia si ruba, e la voce rubata resta a dissolvere mentre la nota
            // nuova ne prende una libera fra quelle di margine: qui suonano quindi fino a
            // poolSize voci invece di maxVoices, ognuna con il matrix pieno addosso. E' la
            // configurazione con piu' voci simultanee di tutto il file, ed e' quella che deve
            // restare dentro il fondo scala. (Che il furto non produca un gradino lo misura
            // "rubare una voce non produce un gradino", in VoiceStealingTests.)
            Config cfg;
            cfg.numNotes = 24;
            const auto peak = sweep (store, cfg, 180, "24 note");
            logMessage ("24 note (8 rubate): picco " + juce::String (peak));
        }

        beginTest ("l'anello regge una ripubblicazione a ogni blocco");
        {
            Config cfg;
            cfg.republishEachBlock = true;
            sweep (store, cfg, 180, "ripubblicazione continua");
        }

        beginTest ("matrix pieno e unison 8: centoventotto oscillatori sotto modulazione");
        {
            // Unison moltiplica per otto gli oscillatori ma non tocca il resto della catena:
            // cio' che va verificato e' che la somma delle copie, compensata di 1/sqrt(N), non
            // faccia saltare il contratto proprio dove il margine e' gia' stretto — con `res`
            // modulato a fondo corsa su sedici note.
            Config cfg;
            cfg.unisonVoices = 8;
            cfg.detuneCents = 18.0f;
            const auto peak = sweep (store, cfg, 240, "unison 8"); // stesso numero di blocchi della passata a unison 1

            logMessage ("unison 8, detune 18 ct, 16 note: picco " + juce::String (peak) + " ("
                        + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS)");
        }

        beginTest ("matrix pieno e stadio FX acceso: il margine al soft clipper con il chorus");
        {
            // La stessa passata di "matrix pieno e parametri in movimento", con in piu' il
            // chorus a mix e feedback a fondo corsa. E' la misura che docs/architecture.md
            // chiede a chiunque aggiunga uno stadio di guadagno dopo il filtro: quanto si
            // muove il picco **presentato al clipper**, non quello che esce.
            //
            // Questi sei numeri sono gli addendi del bilancio del gain staging: quanto lo
            // stadio FX sposta il picco presentato al clipper in ognuna delle sue posizioni
            // notevoli. Il caso peggiore assoluto dell'intero strumento e' **qui dentro** e non
            // nel test del caso peggiore qui sopra: e' il chorus a mix e feedback a fondo corsa
            // a unison 1 (2.832, +9.04 dBFS), piu' alto di quanto la stessa configurazione dia a
            // unison 8 (2.496), perche' le otto copie scordate arrivano decorrelate nell'anello
            // di feedback e ci costruiscono dentro meno.
            constexpr float scale = 0.2f; // tiene il picco d'uscita sotto la soglia del clipper

            Config dry;
            dry.masterGainScale = scale;

            // I valori di fabbrica di chRate, chDepth, chMix e chFeedback: e' cio' che ogni
            // preset suonera' senza che l'utente tocchi niente, quindi e' il caso su cui il
            // contratto deve essere stretto.
            Config factory = dry;
            factory.chorus = true;
            factory.chorusRateHz = 1.6f;
            factory.chorusDepth01 = 0.4f;
            factory.chorusMix01 = 0.3f;
            factory.chorusFeedback01 = 0.0f;

            Config fullMix = dry;
            fullMix.chorus = true;
            fullMix.chorusFeedback01 = 0.0f; // tutto il resto a fondo corsa

            Config fullBoth = dry;
            fullBoth.chorus = true;          // anche il feedback

            // I due effetti insieme, tutti e due a fondo corsa. Non e' il caso peggiore, ed e'
            // questa la cosa da sapere: il riverbero a mix 100 % **sostituisce** i picchi
            // risonanti con una coda diffusa invece di sommarcisi.
            Config everything = fullBoth;
            everything.reverb = true;

            Config reverbFactory = dry;
            reverbFactory.reverb = true;
            reverbFactory.reverbSize01 = 0.6f;
            reverbFactory.reverbDecay01 = 0.5f;
            reverbFactory.reverbDamp01 = 0.4f;
            reverbFactory.reverbPredelaySeconds = 0.02f;
            reverbFactory.reverbMix01 = 0.25f;

            const auto dryIn = (double) sweep (store, dry, 240, "senza FX, gain ridotto") / scale;
            const auto factoryIn = (double) sweep (store, factory, 240, "chorus ai default") / scale;
            const auto mixIn = (double) sweep (store, fullMix, 240, "chorus a mix 100 %, feedback 0") / scale;
            const auto bothIn = (double) sweep (store, fullBoth, 240, "chorus a mix e feedback 100 %") / scale;
            const auto reverbIn = (double) sweep (store, reverbFactory, 240, "riverbero ai default") / scale;
            const auto everythingIn = (double) sweep (store, everything, 240, "chorus e riverbero a fondo corsa") / scale;

            const auto report = [this, dryIn] (const char* what, double peak)
            {
                logMessage (juce::String ("matrix pieno, picco al soft clipper, ") + what + ": "
                            + juce::String (peak, 3) + " ("
                            + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS), "
                            + juce::String (juce::Decibels::gainToDecibels (peak / dryIn), 2)
                            + " dB rispetto a senza FX");
            };

            report ("senza FX", dryIn);
            report ("chorus ai valori di fabbrica", factoryIn);
            report ("chorus a mix 100 %, feedback 0", mixIn);
            report ("chorus a mix 100 %, feedback 100 %", bothIn);
            report ("riverbero ai valori di fabbrica", reverbIn);
            report ("chorus e riverbero, tutto a fondo corsa", everythingIn);

            // **Il contratto che conta.** Ai valori di fabbrica il chorus e' acceso su tutti e
            // dodici i preset, quindi non gli e' concesso di spostare il margine: il mix e'
            // equal-power e il feedback e' a zero, quindi il picco puo' solo restare dov'e' o
            // scendere. Misurato -0.17 dB. La soglia era 1.0 ed e' scesa a 0.5: con il gain
            // staging ritarato questi decibel sono in bilancio, e una soglia che lascia passare
            // un decibel intero ne lascerebbe passare uno dei sei che separano il caso peggiore
            // dal punto in cui il clipper smette di essere una rete.
            const auto factoryDb = juce::Decibels::gainToDecibels (factoryIn / dryIn);
            expect (factoryDb < 0.5, "il chorus ai valori di fabbrica aggiunge " + juce::String (factoryDb, 2)
                                         + " dB al picco presentato al clipper");

            // Agli estremi il chorus **prende** margine, ed e' l'unico stadio che lo faccia:
            // misurato +3.01 dB. Due contratti, uno relativo e uno assoluto, e serve il secondo.
            // Il relativo dice che il feedback non e' sfuggito di mano (kMaxFeedback si ferma a
            // mezzo proprio per questo); l'assoluto dice la cosa che al gain staging interessa
            // davvero, cioe' che nemmeno la posizione di knob peggiore dello strumento porta il
            // clipper oltre i +14 dBFS in cui smette di essere una rete. Misurato 2.832
            // (+9.04 dBFS): cinque decibel di margine su quel punto.
            const auto worstDb = juce::Decibels::gainToDecibels (bothIn / dryIn);
            expect (worstDb < 4.0, "il chorus al caso peggiore aggiunge " + juce::String (worstDb, 2)
                                       + " dB al picco presentato al clipper");
            expect (bothIn < 5.0118723, "il chorus al caso peggiore presenta " + juce::String (bothIn, 3)
                                            + " al clipper, oltre i +14 dBFS in cui smette di essere una rete");

            // Il riverbero ai valori di fabbrica e' acceso di default come il chorus, quindi
            // vale per lui lo stesso contratto stretto, con la stessa soglia. Misurato -0.70 dB.
            const auto reverbDb = juce::Decibels::gainToDecibels (reverbIn / dryIn);
            expect (reverbDb < 0.5, "il riverbero ai valori di fabbrica aggiunge "
                                        + juce::String (reverbDb, 2)
                                        + " dB al picco presentato al clipper");

            // E i due effetti insieme a fondo corsa, dove la soglia era 4.0 dB e non verificava
            // niente: la misura vale **-4.09 dB**, cioe' otto decibel dall'altra parte. La
            // proprieta' vera e' che il riverbero a mix pieno *sostituisce* i picchi invece di
            // sommarcisi, e adesso e' quella a essere verificata — un giorno in cui il riverbero
            // cominciasse a sommare, la vecchia soglia sarebbe rimasta verde fino a otto decibel
            // dopo.
            const auto everythingDb = juce::Decibels::gainToDecibels (everythingIn / dryIn);
            expect (everythingDb < 0.0, "con i due effetti a fondo corsa il picco al clipper sale di "
                                            + juce::String (everythingDb, 2)
                                            + " dB: il riverbero a mix pieno deve sostituire i picchi, non sommarcisi");
        }

        beginTest ("fuzz a semi fissi: matrix casuale, parametri casuali, eventi casuali");
        {
            // Le passate sopra percorrono traiettorie scelte a mano, quindi esplorano solo le
            // combinazioni a cui ho pensato. Questa attraversa lo spazio in modo diverso a ogni
            // seme: matrix di lunghezza qualunque, depth qualunque, basi che saltano di blocco
            // in blocco, note che entrano ed escono. I semi sono fissi apposta — un test che
            // fallisce una volta su venti non e' un test — ma il generatore e' stato fatto
            // girare su duecento semi durante la scrittura, senza trovare un solo campione
            // fuori dal contratto.
            for (const auto seed : { 1, 7, 42, 1337, 90210 })
                fuzz (store, seed, 400);
        }

        beginTest ("count a zero copre le route rimaste nello slot dell'anello");
        {
            // buildModSnapshot riscrive `count` ma non ripulisce le route oltre di esso, e gli
            // slot dell'anello vengono riusati: se qualcuno leggesse routes[] senza fermarsi a
            // `count`, una modulazione cancellata dalla UI continuerebbe a suonare. Qui
            // l'anello viene riempito di snapshot pieni, poi svuotato, e l'uscita deve tornare
            // identica campione per campione a quella di un motore che non ha mai visto una
            // route.
            const auto render = [&store] (bool fillRingFirst)
            {
                engine::SynthEngine synth; prepareEngine (synth, store);

                if (fillRingFirst)
                {
                    // Cinque pubblicazioni: l'anello ha quattro slot, quindi ne riscrive uno.
                    for (int i = 0; i < 5; ++i)
                        synth.setMods (fullSnapshot());

                    auto empty = fullSnapshot();
                    empty.count = 0; // route ancora tutte li' dentro, ma invisibili
                    synth.setMods (empty);
                }

                auto p = defaultParams();
                p.filterOn = true;
                p.modBase.fill (0.5f);
                synth.setParams (p);
                synth.setMasterGainLinear (0.8f);

                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                juce::AudioBuffer<float> b (2, 128);
                b.clear();
                synth.process (b, m);

                std::vector<float> out;
                juce::MidiBuffer none;
                for (int i = 0; i < 30; ++i)
                {
                    juce::AudioBuffer<float> buf (2, 128);
                    buf.clear();
                    synth.process (buf, none);
                    const auto* d = buf.getReadPointer (0);
                    out.insert (out.end(), d, d + 128);
                }
                return out;
            };

            const auto clean = render (false);
            const auto emptied = render (true);

            expectEquals ((int) emptied.size(), (int) clean.size());
            for (size_t i = 0; i < clean.size(); ++i)
                expectWithinAbsoluteError (emptied[i], clean[i], 0.0f);
        }
    }
};

static ModulationStressTests modulationStressTests;
