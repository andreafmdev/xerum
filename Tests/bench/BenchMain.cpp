/**
 * Banco di misura del percorso audio: rende audio offline con engine::SynthEngine e riporta,
 * per ogni scenario, quanto costa un campione.
 *
 * Non e' un test: non asserisce niente e non entra in ctest. Serve a rispondere a una domanda
 * sola — "dove se ne va il tempo del thread audio" — con dei numeri invece che con una lettura
 * del codice, e a poterla rifare dopo ogni modifica per sapere se una ottimizzazione ha pagato.
 *
 * Il numero che conta non e' il tempo assoluto ma la **frazione di un core** che lo scenario
 * consumerebbe girando in tempo reale: ns per campione diviso il periodo di campionamento
 * (20833 ns a 48 kHz per un blocco intero). Un host che chiama con blocchi da 128 campioni
 * concede 2.67 ms ogni 2.67 ms: sopra il 100 % ci sono i dropout, e la soglia pratica di un
 * plugin che deve convivere con altri sta molto piu' in basso.
 *
 * Metodo: ogni scenario gira `kWarmupBlocks` blocchi buttati via (cache calde, inviluppi a
 * regime, code degli FX riempite) e poi `kMeasureBlocks` blocchi cronometrati; si ripete
 * `kRepeats` volte e si tiene il **minimo**, che e' la stima meno disturbata dal resto della
 * macchina. Il risultato viene stampato in TSV, cosi' due esecuzioni si confrontano con diff.
 */

#include "dsp/WavetableStore.h"
#include "engine/EngineParams.h"
#include "engine/SynthEngine.h"
#include "engine/VoiceManager.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 128;
constexpr int kWarmupBlocks = 200;
constexpr int kMeasureBlocks = 2000; // ~5.3 s di audio a 48 kHz
constexpr int kRepeats = 5;

engine::EngineParams baseParams()
{
    engine::EngineParams p;
    p.oscOn = true;
    p.level = 0.5f;
    p.filterOn = true;
    p.filterType = dsp::StateVariableFilter::Type::lowPass;
    p.filterStages = 2;
    p.cutoffHz = 4000.0f;
    p.resonanceQ = 0.707f;
    p.attackSeconds = 0.005f;
    p.decaySeconds = 0.2f;
    p.sustain = 0.8f;
    p.releaseSeconds = 0.3f;
    return p;
}

/** Note-on su `count` note distinte, tutte a campione zero del primo blocco. */
juce::MidiBuffer chordOf (int count)
{
    juce::MidiBuffer midi;

    for (int i = 0; i < count; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, 36 + i * 3, 0.8f), 0);

    return midi;
}

struct Scenario
{
    std::string name;
    int notes { 0 };
    std::function<void (engine::EngineParams&)> tweak;
};

struct Result
{
    double nsPerSample { 0.0 };
    double coreFraction { 0.0 };
    /** Il livello RMS dell'ultimo blocco misurato: un numero che non c'entra con il tempo, ma
        se e' zero lo scenario ha cronometrato del silenzio e il resto della riga non vale
        niente. */
    double rms { 0.0 };
};

Result run (const Scenario& scenario, dsp::WavetableStore& store, const engine::ModSnapshot* mods)
{
    auto best = std::numeric_limits<double>::max();
    double rms = 0.0;

    for (int repeat = 0; repeat < kRepeats; ++repeat)
    {
        engine::SynthEngine synth;

        engine::EngineSpec spec;
        spec.sampleRate = kSampleRate;
        spec.maximumBlockSize = kBlockSize;
        spec.numChannels = 2;
        synth.prepare (spec);
        synth.setWavetable (store.active());

        auto params = baseParams();

        if (scenario.tweak)
            scenario.tweak (params);

        params.mods = mods;
        synth.setParams (params);
        synth.setMasterGainLinear (0.8f);

        juce::AudioBuffer<float> buffer (2, kBlockSize);
        auto onset = chordOf (scenario.notes);
        juce::MidiBuffer empty;

        // Le note partono nel primo blocco di riscaldamento: quando parte il cronometro gli
        // inviluppi sono in sustain e le linee degli FX sono piene, che e' il regime che si
        // vuole misurare — non l'attacco.
        for (int block = 0; block < kWarmupBlocks; ++block)
        {
            buffer.clear();
            synth.setParams (params);
            auto& midi = block == 0 ? onset : empty;
            synth.process (buffer, midi);
        }

        const auto start = std::chrono::steady_clock::now();

        for (int block = 0; block < kMeasureBlocks; ++block)
        {
            buffer.clear();
            synth.setParams (params);
            synth.process (buffer, empty);
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds> (
                                 std::chrono::steady_clock::now() - start)
                                 .count();

        best = std::min (best, (double) elapsed / (double) (kMeasureBlocks * kBlockSize));
        rms = std::sqrt (buffer.getRMSLevel (0, 0, kBlockSize) * buffer.getRMSLevel (0, 0, kBlockSize));
    }

    const auto samplePeriodNs = 1.0e9 / kSampleRate;
    return { best, best / samplePeriodNs, rms };
}
} // namespace

