#include "bridge/WindowChannel.h"

#if JUCE_MAC
 #include "standalone/XerumWindowMac.h"
#endif

namespace bridge
{
WindowChannel::WindowChannel (juce::AudioProcessor& processor)
    : processor_ (processor)
{
}

void* WindowChannel::nativeViewHandle() const noexcept
{
   #if JUCE_MAC
    // Come deviceManager() in AudioSettingsChannel/MidiDeviceChannel: in AU/VST3 la WebView ha
    // comunque un peer (e' a schermo dentro la finestra dell'host), quindi il solo controllo su
    // getPeer() non basterebbe a tenere le native function inerti fuori dallo Standalone.
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (webView_ != nullptr)
            if (auto* peer = webView_->getPeer())
                return peer->getNativeHandle();
   #endif

    return nullptr;
}

juce::WebBrowserComponent::Options WindowChannel::applyTo (juce::WebBrowserComponent::Options options)
{
    return options
        .withNativeFunction ("getWindowChrome",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 auto* obj = new juce::DynamicObject();
                                 double width = 0.0;
                                // L'header (bridge/WindowChannel.h) include XerumWindowMac.h solo sotto JUCE_MAC,
                                // ma senza guardare anche QUESTA chiamata il simbolo xerum::trafficLightWidth
                                // resterebbe visto dal compilatore fuori da macOS pur non essendo mai dichiarato:
                                // errore di compilazione, non comportamento scorretto — nativeViewHandle() torna
                                // gia' nullptr ovunque fuori da JUCE_MAC (M1 della review finale).
                                #if JUCE_MAC
                                 if (nativeViewHandle() != nullptr)
                                     width = xerum::trafficLightWidth (nativeViewHandle());
                                #endif
                                 obj->setProperty ("trafficLightWidth", width);
                                 done (juce::var (obj));
                             })
        .withNativeFunction ("beginWindowDrag",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                #if JUCE_MAC
                                 done (juce::var (nativeViewHandle() != nullptr
                                                  && xerum::beginNativeWindowDrag (nativeViewHandle())));
                                #else
                                 done (juce::var (false));
                                #endif
                             })
        .withNativeFunction ("moveWindowBy",
                             [this] (const juce::Array<juce::var>& args,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                 // Il ripiego per quando beginWindowDrag non e' partito (Task 7): la pagina segue
                                 // i mousemove e chiede qui lo spostamento, in coordinate JUCE cross-platform —
                                 // niente XerumWindowMac, la finestra e' quella che getTopLevelComponent() da'
                                 // gia' a disposizione. Stesso cancello delle altre tre: inerte fuori dallo
                                 // Standalone, altrimenti in AU/VST3 sposterebbe la finestra dell'host.
                                 if (nativeViewHandle() != nullptr && args.size() >= 2 && webView_ != nullptr)
                                     if (auto* window = webView_->getTopLevelComponent())
                                         window->setBounds (window->getBounds().translated ((int) args[0], (int) args[1]));

                                 done (juce::var());
                             })
        .withNativeFunction ("toggleWindowZoom",
                             [this] (const juce::Array<juce::var>&,
                                     juce::WebBrowserComponent::NativeFunctionCompletion done)
                             {
                                #if JUCE_MAC
                                 if (nativeViewHandle() != nullptr)
                                     xerum::toggleWindowZoom (nativeViewHandle());
                                #endif

                                 done (juce::var());
                             });
}
} // namespace bridge
