/**
 * L'app dello Standalone, al posto di quella che genera JUCE.
 *
 * Perche' esiste: juce::StandaloneFilterWindow e' una DocumentWindow con barra del titolo e
 * bottone Options (juce_StandaloneFilterWindow.h:653-666), e Options e' esattamente cio' che
 * questa feature porta dentro la WebUI. Qui si tiene tutto il resto — StandalonePluginHolder
 * invariato, che e' il pezzo che apre l'audio, apre il MIDI e salva le impostazioni — e si
 * cambia solo la finestra.
 *
 * Il prezzo: questo file sostituisce codice che mantiene JUCE, e con esso si eredita l'obbligo
 * di rifare a mano tutto quello che StandaloneFilterWindow/StandaloneFilterApp facevano di
 * nascosto. Ogni blocco qui sotto cita la riga JUCE che replica, perche' quello e' l'unico modo
 * di accorgersi di una regressione: aggiornando il submodule non c'e' nessun test a coprirci —
 * la finestra e' GUI, i test sono headless.
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

#include <cstdio>
#define XDBG(...) do { std::fprintf (stderr, "XDBG " __VA_ARGS__); std::fprintf (stderr, "\n"); std::fflush (stderr); } while (0)

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
        //
        // Il puntatore va tenuto: serve per editorBeingDeleted() nel distruttore. setContentOwned
        // ne prende la proprieta', quindi editor_ e' un osservatore, non un owner.
        editor_ = holder_->processor->createEditorAndMakeActive();
        setContentOwned (editor_, true);

        // Il vincolo altezza/larghezza vive sull'editor (ChassisConstrainer in PluginEditor.cpp).
        // JUCE lo traduce per la finestra con un DecoratorConstrainer perche' la sua finestra ha
        // una barra del titolo alta; qui il contenuto riempie la finestra intera — native title
        // bar piu' fullSizeContentView — quindi lo stesso constrainer si applica diretto.
        setResizable (true, false);
        setConstrainer (editor_->getConstrainer());

        // restoreWindowStateFromString restituisce false quando la stringa e' vuota o illeggibile
        // e in quel caso NON tocca i bounds (juce_ResizableWindow.h:237). Al primo avvio e' sempre
        // cosi', perche' "windowState" lo scrive solo il nostro distruttore: senza questo ramo la
        // finestra si aprirebbe a 0,0, incastrata nell'angolo in alto a sinistra.
        if (! restoreWindowStateFromString (settings_ != nullptr ? settings_->getValue ("windowState")
                                                                 : juce::String()))
            centreWithSize (getWidth(), getHeight());

        // Il peer nasce con addToDesktop, che TopLevelWindow chiamerebbe da solo alla prima
        // setVisible(true). Anticiparlo (juce_TopLevelWindow.h:150) serve a togliere il chrome
        // mentre la finestra e' ancora nascosta: altrimenti esiste un frame, visibile a occhio
        // nudo all'avvio, in cui la barra del titolo c'e'.
        addToDesktop();
        applyChromelessLook();

        setVisible (true);

        // Togliere la barra ridisegna il contenuto ~28 pt piu' alto, e il peer scrive i bounds
        // diretti senza passare dal constrainer (juce_ComponentPeer.cpp:321-347): senza questa
        // riga l'editor resta a 900x708 mentre ChassisConstrainer vuole heightForWidth(900)==680.
        // Rimetterlo in riga qui rende anche irrilevante l'ordine fra setContentOwned e
        // setConstrainer piu' sopra.
        setBoundsConstrained (getBounds());
    }

    ~StandaloneWindow() override
    {
        // Il constrainer e' un unique_ptr dentro l'editor, che fra due righe muore: se resta
        // agganciato, ResizableWindow e il peer se lo portano dietro pendente per tutto il resto
        // della distruzione. Staccarlo per primo e' l'unica riga che deve venire prima di tutte.
        XDBG ("~StandaloneWindow ENTER settings_=%p", (void*) settings_);
        setConstrainer (nullptr);

        if (settings_ != nullptr)
        {
            const auto state = getWindowStateAsString();
            XDBG ("~StandaloneWindow state=\"%s\"", state.toRawUTF8());
            settings_->setValue ("windowState", state);
            XDBG ("~StandaloneWindow readback=\"%s\"", settings_->getValue ("windowState").toRawUTF8());
        }

        // L'ordine e' quello di ~StandaloneFilterWindow (juce_StandaloneFilterWindow.h:750-759):
        // prima si stacca l'AudioProcessorPlayer, poi si smonta la UI. Al contrario, la WebView, i
        // relay e il MeterChannel verrebbero distrutti mentre processBlock e' ancora in corso.
        holder_->stopPlaying();

        // ~AudioProcessorEditor asserisce processor.getActiveEditor() != this e NON pulisce da
        // solo il puntatore (juce_AudioProcessorEditor.cpp:50-56): tocca al wrapper, ed e' quello
        // che JUCE fa a mano in ~MainContentComponent (juce_StandaloneFilterWindow.h:881-885).
        // Senza, in Debug partono due jassert a ogni uscita e in Release il processore attraversa
        // lo spegnimento dell'audio con un activeEditor pendente.
        if (editor_ != nullptr)
        {
            /* NEGATIVE CONTROL: editorBeingDeleted deliberately removed */
            editor_ = nullptr;
        }

        clearContentComponent();
        holder_ = nullptr;
        XDBG ("~StandaloneWindow EXIT");
    }

    /** Lo stato del plugin non lo salva nessun distruttore: savePluginState() e' chiamata solo
        dalla chiusura della finestra (juce_StandaloneFilterWindow.h:784) e da
        systemRequestedQuit (juce_audio_plugin_client_Standalone.cpp:155-157). reloadPluginState()
        invece gira sempre all'avvio, dentro l'holder — quindi saltare il salvataggio non da'
        nessun errore: riapre in eterno l'ultimo "filterState" buono e butta via ogni modifica. */
    void savePluginState()
    {
        if (holder_ != nullptr)
            holder_->savePluginState();
    }

    void closeButtonPressed() override
    {
        savePluginState();
        juce::JUCEApplicationBase::quit();
    }

    /** L'unico aggancio pubblico da cui accorgersi che macOS ha rimesso lo styleMask com'era.
        Uscire dal full screen fa scattare windowDidExitFullScreen: ->
        NSViewComponentPeer::resetWindowPresentation(), che ASSEGNA lo styleMask ricavandolo dai
        soli flag di JUCE (juce_NSViewComponentPeer_mac.mm:1613-1622, chiamata da riga 2822): fra
        quei flag NSWindowStyleMaskFullSizeContentView non c'e', quindi la striscia della barra
        del titolo ricompare e resta fino al riavvio.

        Perche' resized() basta: i bounds del peer sono il frame della NSView, cioe' della content
        view (juce_NSViewComponentPeer_mac.mm:411-428). Togliere FullSizeContentView rimpicciolisce
        la content view di ~28 pt anche a frame di finestra invariato, la view notifica il cambio
        (frameChangedSelector -> redirectMovedOrResized) e si arriva qui. Rimetterlo la riallarga e
        ci ripassa una seconda volta, ma makeWindowChromeless e' idempotente — `|=` sulla maschera —
        quindi al secondo giro non cambia nulla e la catena si ferma. */
    void resized() override
    {
        DocumentWindow::resized();
        applyChromelessLook();
    }

