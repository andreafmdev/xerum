#include "plugin/PluginEditor.h"

#include "bridge/WebAssets.h"

namespace
{
constexpr const char* kDevServerUrl = "http://localhost:5173";

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
      keyboard_ (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // Gli attachment vanno creati dopo la WebView, mai prima.
    relays_.attach (processorRef_.getAPVTS());
    stateChannel_.setWebView (&webView_);

    addAndMakeVisible (webView_);
    addAndMakeVisible (keyboard_);
    configureKeyboard();

    setSize (kDefaultWidth, kDefaultHeight);
    setResizable (true, true);
    setResizeLimits (640, 400 + kKeyboardHeight, 1920, 1200);

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
        webView_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    }
    else
    {
        // XERUM_WEBUI_URL overrides the dev server (e.g. when 5173 is taken).
        const auto url = juce::SystemStats::getEnvironmentVariable ("XERUM_WEBUI_URL", kDevServerUrl);
        webView_.goToURL (url);
    }
}

SerumStyleSynthAudioProcessorEditor::~SerumStyleSynthAudioProcessorEditor()
{
    // stateChannel_ è distrutto dopo webView_ (è dichiarato prima): sgancia il puntatore qui.
    stateChannel_.setWebView (nullptr);
}

void SerumStyleSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff12141a));
}

void SerumStyleSynthAudioProcessorEditor::configureKeyboard()
{
    using KC = juce::MidiKeyboardComponent;

    // Theme colours mirror WebUI/packages/ui/src/theme.css (dark only).
    const auto background = juce::Colour (0xff0e1016);
    const auto surface0 = juce::Colour (0xff12151d);
    const auto surface1 = juce::Colour (0xff171a24);
    const auto line = juce::Colour (0xff2a3144);
    const auto text = juce::Colour (0xffe9ecf5);
    const auto muted = juce::Colour (0xff9aa3b8);
    const auto accent = juce::Colour (0xff6ee7c5);

    keyboard_.setColour (KC::whiteNoteColourId, text);
    keyboard_.setColour (KC::blackNoteColourId, surface0);
    keyboard_.setColour (KC::keySeparatorLineColourId, line);
    keyboard_.setColour (KC::mouseOverKeyOverlayColourId, accent.withAlpha (0.35f));
    keyboard_.setColour (KC::keyDownOverlayColourId, accent.withAlpha (0.85f));
    keyboard_.setColour (KC::textLabelColourId, muted);
    keyboard_.setColour (KC::shadowColourId, background.withAlpha (0.4f));
    keyboard_.setColour (KC::upDownButtonBackgroundColourId, surface1);
    keyboard_.setColour (KC::upDownButtonArrowColourId, text);

    // Five octaves fit the window width; no scroll buttons needed.
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
    auto bounds = getLocalBounds();
    auto keyboardArea = bounds.removeFromBottom (kKeyboardHeight);

    keyboard_.setKeyWidth (static_cast<float> (keyboardArea.getWidth()) / kWhiteKeysVisible);
    keyboard_.setBounds (keyboardArea);
    webView_.setBounds (bounds);
}
