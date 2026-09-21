#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace bridge
{
/** Un device di uscita, come lo vede la UI. Su CoreAudio `id` e `name` coincidono: JUCE
    identifica i device per nome (AudioIODeviceType::getDeviceNames), e un id separato non
    esiste. I due campi restano distinti perche' il contratto JSON non debba cambiare se un
    giorno si aggiunge un backend che gli id ce li ha. */
struct AudioOutputDevice
{
    juce::String id;
    juce::String name;
};

/** Lo stato del device audio come lo mostra il pannello. Tutto cio' che serve alla UI, niente
    di piu': le liste dipendono dal device aperto e vanno rilette a ogni cambio. */
struct AudioSettings
{
    bool standalone { false };
    std::vector<AudioOutputDevice> outputs;
    juce::String currentOutput;
    std::vector<double> sampleRates;
    double currentSampleRate { 0.0 };
    std::vector<int> bufferSizes;
    int currentBufferSize { 0 };
};

/**
 * Il payload di getAudioSettings() e dell'evento "audioSettingsChanged".
 *
 * `standalone` falso (VST3/AU) significa "li gestisce l'host": le liste sono vuote e la UI
 * mostra una riga di testo, non controlli morti. Funzione libera e header-only perche' sia
 * testabile senza aprire nessun device audio (Tests/AudioSettingsTests.cpp) — stesso motivo,
 * e stessa forma, di bridge/MidiDevices.h.
 *
 * `latencyMs` e' calcolata qui e non passata da fuori: e' l'unica aritmetica del payload, e
 * tenerla dentro la funzione pura e' cio' che la mette sotto test. Con sample rate a zero —
 * che capita davvero fra la chiusura di un device e l'apertura del successivo — vale zero
 * invece di infinito.
 */
inline juce::var audioSettingsToVar (const AudioSettings& s)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("standalone", s.standalone);

    juce::Array<juce::var> outputs;

    for (const auto& d : s.outputs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", d.id);
        o->setProperty ("name", d.name);
        outputs.add (juce::var (o));
    }

    obj->setProperty ("outputs", outputs);
    obj->setProperty ("currentOutput", s.currentOutput);

    juce::Array<juce::var> rates;
    for (const auto r : s.sampleRates) rates.add (r);
    obj->setProperty ("sampleRates", rates);
    obj->setProperty ("currentSampleRate", s.currentSampleRate);

    juce::Array<juce::var> sizes;
    for (const auto b : s.bufferSizes) sizes.add (b);
    obj->setProperty ("bufferSizes", sizes);
    obj->setProperty ("currentBufferSize", s.currentBufferSize);

    obj->setProperty ("latencyMs", s.currentSampleRate > 0.0
                                       ? 1000.0 * (double) s.currentBufferSize / s.currentSampleRate
                                       : 0.0);

    return juce::var (obj);
}
} // namespace bridge