private:
    void applyChromelessLook()
    {
        if (auto* peer = getPeer())
            xerum::makeWindowChromeless (peer->getNativeHandle());
    }

    juce::PropertySet* settings_;
    std::unique_ptr<juce::StandalonePluginHolder> holder_;

    /** Osservatore, non proprietario: la proprieta' e' di setContentOwned. Serve solo per
        editorBeingDeleted(). */
    juce::AudioProcessorEditor* editor_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneWindow)
};

class StandaloneApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
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

        // Stessa guardia di createWindow() (juce_audio_plugin_client_Standalone.cpp:90-97): senza
        // schermi la finestra non si puo' creare. JUCE in quel caso tiene in vita un holder
        // headless per l'Inter-App Audio di iOS; qui non serve — lo Standalone e' solo macOS — e
        // un holder senza finestra sarebbe solo un motore audio che nessuno puo' fermare.
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            jassertfalse;
            return;
        }

        window_ = std::make_unique<StandaloneWindow> (getApplicationName(), properties_.getUserSettings());
    }

    void shutdown() override
    {
        XDBG ("shutdown ENTER window_=%p", (void*) window_.get());
        window_ = nullptr;
        XDBG ("shutdown saveIfNeeded, user=%p", (void*) properties_.getUserSettings());
        XDBG ("shutdown readback=\"%s\"", properties_.getUserSettings()->getValue ("windowState").toRawUTF8());
        properties_.saveIfNeeded();
        XDBG ("shutdown EXIT");
    }

    /** Ricalcato su StandaloneFilterApp::systemRequestedQuit
        (juce_audio_plugin_client_Standalone.cpp:153-175). I due pezzi da non perdere: il
        salvataggio dello stato PRIMA di qualunque uscita, e il rinvio di 100 ms quando c'era
        roba modale da chiudere — cancelAllModalComponents() la chiude in modo asincrono, quindi
        uscire subito significherebbe distruggerla a meta'. */
    void systemRequestedQuit() override
    {
        XDBG ("systemRequestedQuit ENTER window_=%p", (void*) window_.get());

        if (window_ != nullptr)
            window_->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            XDBG ("systemRequestedQuit -> modal cancelled, retry in 100ms");
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            XDBG ("systemRequestedQuit -> quit()");
            quit();
        }
    }

private:
    juce::ApplicationProperties properties_;
    std::unique_ptr<StandaloneWindow> window_;
};
} // namespace xerum

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new xerum::StandaloneApp(); }
