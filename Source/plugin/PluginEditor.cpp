#include "plugin/PluginEditor.h"

#include "bridge/WebAssets.h"

#include <cmath>
#include <memory>

namespace
{
constexpr const char* kDevServerUrl = "http://localhost:5173";

// Lo chassis della WebUI è 900×680 e si scala per riempire la WebView (SynthWindow.tsx).
// Qui la larghezza della finestra è l'unica variabile libera: da lei derivano la scala e
// l'altezza della WebView, che è l'intero editor.
constexpr int kChassisWidth = 900;
// 680 = H in SynthWindow.tsx (WebUI/src/synth/ui/SynthWindow.tsx): 670 px che i figli dello
// chassis occupano davvero piu' 10 di respiro in fondo — l'aritmetica sta nel commento di H.
// Terza copia dello stesso numero, dopo H e la regola .sx-chassis in synth.css: la sorgente di
// verita' resta H, questa costante la segue.
constexpr int kChassisHeight = 680;

// kMinScale e' il pavimento della FINESTRA, non della scala dello chassis: sotto 648x490 la UI
// non e' piu' leggibile. Non ha un gemello nella WebUI, e non deve averlo — la' sotto il minimo
// la cosa giusta e' continuare a rimpicciolire, non far traboccare lo chassis fuori dalla
// WebView. Vedi il commento di fitScale() in SynthWindow.tsx.
constexpr float kMinScale = 0.72f;
constexpr float kMaxScale = 1.5f;     // stesso tetto del fit lato web

// I tre numeri qui sopra che la WebUI ricalca — kChassisWidth/kChassisHeight/kMaxScale — sono
// riletti da questo sorgente dal test "le due formule della scala restano la stessa regola" in
// WebUI/src/synth/ui/SynthWindow.test.tsx, che poi dimostra su tutte le larghezze 648..1350 che
// fitScale(w, heightForWidth(w)) == w/kChassisWidth con meno di un pixel di scarto verticale.
// E' quello a tenere insieme le due copie: cambiare un numero qui e non l'altro la' fa fallire
// `pnpm test`, non una review.

float scaleForWidth (int width) noexcept
{
    return juce::jlimit (kMinScale, kMaxScale, static_cast<float> (width) / static_cast<float> (kChassisWidth));
}

int heightForWidth (int width) noexcept
{
    // La WebView e' l'intero editor: l'altezza e' quella dello chassis scalato, punto. Prima
    // qui si sommava la striscia nativa, che adesso vive dentro lo chassis.
    return static_cast<int> (std::ceil (kChassisHeight * scaleForWidth (width)));
}

int widthForScale (float scale) noexcept
{
    return juce::roundToInt (kChassisWidth * scale);
}

/** L'altezza è funzione della larghezza: qualunque trascinamento la ricalcola. */
class ChassisConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    void checkBounds (juce::Rectangle<int>& bounds,
                      const juce::Rectangle<int>& old,
                      const juce::Rectangle<int>& limits,
                      bool isStretchingTop,
                      bool isStretchingLeft,
                      bool isStretchingBottom,
                      bool isStretchingRight) override
    {
        juce::ComponentBoundsConstrainer::checkBounds (bounds, old, limits, isStretchingTop,
                                                       isStretchingLeft, isStretchingBottom, isStretchingRight);

        const auto height = heightForWidth (bounds.getWidth());

        if (isStretchingTop && ! isStretchingBottom)
            bounds.setTop (bounds.getBottom() - height);
        else
            bounds.setHeight (height);
    }
};

/** L'host web legge `gutter` dalla query: 0 = chassis a filo, niente margine. */
juce::String withGutterParam (juce::String url)
{
    return url + (url.containsChar ('?') ? "&" : "?") + "gutter=0";
}

