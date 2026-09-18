#include "ui/XerumKeyboard.h"

namespace ui
{
namespace
{
// Palette: rispecchia WebUI/packages/ui/src/theme.css (solo tema scuro).
const juce::Colour kBackground  { 0xff0e1016 }; // --background
const juce::Colour kSeam        { 0xff2a3144 }; // linea di giunzione col chassis
const juce::Colour kEdgeDark    { 0xff090b10 }; // --color-edge-dark
const juce::Colour kAccent      { 0xff6ee7c5 }; // --primary
const juce::Colour kWhiteTop    { 0xfff2f4fa };
const juce::Colour kWhiteBottom { 0xffbcc3d6 };
const juce::Colour kBlackTop    { 0xff272d3d };
const juce::Colour kBlackBottom { 0xff0b0d12 };
const juce::Colour kLabel       { 0xff79829a };

/** Angolo inferiore del singolo tasto: appena accennato, come sui synth veri.
    Oltre i 2 px fra tasto e tasto si apre un dente scuro invece di una fessura. */
constexpr float kKeyCorner = 1.5f;

/** Altezza della fascia d'ombra che salda la tastiera al bordo dello chassis. */
constexpr float kSeamShadow = 6.0f;

juce::Path bottomRounded (juce::Rectangle<float> r, float radius)
{
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                           radius, radius, false, false, true, true);
    return p;
}

juce::ColourGradient verticalFill (juce::Colour top, juce::Colour bottom, juce::Rectangle<float> r)
{
    return { top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false };
}
} // namespace

XerumKeyboard::XerumKeyboard (juce::MidiKeyboardState& stateToUse)
    : juce::MidiKeyboardComponent (stateToUse, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // drawKeyboardBackground è final: il fondo si governa solo da questi tre ColourId.
    // Ombra e linea di giunzione le disegna paintOverChildren, quindi qui vanno spente.
    setColour (whiteNoteColourId, kBackground);
    setColour (shadowColourId, juce::Colours::transparentBlack);
    setColour (keySeparatorLineColourId, juce::Colours::transparentBlack);
}

void XerumKeyboard::setBottomCornerRadius (float radiusPx) noexcept
{
    const auto r = juce::jmax (0.0f, radiusPx);

    if (juce::approximatelyEqual (r, bottomCornerRadius_))
        return;

    bottomCornerRadius_ = r;
    repaint();
}

void XerumKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                                   bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour)
{
    // I colori arrivano dalla palette del file, non dai ColourId: ignoriamo quelli passati.
    juce::ignoreUnused (lineColour, textColour);

    const auto body = area.withTrimmedLeft (1.0f);
    const auto key = bottomRounded (body, kKeyCorner);

    auto top = kWhiteTop;
    auto bottom = kWhiteBottom;

    if (isDown)
    {
        top = kWhiteTop.interpolatedWith (kAccent, 0.55f).darker (0.10f);
        bottom = kWhiteBottom.interpolatedWith (kAccent, 0.35f);
    }
    else if (isOver)
    {
        top = top.darker (0.05f);
        bottom = bottom.darker (0.05f);
    }

    g.setGradientFill (verticalFill (top, bottom, body));
    g.fillPath (key);

    if (isDown)
    {
        // Ombra interna in alto: il tasto sembra affondato invece che solo colorato.
        g.setGradientFill ({ kEdgeDark.withAlpha (0.45f), body.getX(), body.getY(),
                             juce::Colours::transparentBlack, body.getX(), body.getY() + 12.0f, false });
        g.fillPath (key);
    }

    // Separatore a sinistra; l'ultimo tasto chiude anche a destra.
    auto separators = area;
    g.setColour (kEdgeDark.withAlpha (0.8f));
    g.fillRect (separators.withWidth (1.0f));

    if (midiNoteNumber == getRangeEnd())
        g.fillRect (separators.removeFromRight (1.0f));

    const auto text = getWhiteNoteText (midiNoteNumber);

    if (text.isNotEmpty())
    {
        const auto fontHeight = juce::jmin (10.0f, getKeyWidth() * 0.8f);

        g.setColour (isDown ? kLabel.darker (0.5f) : kLabel);
        g.setFont (juce::FontOptions { juce::Font::getDefaultMonospacedFontName(), fontHeight, juce::Font::plain });
        g.drawText (text, body.withTrimmedBottom (3.0f), juce::Justification::centredBottom, false);
    }
}

void XerumKeyboard::drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                                   bool isDown, bool isOver, juce::Colour noteFillColour)
{
    juce::ignoreUnused (midiNoteNumber, noteFillColour);

    const auto body = area;
    const auto key = bottomRounded (body, kKeyCorner);

    auto top = kBlackTop;
    auto bottom = kBlackBottom;

    if (isDown)
    {
        top = kBlackTop.interpolatedWith (kAccent, 0.50f);
        bottom = kBlackBottom.interpolatedWith (kAccent, 0.22f);
    }
    else if (isOver)
    {
        top = top.brighter (0.18f);
        bottom = bottom.brighter (0.12f);
    }

    g.setGradientFill (verticalFill (top, bottom, body));
    g.fillPath (key);

    // Unica luce che prendono i tasti neri: un rim sottile sullo spigolo alto.
    g.setColour (juce::Colours::white.withAlpha (isDown ? 0.04f : 0.12f));
    g.fillRect (body.withHeight (1.0f).reduced (1.0f, 0.0f));

    g.setColour (kEdgeDark);
    g.strokePath (key, juce::PathStrokeType { 1.0f });

    if (isDown)
    {
        // Filo accent sul fondo: la nota premuta si vede anche in mezzo a un accordo.
        auto glow = body;
        g.setColour (kAccent.withAlpha (0.9f));
        g.fillRect (glow.removeFromBottom (2.0f).reduced (1.5f, 0.0f));
    }
}

void XerumKeyboard::paintOverChildren (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Ombra sotto il bordo inferiore dello chassis: salda i due pezzi.
    g.setGradientFill ({ kEdgeDark.withAlpha (0.7f), 0.0f, b.getY(),
                         juce::Colours::transparentBlack, 0.0f, b.getY() + kSeamShadow, false });
    g.fillRect (b.withHeight (kSeamShadow));

    g.setColour (kSeam);
    g.fillRect (b.withHeight (1.0f));

    if (bottomCornerRadius_ <= 0.0f)
        return;

    // Maschera even-odd: riempie solo ciò che sta fuori dal rettangolo arrotondato,
    // cioè i due angoli inferiori, col fondo della finestra.
    juce::Path mask;
    mask.setUsingNonZeroWinding (false);
    mask.addRectangle (b);
    mask.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(),
                              bottomCornerRadius_, bottomCornerRadius_, false, false, true, true);

    g.setColour (kBackground);
    g.fillPath (mask);
}
} // namespace ui
