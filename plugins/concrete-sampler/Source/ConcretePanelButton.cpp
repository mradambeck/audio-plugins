#include "ConcretePanelButton.h"

namespace
{
    // ---- PanelButton.module.css ----
    constexpr float buttonHeight = 46.0f;
    const juce::Colour fill { 0xff232323 };
    const juce::Colour fillPressed { 0xff1c1c1c };
    const juce::Colour borderTop { 0xff3a3a3a };
    const juce::Colour borderBottom { 0xff050505 };
    const juce::Colour ledOff { 0xff0a0a0a };
    const juce::Colour ledLit { 0xff0555eb };
    const juce::Colour labelColour { 0xff9a9a9a };
}

ConcretePanelButton::ConcretePanelButton (ConcreteLookAndFeel& lookAndFeelIn, juce::String labelIn,
                                           std::function<void()> onClickIn, std::function<bool()> isLitSourceIn)
    : lookAndFeel (lookAndFeelIn), label (std::move (labelIn)), onClick (std::move (onClickIn)),
      isLitSource (std::move (isLitSourceIn))
{
    setSize (80, (int) buttonHeight);
}

void ConcretePanelButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto alpha = isEnabled() ? 1.0f : 0.5f;

    g.setColour ((pressed ? fillPressed : fill).withAlpha (alpha));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour ((pressed ? borderBottom : borderTop).withAlpha (alpha));
    g.drawLine (bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f, 1.0f);
    g.setColour ((pressed ? borderTop : borderBottom).withAlpha (alpha));
    g.drawLine (bounds.getX(), bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f, 1.0f);

    auto content = bounds.reduced (4.0f, 6.0f);

    if (isLitSource)
    {
        const auto ledBounds = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ content.getCentreX(), content.getY() + 3.0f });
        const bool lit = isLitSource();
        g.setColour ((lit ? ledLit : ledOff).withAlpha (alpha));
        g.fillEllipse (ledBounds);
        if (lit)
        {
            juce::DropShadow glow (ledLit.withAlpha (0.7f * alpha), 4, {});
            juce::Path ledPath;
            ledPath.addEllipse (ledBounds);
            glow.drawForPath (g, ledPath);
        }
        content.removeFromTop (6.0f + 5.0f);
    }

    g.setColour (labelColour.withAlpha (alpha));
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f).withExtraKerningFactor (0.09f));
    g.drawFittedText ((labelSource ? labelSource() : label).toUpperCase(), content.getSmallestIntegerContainer(),
                       juce::Justification::centred, 2);
}

void ConcretePanelButton::mouseDown (const juce::MouseEvent&)
{
    if (! isEnabled())
        return;
    pressed = true;
    repaint();
}

void ConcretePanelButton::mouseUp (const juce::MouseEvent& event)
{
    const auto wasPressed = pressed;
    pressed = false;
    repaint();

    if (isEnabled() && wasPressed && getLocalBounds().toFloat().contains (event.position) && onClick)
        onClick();
}
