#include "plugin/PluginEditor.h"

#include "bridge/WebAssets.h"

#include <cmath>
#include <memory>

namespace
{
constexpr const char* kDevServerUrl = "http://localhost:5173";

// Lo chassis della WebUI è 900×600 e si scala per riempire la WebView (SynthWindow.tsx).
// Qui la larghezza della finestra è l'unica variabile libera: da lei derivano la scala,
// l'altezza della WebView e quella della tastiera, così i due pezzi combaciano sempre.
constexpr int kChassisWidth = 900;
constexpr int kChassisHeight = 600;
constexpr int kKeyboardHeight = 78;   // a scala 1
constexpr float kChassisCorner = 14.0f;
constexpr float kMinScale = 0.72f;
constexpr float kMaxScale = 1.5f;     // stesso tetto del fit lato web

float scaleForWidth (int width) noexcept
{
    return juce::jlimit (kMinScale, kMaxScale, static_cast<float> (width) / static_cast<float> (kChassisWidth));
}

int webHeightForWidth (int width) noexcept
{
    // Arrotondato per eccesso: la WebView non deve mai essere più bassa dello chassis,
    // altrimenti il fit lato web rimpicciolisce e riaprirebbe il margine laterale.
    return static_cast<int> (std::ceil (kChassisHeight * scaleForWidth (width)));
}

int heightForWidth (int width) noexcept
{
    return webHeightForWidth (width) + juce::roundToInt (kKeyboardHeight * scaleForWidth (width));
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
                                                  bridge::StateChannel& stateChannel)
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

    return options;
}
} // namespace

SerumStyleSynthAudioProcessorEditor::SerumStyleSynthAudioProcessorEditor (
    SerumStyleSynthAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef_ (p),
      stateChannel_ (p.getAPVTS(), p.getStateReplacedBroadcaster()),
      webView_ (makeWebOptions (relays_, stateChannel_)),
      meters_ (p.getMeters(), webView_),
      keyboard_ (p.getKeyboardState()),
      constrainer_ (std::make_unique<ChassisConstrainer>())
{
    // Gli attachment vanno creati dopo la WebView, mai prima.
    relays_.attach (processorRef_.getAPVTS());
    stateChannel_.setWebView (&webView_);

    addAndMakeVisible (webView_);
    addAndMakeVisible (keyboard_);
    configureKeyboard();

    setResizable (true, true);
    setConstrainer (constrainer_.get());
    constrainer_->setSizeLimits (widthForScale (kMinScale), heightForWidth (widthForScale (kMinScale)),
                                 widthForScale (kMaxScale), heightForWidth (widthForScale (kMaxScale)));
    setSize (kChassisWidth, heightForWidth (kChassisWidth));

   #if JUCE_DEBUG
    // Dev aid: XERUM_SNAPSHOT=/path/out.png writes a snapshot of the JUCE-painted editor
    // (keyboard strip; the WebView is a native view and renders blank) ~2 s after opening.
    if (const auto snapshotPath = juce::SystemStats::getEnvironmentVariable ("XERUM_SNAPSHOT", {});
        snapshotPath.isNotEmpty())
    {
        juce::Timer::callAfterDelay (2000, [safeThis = juce::Component::SafePointer (this), snapshotPath]
        {
            if (safeThis == nullptr)
                return;

            const auto image = safeThis->createComponentSnapshot (safeThis->getLocalBounds(), true, 2.0f);
            juce::File (snapshotPath).deleteFile();
            juce::FileOutputStream out { juce::File (snapshotPath) };

            if (out.openedOk())
                juce::PNGImageFormat().writeImageToStream (image, out);
        });
    }
   #endif

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

SerumStyleSynthAudioProcessorEditor::~SerumStyleSynthAudioProcessorEditor()
{
    // stateChannel_ è distrutto dopo webView_ (è dichiarato prima): sgancia il puntatore qui.
    stateChannel_.setWebView (nullptr);
}

void SerumStyleSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0e1016)); // --background (theme.css)
}

void SerumStyleSynthAudioProcessorEditor::configureKeyboard()
{
    // La pelle (colori, gradienti, angoli) sta in ui::XerumKeyboard: qui solo il comportamento.
    keyboard_.setAvailableRange (kLowestNote, kHighestNote);
    keyboard_.setLowestVisibleKey (kLowestNote);
    keyboard_.setScrollButtonsVisible (false);
    keyboard_.setOctaveForMiddleC (4);
    keyboard_.setKeyPressBaseOctave (5);          // QWERTY row (A W S E D ...) plays from middle C
    keyboard_.setVelocity (0.8f, true);           // click height sets velocity
    keyboard_.setBlackNoteLengthProportion (0.62f);
}

void SerumStyleSynthAudioProcessorEditor::resized()
{
    const auto bounds = getLocalBounds();
    const auto scale = scaleForWidth (bounds.getWidth());
    const auto webHeight = webHeightForWidth (bounds.getWidth());

    webView_.setBounds (bounds.withHeight (webHeight));

    keyboard_.setKeyWidth (static_cast<float> (bounds.getWidth()) / static_cast<float> (kWhiteKeysVisible));
    keyboard_.setBottomCornerRadius (kChassisCorner * scale);
    keyboard_.setBounds (bounds.withTop (webHeight));
}
