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
 * Il gain staging visto dai dodici preset di fabbrica e dalla corsa di `drive`.
 *
 * Gli altri test del file costruiscono a mano una engine::EngineParams; qui si parte invece da
 * presets.json e parameters.json e si passa da params::collectEngineParams, cioe' dalla stessa
 * aritmetica che percorre il plugin vero. E' l'unico posto della suite dove un preset viene
 * davvero *suonato* invece che solo controllato come insieme di numeri.
 */
struct PresetGainStagingTests final : juce::UnitTest
{
    PresetGainStagingTests() : juce::UnitTest ("gain staging dei preset", "engine") {}

    void runTest() override
    {
        dsp::WavetableStore store;

        beginTest ("nessun preset di fabbrica accende il clipper su un accordo di quattro note");
        {
            // Il criterio di accettazione del gain staging che nessun test copriva: la tabella
            // dei preset veniva controllata solo per validita' dei numeri (PresetValueTests),
            // mai facendola suonare. Prima di questa ritaratura "Acid Line" presentava **1.035**
            // al clipper su quattro note a secco, cioe' lo accendeva — e la documentazione
            // affermava il contrario (0.84), perche' quel numero veniva da una misura vecchia.
            //
            // Si misurano due stati per preset, e servono tutti e due: con gli effetti (come
            // l'utente li trova, accesi di default) e a secco (come restano se li spegne). Il
            // secco e' quasi sempre il piu' alto, perche' il ramo secco del mix equal-power al
            // 30 % vale 0.891 e quindi gli effetti ai valori di fabbrica *restituiscono* picco.
            //
            // 1500 blocchi sono 4 secondi: bastano all'attacco piu' lento della tabella
            // ("Cold Sweep", 1.8 s) e alla coda del riverbero per arrivare a regime.
            constexpr float kMinMarginDb = 1.0f;

            double hottestDry = 0.0;
            juce::String hottestName;

            for (int i = 0; i < params::kNumPresets; ++i)
            {
                const auto& preset = params::kPresetTable[i];
                const auto patch = patchFromPreset (preset);

                const auto probe = [&] (bool fx)
                {
                    store.setActive (patch.wavetable);
                    engine::SynthEngine synth;
                    prepareEngine (synth, store);

                    auto p = patch.params;
                    p.chorusOn = fx && patch.params.chorusOn;
                    p.reverbOn = fx && patch.params.reverbOn;
                    synth.setParams (p);

                    return measureAtClipper (synth, { 60, 64, 67, 72 }, patch.volume, 1500, 1400);
                };

                const auto withFx = probe (true);
                const auto dry = probe (false);

                expect (withFx.clipperOff && dry.clipperOff,
                        juce::String (preset.name) + ": il gain di prova non basta, la misura non vale");

                const auto worst = juce::jmax (withFx.peak, dry.peak);
                const auto marginDb = juce::Decibels::gainToDecibels (0.95 / worst);

                if (dry.peak > hottestDry)
                {
                    hottestDry = dry.peak;
                    hottestName = preset.name;
                }

                logMessage (juce::String (preset.name) + ": FX " + juce::String (withFx.peak, 3)
                            + " | secco " + juce::String (dry.peak, 3) + " | margine "
                            + juce::String (marginDb, 2) + " dB");

                expect (worst < 0.95,
                        juce::String (preset.name) + " accende il soft clipper su quattro note: "
                            + juce::String (worst, 3) + " contro una soglia di 0.95");

                expect (marginDb > kMinMarginDb,
                        juce::String (preset.name) + " ha " + juce::String (marginDb, 2)
                            + " dB di margine sotto il clipper, meno del minimo di progetto ("
                            + juce::String (kMinMarginDb, 1) + " dB)");
            }

            // Il preset piu' caldo e' "Init", cioe' i default di parameters.json: e' giusto che
            // sia cosi', perche' e' l'unico che non ha nessuno che lo abbia ritarato, e se non lo
            // fosse vorrebbe dire che un preset e' stato lasciato piu' caldo del suono di
            // partenza. "Acid Line" lo era, di un decibel, ed e' stato riportato in riga
            // abbassandogli `level` da 0.9 a 0.8.
            logMessage ("il piu' caldo a secco e' \"" + hottestName + "\": "
                        + juce::String (hottestDry, 3));
            expectEquals (hottestName, juce::String ("Init"),
                          "nessun preset dovrebbe essere piu' caldo dei default di fabbrica");
        }

        beginTest ("drive: il knob parte da niente, senza gradino all'innesco");
        {
            // Il difetto che questo test blocca: il cancello `driveGain_ > 1.0f` faceva passare
            // la saturazione da spenta a piena forza fra 0.00 e 0.01 dB di `drive`, e siccome la
            // curva non e' l'identita' sul segnale che le arriva (saturate(1.0) = 0.778) il
            // risultato era un **gradino di -0.94 dB di RMS nel nulla**, seguito dai primi due
            // decibel di corsa spesi solo a riemergere dalla buca. Un knob che, girato di un
            // capello, abbassa.
            //
            // Adesso la saturazione entra in dissolvenza (SynthVoice::driveMix_, kDriveFadeGain):
            // a 0.001 dB l'uscita e' ancora quella secca a meno di un millesimo di decibel, e
            // l'avvallamento residuo a meta' dissolvenza vale 0.06 dB su questo patch (0.13 su
            // uno piu' scuro, dove il filtro toglie le armoniche che la curva aggiunge).
            store.setActive (1);

            const auto rmsAtDrive = [&store, this] (double driveDb)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = defaultParams();
                p.level = 1.0f;
                p.driveGain = (float) juce::Decibels::decibelsToGain (driveDb);
                synth.setParams (p);

                return measureAtClipper (synth, { 60 }, 1.0f, 40, 10).rms;
            };

            const auto dry = rmsAtDrive (0.0);
            const auto justOn = rmsAtDrive (0.001);

            const auto stepDb = std::abs (juce::Decibels::gainToDecibels (justOn / dry));
            logMessage ("drive 0 -> 0.001 dB: " + juce::String (stepDb, 3) + " dB di gradino");
            expect (stepDb < 0.05,
                    "l'innesco di drive costa " + juce::String (stepDb, 2)
                        + " dB: il cancello e' tornato a essere un gradino");

            // L'avvallamento residuo lungo la dissolvenza. Non puo' essere zero — la saturazione
            // *e'* una riduzione di picco, e finche' il guadagno non ha recuperato quello che la
            // curva toglie la somma sta sotto il secco — ma deve restare sotto la risoluzione di
            // un ascolto. Misurato 0.06 dB; la soglia sta a 0.3, non a 0.9, perche' un ritorno
            // al cancello secco varrebbe 0.94 e deve restare fuori con un margine chiaro.
            double worstDipDb = 0.0;

            for (const double driveDb : { 0.05, 0.1, 0.2, 0.3, 0.5, 0.75, 1.0, 1.5, 2.0 })
            {
                const auto dipDb = juce::Decibels::gainToDecibels (dry / rmsAtDrive (driveDb));
                worstDipDb = juce::jmax (worstDipDb, dipDb);
            }

            logMessage ("avvallamento massimo sulla dissolvenza: " + juce::String (worstDipDb, 3) + " dB");
            expect (worstDipDb < 0.3,
                    "la dissolvenza di drive lascia un avvallamento di " + juce::String (worstDipDb, 2) + " dB");
        }

