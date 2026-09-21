#include "bridge/AudioSettings.h"

#include <juce_core/juce_core.h>

/** Il contratto JSON del pannello impostazioni (WebUI/src/juce/backend.ts, AudioSettings). */
struct AudioSettingsTests final : public juce::UnitTest
{
    AudioSettingsTests() : juce::UnitTest ("bridge: audio settings payload", "bridge") {}

    void runTest() override
    {
        beginTest ("nel plugin: standalone falso e nessuna lista");
        {
            const auto v = bridge::audioSettingsToVar ({});
            expect (! (bool) v["standalone"]);
            expectEquals (v["outputs"].getArray()->size(), 0);
            expectEquals (v["sampleRates"].getArray()->size(), 0);
            expectEquals (v["bufferSizes"].getArray()->size(), 0);
        }

        beginTest ("nello Standalone: device, liste e valori correnti");
        {
            bridge::AudioSettings s;
            s.standalone = true;
            s.outputs = { { "AppleHDA", "MacBook Pro Speakers" }, { "Scarlett", "Focusrite 2i2" } };
            s.currentOutput = "Scarlett";
            s.sampleRates = { 44100.0, 48000.0 };
            s.currentSampleRate = 48000.0;
            s.bufferSizes = { 64, 128, 256 };
            s.currentBufferSize = 128;

            const auto v = bridge::audioSettingsToVar (s);
            expect ((bool) v["standalone"]);
            expectEquals (v["outputs"].getArray()->size(), 2);
            expectEquals (v["outputs"][1]["name"].toString(), juce::String ("Focusrite 2i2"));
            expectEquals (v["currentOutput"].toString(), juce::String ("Scarlett"));
            expectEquals ((double) v["currentSampleRate"], 48000.0);
            expectEquals ((int) v["currentBufferSize"], 128);
        }

        beginTest ("la latenza viene calcolata dal buffer e dal sample rate");
        {
            bridge::AudioSettings s;
            s.standalone = true;
            s.currentSampleRate = 48000.0;
            s.currentBufferSize = 240;
            // 240 / 48000 = 5 ms esatti
            expectWithinAbsoluteError ((double) bridge::audioSettingsToVar (s)["latencyMs"], 5.0, 1.0e-9);
        }

        beginTest ("sample rate a zero: latenza zero, non infinito");
        {
            // Succede davvero fra la chiusura di un device e l'apertura del successivo.
            bridge::AudioSettings s;
            s.standalone = true;
            s.currentSampleRate = 0.0;
            s.currentBufferSize = 128;
            expectEquals ((double) bridge::audioSettingsToVar (s)["latencyMs"], 0.0);
        }

        beginTest ("il valore corrente si riporta anche se non e' fra i disponibili");
        {
            // Device staccato a caldo: l'AudioDeviceManager tiene il vecchio valore mentre la
            // lista dei disponibili e' gia' quella nuova. La funzione riporta, non corregge.
            bridge::AudioSettings s;
            s.standalone = true;
            s.sampleRates = { 44100.0 };
            s.currentSampleRate = 96000.0;
            expectEquals ((double) bridge::audioSettingsToVar (s)["currentSampleRate"], 96000.0);
        }
    }
};

static AudioSettingsTests audioSettingsTests;
