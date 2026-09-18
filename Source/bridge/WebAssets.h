#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace bridge::webAssets
{
/** true se il bundle Vite è compilato nel binario (XERUM_EMBED_WEBUI). */
bool embedded() noexcept;

/** Risorsa per un URL del resource provider ("/", "/assets/index-abc.js"...). */
std::optional<juce::WebBrowserComponent::Resource> lookup (const juce::String& url);
} // namespace bridge::webAssets
