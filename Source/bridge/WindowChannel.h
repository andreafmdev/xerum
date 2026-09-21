#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace bridge
{
/**
 * Cio' che serviva gratis dalla barra del titolo nativa, che nello Standalone e' invisibile
 * (XerumWindowMac.h la rende chromeless ma resta nativa: semaforo, full screen, snap). Qui la UI
 * web recupera trascinamento, doppio clic per lo zoom e lo spazio da lasciare libero perche' il
 * semaforo non copra il logo.
 *
 * Native function: getWindowChrome() -> { trafficLightWidth }, beginWindowDrag() -> bool (vero
 * se il trascinamento nativo e' partito), moveWindowBy(dx, dy) (il ripiego quando non parte),
 * toggleWindowZoom() (il doppio clic).
 *
 * Nessun ChangeListener: a differenza di AudioSettingsChannel e MidiDeviceChannel non c'e' niente
 * da osservare, il semaforo non cambia larghezza da solo mentre l'app gira.
 *
 * Ha senso solo nello Standalone macOS: in AU/VST3 (e su altre piattaforme) nativeViewHandle()
 * torna nullptr e le quattro native function restano inerti — beginWindowDrag torna false,
 * moveWindowBy e toggleWindowZoom non fanno niente, getWindowChrome risponde
 * trafficLightWidth: 0. E' lo stesso cancello che deviceManager() usa in AudioSettingsChannel e
 * MidiDeviceChannel, qui sul wrapperType invece che sull'AudioDeviceManager.
 */
class WindowChannel final
{
public:
    explicit WindowChannel (juce::AudioProcessor& processor);

    /** Registra le native function nelle Options. Va chiamata prima di costruire la WebView. */
    juce::WebBrowserComponent::Options applyTo (juce::WebBrowserComponent::Options options);

    /** La WebView da cui risalire alla finestra; nullptr la disattiva. */
    void setWebView (juce::WebBrowserComponent* view) noexcept { webView_ = view; }

private:
    /** L'NSView della WebView, da cui XerumWindowMac risale alla finestra. Nullo finche' la
        WebView non e' sul desktop, fuori da macOS, e in tutti i formati che non sono lo
        Standalone — vedi il commento sulla classe sul perche' del controllo su wrapperType. */
    void* nativeViewHandle() const noexcept;

    juce::AudioProcessor& processor_;
    juce::WebBrowserComponent* webView_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowChannel)
};
} // namespace bridge
