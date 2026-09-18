#include "plugin/PluginEditor.h"

namespace
{
#if JUCE_DEBUG
constexpr bool kUseDevServer = true;
#else
constexpr bool kUseDevServer = false;
#endif

constexpr const char* kDevServerUrl = "http://localhost:5173";

juce::WebBrowserComponent::Resource makeFallbackIndexHtml()
{
    static constexpr char html[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8"/>
  <title>SerumStyleSynth</title>
  <style>
    html, body { margin: 0; height: 100%; background: #12141a; color: #e8eaf0;
      font-family: ui-sans-serif, system-ui, sans-serif; display: grid; place-items: center; }
    main { text-align: center; max-width: 28rem; padding: 2rem; }
    h1 { font-weight: 600; letter-spacing: 0.02em; margin-bottom: 0.5rem; }
    p { opacity: 0.7; line-height: 1.5; }
    code { background: #1c2030; padding: 0.15rem 0.4rem; border-radius: 4px; }
  </style>
</head>
<body>
  <main>
    <h1>SerumStyleSynth</h1>
    <p>Web UI scaffold. Start the Vite dev server:</p>
    <p><code>cd WebUI && pnpm dev</code></p>
    <p>then reopen the editor.</p>
  </main>
</body>
</html>
)HTML";

    juce::WebBrowserComponent::Resource resource;
    const auto* data = reinterpret_cast<const std::byte*> (html);
    resource.data.assign (data, data + sizeof (html) - 1);
    resource.mimeType = "text/html";
    return resource;
}

juce::WebBrowserComponent::Options makeWebOptions()
{
    auto options = juce::WebBrowserComponent::Options {}
                       .withNativeIntegrationEnabled()
                       .withResourceProvider ([] (const auto& url)
                                              -> std::optional<juce::WebBrowserComponent::Resource>
                       {
                           if (url == "/" || url == "/index.html"
                               || url.endsWithIgnoreCase ("index.html"))
                               return makeFallbackIndexHtml();

                           return std::nullopt;
                       });

   #if JUCE_WINDOWS
    options = options
                  .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                  .withWinWebView2Options (
                      juce::WebBrowserComponent::Options::WinWebView2 {}.withUserDataFolder (
                          juce::File::getSpecialLocation (juce::File::tempDirectory)));
   #endif

    return options;
}
} // namespace

SerumStyleSynthAudioProcessorEditor::SerumStyleSynthAudioProcessorEditor (
    SerumStyleSynthAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef_ (p),
      webView_ (makeWebOptions()),
      keyboard_ (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (webView_);
    addAndMakeVisible (keyboard_);
    configureKeyboard();

    setSize (kDefaultWidth, kDefaultHeight);
    setResizable (true, true);
    setResizeLimits (640, 400 + kKeyboardHeight, 1920, 1200);

    if (kUseDevServer)
    {
        // XERUM_WEBUI_URL overrides the dev server (e.g. when 5173 is taken).
        const auto url = juce::SystemStats::getEnvironmentVariable ("XERUM_WEBUI_URL", kDevServerUrl);
        webView_.goToURL (url);
    }
    else
        webView_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
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
