#include "dsp/StereoDelay.h"
#include "plugin/PluginProcessor.h"
#include "state/StateTree.h"

#include <juce_audio_processors/juce_audio_processors.h>

/**
 * Il round-trip dello stato salvato attraverso il processore vero: getStateInformation ->
 * setStateInformation, con i tre effetti che setStateInformation promette (parametri dal file,
 * MODS/ARP ricreati, parametro assente al default). Il processore e' compilato senza editor
 * (XERUM_HEADLESS_TESTS), quindi niente WebView e niente juce_gui_extra.
 */
struct PluginProcessorTests final : public juce::UnitTest
{
    PluginProcessorTests() : juce::UnitTest ("PluginProcessor: stato salvato", "plugin") {}

    /** getXmlFromBinary/copyXmlToBinary sono protette e statiche: un tipo derivato le rende
        chiamabili senza istanziare niente. */
    struct Peek : juce::AudioProcessor
    {
        using AudioProcessor::copyXmlToBinary;
        using AudioProcessor::getXmlFromBinary;
    };

    static juce::MemoryBlock save (XerumAudioProcessor& p)
    {
        juce::MemoryBlock block;
        p.getStateInformation (block);
        return block;
    }

    static juce::var modVar (const char* src, const char* target, double depth)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("src", src);
        o->setProperty ("target", target);
        o->setProperty ("depth", depth);
        return juce::var (o);
    }

    void runTest() override
    {
        beginTest ("getState -> setState riporta parametri, mod matrix e pattern dell'arp");
        {
            XerumAudioProcessor a;
            auto& apvts = a.getAPVTS();
            auto* cutoff = apvts.getParameter ("cutoff");
            expect (cutoff != nullptr);
            cutoff->setValueNotifyingHost (0.25f);

            juce::Array<juce::var> mods;
            mods.add (modVar ("env", "cutoff", 0.6));
            state::setMods (apvts.state, juce::var (mods), nullptr);

            juce::Array<juce::var> steps;
            for (int i = 0; i < state::kArpSteps; ++i)
                steps.add (i % 2 == 0 ? 0.9 : 0.0);
            state::setArpSteps (apvts.state, juce::var (steps), nullptr);

            const auto saved = save (a);

            XerumAudioProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());

            expectWithinAbsoluteError (b.getAPVTS().getParameter ("cutoff")->getValue(), 0.25f, 1.0e-6f);

            const auto out = state::toVar (b.getAPVTS().state, "t");
            const auto* list = out["mods"].getArray();
            expect (list != nullptr && list->size() == 1, "una assegnazione, come salvata");
            expectEquals ((*list)[0]["target"].toString(), juce::String ("cutoff"));
            expectWithinAbsoluteError ((double) (*list)[0]["depth"], 0.6, 1.0e-9);

            const auto* arp = out["arpSteps"].getArray();
            expect (arp != nullptr && arp->size() == state::kArpSteps);
            expectWithinAbsoluteError ((double) (*arp)[0], 0.9, 1.0e-9);
            expectWithinAbsoluteError ((double) (*arp)[1], 0.0, 1.0e-9);

            // L'albero e' lo stesso, nodo per nodo: nessun residuo e niente di perso. Quello di `b`
            // porta la versione di schema sulla radice (la stampa getStateInformation): si confronta
            // con la copia stampata di `a`, non con il suo albero vivo.
            auto expected = a.getAPVTS().copyState();
            state::stampSchemaVersion (expected);
            expect (b.getAPVTS().copyState().isEquivalentTo (expected));
        }

        beginTest ("la versione di schema e' stampata sulla radice dello stato salvato");
        {
            XerumAudioProcessor a;
            const auto saved = save (a);
            const auto xml = Peek::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            expect (xml != nullptr);
            expectEquals (xml->getIntAttribute ("schemaVersion", -1), state::kStateVersion);
        }

        beginTest ("un parametro assente dallo stato salvato torna al default, dentro il processore");
        {
            XerumAudioProcessor a;
            a.getAPVTS().getParameter ("cutoff")->setValueNotifyingHost (0.25f);
            a.getAPVTS().getParameter ("res")->setValueNotifyingHost (0.9f);
            const auto saved = save (a);

            // Lo stesso file senza il PARAM di `res`: un progetto salvato prima che il parametro esistesse.
            auto xml = Peek::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            expect (xml != nullptr);
            auto* res = xml->getChildByAttribute ("id", "res");
            expect (res != nullptr);
            xml->removeChildElement (res, true);

            juce::MemoryBlock trimmed;
            Peek::copyXmlToBinary (*xml, trimmed);

            XerumAudioProcessor b;
            b.getAPVTS().getParameter ("res")->setValueNotifyingHost (0.9f); // il "valore corrente" da non tenere
            b.setStateInformation (trimmed.getData(), (int) trimmed.getSize());

            expectWithinAbsoluteError (b.getAPVTS().getParameter ("cutoff")->getValue(), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (b.getAPVTS().getParameter ("res")->getValue(),
                                       b.getAPVTS().getParameter ("res")->getDefaultValue(), 1.0e-6f);
        }

        beginTest ("la coda dichiarata cresce con il delay acceso e torna alla costante da spento");
        {
            XerumAudioProcessor a;
            expectWithinAbsoluteError (a.getTailLengthSeconds(), engine::SynthEngine::kDeclaredTailSeconds, 1.0e-9);

            a.getAPVTS().getParameter ("fx3On")->setValueNotifyingHost (1.0f);
            a.getAPVTS().getParameter ("dlTime")->setValueNotifyingHost (1.0f);      // 2 s
            a.getAPVTS().getParameter ("dlFeedback")->setValueNotifyingHost (1.0f);  // 90 %
            expect (a.getTailLengthSeconds() > engine::SynthEngine::kDeclaredTailSeconds, "il delay lungo deve allungare la coda");
            expect (a.getTailLengthSeconds() <= dsp::StereoDelay::kMaxTailSeconds + 1.0e-9);

            a.getAPVTS().getParameter ("fx3On")->setValueNotifyingHost (0.0f);
            expectWithinAbsoluteError (a.getTailLengthSeconds(), engine::SynthEngine::kDeclaredTailSeconds, 1.0e-9);
        }

        beginTest ("uno stato senza MODS e ARP li riceve vuoti, non manca niente dopo");
        {
            XerumAudioProcessor a;
            auto xml = Peek::getXmlFromBinary (save (a).getData(), (int) save (a).getSize());
            expect (xml != nullptr);
            xml->deleteAllChildElementsWithTagName ("MODS");
            xml->deleteAllChildElementsWithTagName ("ARP");

            juce::MemoryBlock old;
            Peek::copyXmlToBinary (*xml, old);

            XerumAudioProcessor b;
            b.setStateInformation (old.getData(), (int) old.getSize());
            expect (b.getAPVTS().state.getChildWithName (state::ids::MODS).isValid());
            expect (b.getAPVTS().state.getChildWithName (state::ids::ARP).isValid());
        }
    }
};

static PluginProcessorTests pluginProcessorTests;