        beginTest ("drive: la mappa quadratica distribuisce la corsa sulle due meta' del knob");
        {
            // La mappa era `linear 0..24`, e la corsa era tutta nella prima meta': da 12 a 24 dB
            // — **meta' del knob** — l'RMS di una nota saliva di 0.70 dB, mentre da 0 a 12 ne
            // saliva 4.78. Il tetto non era il problema (a 24 dB la saturazione e' un fatto
            // compiuto, non un'aggiunta): lo era la mappa, che spendeva meta' corsa dove non
            // succede piu' niente.
            //
            // Con `ms-squared 0..24` — la stessa mappa quadratica di att/dec/rel — meta' knob
            // sta a 6 dB invece che a 12, il tetto resta 24 e le due meta' della corsa valgono
            // quasi lo stesso numero di decibel. Il tetto si tiene perche' `drive` e' un target
            // del mod matrix: accorciarlo avrebbe cambiato anche cosa significa una route a
            // fondo corsa, che e' una decisione diversa da questa.
            constexpr auto* driveSpec = params::find ("drive");
            static_assert (driveSpec != nullptr, "drive non e' in ParameterTable.h");
            static_assert (driveSpec->map == params::Map::MsSquared,
                           "la corsa di drive dipende dalla mappa quadratica: se torna lineare, questo test non misura piu' quello che dice");

            expectWithinAbsoluteError (params::denormalise (*driveSpec, 0.5f), 6.0f, 0.01f,
                                       "meta' knob deve valere 6 dB, non 12");
            expectWithinAbsoluteError (params::denormalise (*driveSpec, 1.0f), 24.0f, 0.01f,
                                       "il tetto resta 24 dB");

            store.setActive (1);

            const auto rmsAtRaw = [&store, this] (float raw)
            {
                engine::SynthEngine synth;
                prepareEngine (synth, store);

                auto p = defaultParams();
                p.level = 1.0f;
                p.driveGain = params::driveGainFromRaw (raw);
                synth.setParams (p);

                return measureAtClipper (synth, { 60 }, 1.0f, 40, 10).rms;
            };

            const auto atZero = rmsAtRaw (0.0f);
            const auto atHalf = rmsAtRaw (0.5f);
            const auto atTop = rmsAtRaw (1.0f);

            const auto lowerHalfDb = juce::Decibels::gainToDecibels (atHalf / atZero);
            const auto upperHalfDb = juce::Decibels::gainToDecibels (atTop / atHalf);

            logMessage ("corsa di drive: prima meta' " + juce::String (lowerHalfDb, 2)
                        + " dB, seconda meta' " + juce::String (upperHalfDb, 2) + " dB");

            // Con la mappa lineare il rapporto fra le due meta' era 0.15 (0.70 contro 4.78).
            // Un terzo e' largamente sopra quel valore e largamente sotto uno: dice "le due
            // meta' fanno qualcosa di paragonabile", che e' la proprieta' voluta, senza
            // pretendere che siano uguali.
            expect (upperHalfDb > lowerHalfDb / 3.0f,
                    "la seconda meta' del knob vale " + juce::String (upperHalfDb, 2)
                        + " dB contro " + juce::String (lowerHalfDb, 2) + " della prima: la corsa e' di nuovo schiacciata");
        }
    }
};

static PresetGainStagingTests presetGainStagingTests;
