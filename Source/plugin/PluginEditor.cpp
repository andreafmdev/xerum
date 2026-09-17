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
    <p><code>cd WebUI && npm run dev</code></p>
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
      webView_ (makeWebOptions())
{
    juce::ignoreUnused (processorRef_);

    addAndMakeVisible (webView_);

    setSize (kDefaultWidth, kDefaultHeight);
    setResizable (true, true);
    setResizeLimits (640, 400, 1920, 1200);

    if (kUseDevServer)
        webView_.goToURL (kDevServerUrl);
    else
        webView_.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
}

void SerumStyleSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff12141a));
}

void SerumStyleSynthAudioProcessorEditor::resized()
{
    webView_.setBounds (getLocalBounds());
}