int main()
{
    dsp::WavetableStore store;
    store.setActive (0);

    // Una route lfo -> cutoff, l'unica fra gli scenari che accende il percorso modulato di
    // SynthVoice::applyModulation (denormalizzazioni e pow per sotto-fetta).
    engine::ModSnapshot mods;
    mods.count = 1;
    mods.routes[0].src = engine::ModSource::lfo;
    mods.routes[0].targetIndex = 0; // il primo target: cutoff, vedi engine::kModTargets
    mods.routes[0].depth = 0.4f;

    const std::vector<Scenario> scenarios {
        { "idle-nessuna-nota", 0, {} },
        { "1-voce", 1, {} },
        { "8-voci", 8, {} },
        { "16-voci", 16, {} },
        { "16-voci-no-filtro", 16, [] (auto& p) { p.filterOn = false; } },
        { "16-voci-risonanza", 16, [] (auto& p) { p.resonanceQ = 12.0f; } },
        { "16-voci-drive", 16, [] (auto& p) { p.driveGain = 2.0f; } },
        { "16-voci-wtpos-intermedio", 16, [] (auto& p) { p.framePosition = 0.37f; } },
        { "16-voci-res-butterworth-1-stadio", 16, [] (auto& p) { p.filterStages = 1; } },
        { "16-voci-warp", 16, [] (auto& p) { p.warp = 0.5f; } },
        { "16-voci-unison2", 16, [] (auto& p) { p.unisonVoices = 2; p.detuneCents = 12.0f; } },
        { "16-voci-unison4", 16, [] (auto& p) { p.unisonVoices = 4; p.detuneCents = 12.0f; } },
        { "16-voci-unison8", 16, [] (auto& p) { p.unisonVoices = 8; p.detuneCents = 12.0f; } },
        { "16-voci-chorus", 16, [] (auto& p) { p.chorusOn = true; p.chorusMix01 = 0.5f; p.chorusDepth01 = 0.5f; } },
        { "16-voci-delay", 16, [] (auto& p) { p.delayOn = true; p.delayMix01 = 0.4f; p.delayFeedback01 = 0.4f; } },
        { "16-voci-riverbero", 16, [] (auto& p) { p.reverbOn = true; p.reverbMix01 = 0.4f; } },
        { "16-voci-fx-completo", 16, [] (auto& p) {
             p.chorusOn = true; p.chorusMix01 = 0.4f; p.chorusDepth01 = 0.5f;
             p.delayOn = true; p.delayMix01 = 0.3f; p.delayFeedback01 = 0.4f;
             p.reverbOn = true; p.reverbMix01 = 0.4f; } },
        { "16-voci-unison8-fx-completo", 16, [] (auto& p) {
             p.unisonVoices = 8; p.detuneCents = 12.0f;
             p.chorusOn = true; p.chorusMix01 = 0.4f; p.chorusDepth01 = 0.5f;
             p.delayOn = true; p.delayMix01 = 0.3f; p.delayFeedback01 = 0.4f;
             p.reverbOn = true; p.reverbMix01 = 0.4f; } },
    };

    std::printf ("scenario\tns/campione\t%% di un core\trms\n");

    for (const auto& scenario : scenarios)
    {
        const auto result = run (scenario, store, nullptr);
        std::printf ("%s\t%.2f\t%.1f\t%.4f\n", scenario.name.c_str(), result.nsPerSample,
                     result.coreFraction * 100.0, result.rms);
        std::fflush (stdout);
    }

    // Gli stessi due scenari con una route attiva: la differenza e' il costo del percorso
    // modulato, che a matrix vuoto non gira affatto.
    for (const auto& scenario : { Scenario { "16-voci-mod-cutoff", 16, {} },
                                  Scenario { "16-voci-unison8-mod-cutoff", 16,
                                             [] (auto& p) { p.unisonVoices = 8; p.detuneCents = 12.0f; } } })
    {
        const auto result = run (scenario, store, &mods);
        std::printf ("%s\t%.2f\t%.1f\t%.4f\n", scenario.name.c_str(), result.nsPerSample,
                     result.coreFraction * 100.0, result.rms);
        std::fflush (stdout);
    }

    std::printf ("\nsizeof(EngineParams)=%zu  sizeof(SynthVoice)=%zu  voci nel pool=%d\n",
                 sizeof (engine::EngineParams), sizeof (engine::SynthVoice), engine::VoiceManager::poolSize);

    return 0;
}
