#include "bridge/WebAssets.h"

#if XERUM_EMBED_WEBUI
 #include "BinaryData.h"   // generato da juce_add_binary_data, namespace WebUIAssets
#endif

namespace bridge::webAssets
{
bool embedded() noexcept
{
   #if XERUM_EMBED_WEBUI
    return true;
   #else
    return false;
   #endif
}

#if XERUM_EMBED_WEBUI
static const char* mimeFor (const juce::String& name)
{
    if (name.endsWithIgnoreCase (".html"))  return "text/html";
    if (name.endsWithIgnoreCase (".js"))    return "text/javascript";
    if (name.endsWithIgnoreCase (".css"))   return "text/css";
    if (name.endsWithIgnoreCase (".json"))  return "application/json";
    if (name.endsWithIgnoreCase (".svg"))   return "image/svg+xml";
    if (name.endsWithIgnoreCase (".png"))   return "image/png";
    if (name.endsWithIgnoreCase (".woff2")) return "font/woff2";
    if (name.endsWithIgnoreCase (".woff"))  return "font/woff";
    return "application/octet-stream";
}
#endif

std::optional<juce::WebBrowserComponent::Resource> lookup (const juce::String& url)
{
   #if XERUM_EMBED_WEBUI
    // Vite emette nomi con hash unici: basta il basename. "/" → index.html.
    auto name = url.fromLastOccurrenceOf ("/", false, false);

    if (name.isEmpty())
        name = "index.html";

    for (int i = 0; i < WebUIAssets::namedResourceListSize; ++i)
    {
        if (juce::String (WebUIAssets::originalFilenames[i]) != name)
            continue;

        int size = 0;
        const auto* data = WebUIAssets::getNamedResource (WebUIAssets::namedResourceList[i], size);

        if (data == nullptr || size <= 0)
            return std::nullopt;

        const auto* bytes = reinterpret_cast<const std::byte*> (data);

        juce::WebBrowserComponent::Resource resource;
        resource.data.assign (bytes, bytes + size);
        resource.mimeType = mimeFor (name);
        return resource;
    }

    return std::nullopt;
   #else
    juce::ignoreUnused (url);
    return std::nullopt;
   #endif
}
} // namespace bridge::webAssets