juce::WebBrowserComponent::Options makeWebOptions (const bridge::WebRelays& relays,
                                                  bridge::StateChannel& stateChannel,
                                                  bridge::MidiChannel& midiChannel,
                                                  bridge::MidiDeviceChannel& midiDevices,
                                                  bridge::AudioSettingsChannel& audioSettings,
                                                  bridge::WindowChannel& window)
{
    auto options = juce::WebBrowserComponent::Options {}
                       .withNativeIntegrationEnabled()
                       // In Release il bundle Vite è dentro il binario: serve quello. In Debug
                       // la WebView va sul dev server e il provider non viene mai interrogato.
                       .withResourceProvider ([] (const auto& url) { return bridge::webAssets::lookup (url); });

   #if JUCE_WINDOWS
    options = options
                  .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options (
                      juce::WebBrowserComponent::Options::WinWebView2 {}.withUserDataFolder (
                          juce::File::getSpecialLocation (juce::File::tempDirectory)));
   #endif

    // I relay aggiungono a initialisationData la voce di ogni parametro.
    options = relays.applyTo (options);

    // Il canale di stato aggiunge getState / setMods / setArpSteps.
    options = stateChannel.applyTo (options);

    // Il canale MIDI aggiunge noteOn / noteOff / allNotesOff / setWheel.
    options = midiChannel.applyTo (options);

    // Gli ingressi MIDI del sistema: getMidiInputs / setMidiInputEnabled (solo Standalone).
    options = midiDevices.applyTo (options);

    // Il device audio: getAudioSettings / setAudioOutput / setSampleRate / setBufferSize
    // (solo Standalone).
    options = audioSettings.applyTo (options);

    // Trascinamento, zoom e spazio per il semaforo: getWindowChrome / beginWindowDrag /
    // moveWindowBy / toggleWindowZoom (solo Standalone macOS).
    options = window.applyTo (options);

    return options;
}
} // namespace

XerumAudioProcessorEditor::XerumAudioProcessorEditor (
    XerumAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef_ (p),
      stateChannel_ (p.getAPVTS(), p.getStateReplacedBroadcaster()),
      midiChannel_ (p.getKeyboardState(), p),
      midiDevices_ (p),
      audioSettings_ (p),
      window_ (p),
      webView_ (makeWebOptions (relays_, stateChannel_, midiChannel_, midiDevices_, audioSettings_, window_)),
      meters_ (p.getMeters(), webView_),
      constrainer_ (std::make_unique<ChassisConstrainer>())
{
    // Gli attachment vanno creati dopo la WebView, mai prima.
    relays_.attach (processorRef_.getAPVTS());
    stateChannel_.setWebView (&webView_);
    midiDevices_.setWebView (&webView_);
    audioSettings_.setWebView (&webView_);
    window_.setWebView (&webView_);

    addAndMakeVisible (webView_);

    setResizable (true, true);
    setConstrainer (constrainer_.get());
    constrainer_->setSizeLimits (widthForScale (kMinScale), heightForWidth (widthForScale (kMinScale)),
                                 widthForScale (kMaxScale), heightForWidth (widthForScale (kMaxScale)));
    setSize (kChassisWidth, heightForWidth (kChassisWidth));

    if (bridge::webAssets::embedded())
    {
        webView_.goToURL (withGutterParam (juce::WebBrowserComponent::getResourceProviderRoot()));
    }
    else
    {
        // XERUM_WEBUI_URL overrides the dev server (e.g. when 5173 is taken).
        const auto url = juce::SystemStats::getEnvironmentVariable ("XERUM_WEBUI_URL", kDevServerUrl);
        webView_.goToURL (withGutterParam (url));
    }
}

XerumAudioProcessorEditor::~XerumAudioProcessorEditor()
{
    // stateChannel_ è distrutto dopo webView_ (è dichiarato prima): sgancia il puntatore qui.
    stateChannel_.setWebView (nullptr);
    midiDevices_.setWebView (nullptr);
    audioSettings_.setWebView (nullptr);
    window_.setWebView (nullptr);
}

void XerumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e1016)); // --background (theme.css)
}

void XerumAudioProcessorEditor::resized()
{
    webView_.setBounds (getLocalBounds());
}
