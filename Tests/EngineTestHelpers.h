#pragma once

/**
 * Gli helper che le suite del motore condividono: erano nel namespace anonimo di un unico
 * EngineTests.cpp da 4800 righe, oggi spezzato per suite. Tutto `inline` in `harness`, accanto a
 * EngineHarness.h; nessun comportamento cambiato.
 */

#include "EngineHarness.h"

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

namespace harness
{
/** Fa girare `numBlocks` blocchi da 128 campioni senza MIDI e ritorna il picco assoluto
    misurato su tutti i canali. Controlla anche che l'uscita resti finita: un bug di
    stabilità in un qualsiasi test emerge subito invece di passare inosservato. */
inline float renderPeak (engine::SynthEngine& synth, int numBlocks, juce::UnitTest& test)
{
    float peak = 0.0f;
    juce::MidiBuffer noMidi;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, noMidi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                test.expect (std::isfinite (data[i]), "campione non finito");
                peak = juce::jmax (peak, std::abs (data[i]));
            }
        }
    }

    return peak;
}

/** RMS su `numBlocks` blocchi da 128 campioni, canale sinistro. */
inline float renderRms (engine::SynthEngine& synth, int numBlocks)
{
    double sumSquares = 0.0;
    int count = 0;
    juce::MidiBuffer noMidi;

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, noMidi);

        const auto* data = buffer.getReadPointer (0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            sumSquares += (double) data[i] * (double) data[i];
            ++count;
        }
    }

    return count > 0 ? (float) std::sqrt (sumSquares / count) : 0.0f;
}

/** Stessa formula del ramo Map::Linear di params::denormalise (Source/parameters/
    ParameterMapping.h), copiata qui invece di inclusa: quell'header porta dentro
    juce_audio_processors, che XerumTests non linka (nessun bisogno degli AudioParameter*
    per testare solo l'aritmetica di denormalizzazione). oct e semi usano entrambi
    Map::Linear, quindi la formula e' fedele. */
inline float denormaliseLinear (const params::Spec& s, float x) noexcept
{
    return s.min + x * (s.max - s.min);
}

/**
 * Il picco **presentato al soft clipper**, misurato con il metodo del gain ridotto.
 *
 * E' l'unico numero confrontabile fra una configurazione e l'altra, e non si ottiene invertendo
 * il clipper: sopra soglia l'inversa e' malcondizionata da far spavento (0.9983 in uscita risale
 * a 2.36 all'ingresso, 0.9990 a 3.40 — sette decimillesimi di uscita diventano 3.4 dB di
 * ingresso stimato). Qui invece si abbassa il gain master finche' il clipper resta spento, si
 * **verifica** che sia spento, e si riscala: tutto cio' che precede il clipper (voci, stadio FX)
 * e' esattamente lineare nel gain master, quindi il riscalamento e' esatto e non una stima.
 *
 * Il blocco muto prima della nota non e' decorazione: `applyGainRamp` parte da
 * `previousMasterGain_`, che al primissimo blocco vale ancora 1.0. Senza quello scarto il primo
 * blocco della nota viene moltiplicato da una rampa che scende dall'unita' al gain di prova, e
 * il picco che si misura e' quella rampa, non il segnale — un fattore fino a otto.
 */
struct ClipperProbe
{
    double peak { 0.0 };   // presentato al clipper, riscalato al volume vero
    double rms { 0.0 };    // idem, sul canale sinistro, nella finestra di misura
    bool clipperOff { true };
};

inline ClipperProbe measureAtClipper (engine::SynthEngine& synth, const std::vector<int>& notes,
                               float realVolume, int blocks, int rmsFromBlock,
                               float probeVolume = 0.02f, int peakFromBlock = 0)
{
    synth.setMasterGainLinear (probeVolume);

    {
        juce::MidiBuffer none;
        juce::AudioBuffer<float> warmUp (2, 128);
        warmUp.clear();
        synth.process (warmUp, none);
    }

    juce::MidiBuffer midi;
    for (const auto note : notes)
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);

    double peak = 0.0;
    double energy = 0.0;
    long counted = 0;

    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (2, 128);
        buffer.clear();
        synth.process (buffer, midi);
        midi.clear();

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 128; ++i)
            {
                const auto v = (double) std::abs (buffer.getSample (ch, i));

                if (b >= peakFromBlock)
                    peak = juce::jmax (peak, v);

                if (b >= rmsFromBlock && ch == 0)
                {
                    energy += v * v;
                    ++counted;
                }
            }
    }

    const auto scale = (double) realVolume / (double) probeVolume;
    return { peak * scale, counted > 0 ? std::sqrt (energy / (double) counted) * scale : 0.0,
             peak < 0.95 };
}

/** Il grezzo che l'APVTS conterrebbe per un valore normalizzato 0..1, Kind per Kind. E'
    l'inversa di params::normalisedDefault sul percorso che i preset percorrono davvero. */
inline float rawFromNormalised (const params::Spec& spec, float norm) noexcept
{
    switch (spec.kind)
    {
        case params::Kind::Bool:   return norm >= 0.5f ? 1.0f : 0.0f;
        case params::Kind::Choice: return spec.numOptions > 1 ? norm * (float) (spec.numOptions - 1) : 0.0f;
        case params::Kind::Int:    return spec.min + norm * (spec.max - spec.min);
        case params::Kind::Float:  break;
    }

    return norm;
}

/** Un preset di fabbrica tradotto in cio' che il motore riceve davvero: la EngineParams che
    esce da collectEngineParams, il gain master e la tavola. "Init" (nessun valore) da' i default
    di parameters.json, cioe' il suono che si trova aprendo il plugin. */
struct PresetPatch
{
    engine::EngineParams params;
    float volume { 0.8f };
    int wavetable { 0 };
};

inline PresetPatch patchFromPreset (const params::Preset& preset)
{
    std::array<float, (size_t) params::ParamSlot::count> raw {};

    for (int i = 0; i < (int) params::ParamSlot::count; ++i)
    {
        const auto& spec = params::specForSlot ((params::ParamSlot) i);
        raw[(size_t) i] = rawFromNormalised (spec, params::presetValue (preset, spec));
    }

    PresetPatch patch;
    patch.params = params::collectEngineParams ([&raw] (params::ParamSlot slot) { return raw[(size_t) slot]; });

    constexpr auto* volumeSpec = params::find ("volume");
    constexpr auto* wtSpec = params::find ("wtIndex");
    static_assert (volumeSpec != nullptr && wtSpec != nullptr, "volume/wtIndex non sono in ParameterTable.h");

    // `volume` ha mappa Db ma il grezzo e' gia' il guadagno lineare, come `level`.
    patch.volume = params::presetValue (preset, *volumeSpec);
    patch.wavetable = juce::roundToInt (rawFromNormalised (*wtSpec, params::presetValue (preset, *wtSpec)));
    return patch;
}
} // namespace harness
