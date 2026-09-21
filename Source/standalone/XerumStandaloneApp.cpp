/**
 * L'app dello Standalone, al posto di quella che genera JUCE.
 *
 * Perche' esiste: juce::StandaloneFilterWindow e' una DocumentWindow con barra del titolo e
 * bottone Options (juce_StandaloneFilterWindow.h:653-666), e Options e' esattamente cio' che
 * questa feature porta dentro la WebUI. Qui si tiene tutto il resto — StandalonePluginHolder
 * invariato, che e' il pezzo che apre l'audio, apre il MIDI e salva le impostazioni — e si
 * cambia solo la finestra.
 *
 * Il prezzo: questo file sostituisce codice che mantiene JUCE. Aggiornando il submodule, se
 * StandalonePluginHolder cambia se ne accorge il compilatore, non un test.
 */

// Ordine obbligato: juce_StandaloneFilterWindow.h non include nulla di suo tranne
// detail/juce_CreatePluginFilter.h, che a sua volta non include niente. Entrambi danno per
// scontato che chi li include abbia gia' aperto i moduli JUCE — e' quello che fa
// juce_audio_plugin_client_Standalone.cpp:45-53 prima di includerlo. Metterlo per primo
// (come farebbe l'ordine alfabetico) lo fa fallire su AudioProcessor e PluginHostType.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#include "standalone/XerumWindowMac.h"

namespace xerum
{
/** La finestra: una DocumentWindow con la native title bar ACCESA — e' lei a portare semaforo,
    full screen e snap — resa invisibile da makeWindowChromeless. Spegnerla significherebbe
    perderli e doverli riscrivere. */
class StandaloneWindow final : public juce::DocumentWindow
{
public:
    StandaloneWindow (const juce::String& title, juce::PropertySet* settings)
        : DocumentWindow (title, juce::Colour (0xff0e1016), DocumentWindow::allButtons),
          settings_ (settings)
    {
        setUsingNativeTitleBar (true);

        // `false` = NON prendere possesso delle settings: le possiede ApplicationProperties, e
        // lasciargliele cancellare sarebbe una doppia distruzione. Il default del costruttore e'
        // `true` (juce_StandaloneFilterWindow.h:79), quindi va scritto a mano.
        holder_ = std::make_unique<juce::StandalonePluginHolder> (settings, false);

        // createEditorAndMakeActive() sta sull'AudioProcessor, non sull'holder: e' da li' che lo
        // prende anche JUCE (juce_StandaloneFilterWindow.h:855). Il gemello createEditorIfNeeded()
        // fa la stessa identica chiamata ma in 9.0.2 e' marcato [[deprecated]]
        // (juce_audio_processors_headless/processors/juce_AudioProcessor.h:1062).
        auto* editor = holder_->processor->createEditorAndMakeActive();
        setContentOwned (editor, true);

        // Il vincolo altezza/larghezza vive sull'editor (ChassisConstrainer in PluginEditor.cpp).
        // JUCE lo traduce per la finestra con un DecoratorConstrainer perche' la sua finestra ha
        // una barra del titolo alta; qui il contenuto riempie la finestra intera — native title
        // bar piu' fullSizeContentView — quindi lo stesso constrainer si applica diretto.
        setResizable (true, false);
        setConstrainer (editor->getConstrainer());

        if (settings_ != nullptr)
            restoreWindowStateFromString (settings_->getValue ("windowState"));
        else
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
        xerum::makeWindowChromeless (getPeer() != nullptr ? getPeer()->getNativeHandle() : nullptr);
    }

    ~StandaloneWindow() override
    {
        if (settings_ != nullptr)
            settings_->setValue ("windowState", getWindowStateAsString());

        clearContentComponent();
        holder_ = nullptr;
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplicationBase::quit();
    }

private:
    juce::PropertySet* settings_;
    std::unique_ptr<juce::StandalonePluginHolder> holder_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneWindow)
};

class StandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";

        properties_.setStorageParameters (options);
        window_ = std::make_unique<StandaloneWindow> (JucePlugin_Name, properties_.getUserSettings());
    }

    void shutdown() override
    {
        window_ = nullptr;
        properties_.saveIfNeeded();
    }

    void systemRequestedQuit() override { quit(); }

private:
    juce::ApplicationProperties properties_;
    std::unique_ptr<StandaloneWindow> window_;
};
} // namespace xerum

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new xerum::StandaloneApp(); }
