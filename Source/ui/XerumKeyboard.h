#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace ui
{
/** La MidiKeyboardComponent di JUCE riskinnata sui token del chassis web (dark only).

    Ridisegna tasti bianchi e tasti neri; gli angoli inferiori vengono
    mascherati col colore di sfondo della finestra così la striscia chiude il corpo
    del pannello come fa `.sx-chassis` (vedi WebUI/packages/ui/src/theme.css).

    La palette vive qui dentro: l'editor non imposta più i ColourId della classe base.
    Restano usati solo i tre ColourId che servono al disegno del fondo — quello sì
    `final` in MidiKeyboardComponent — impostati nel costruttore.
*/
class XerumKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit XerumKeyboard (juce::MidiKeyboardState& stateToUse);

    /** Raggio degli angoli inferiori, in pixel. L'editor lo tiene allineato allo
        chassis (14 px a scala 1) moltiplicandolo per la scala corrente. */
    void setBottomCornerRadius (float radiusPx) noexcept;

private:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int midiNoteNumber, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;
    void paintOverChildren (juce::Graphics&) override;

    float bottomCornerRadius_ { 14.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XerumKeyboard)
};
} // namespace ui
