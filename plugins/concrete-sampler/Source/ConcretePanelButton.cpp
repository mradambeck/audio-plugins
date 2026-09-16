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
    {
        // CSS border-top/border-bottom follow the element's own border-radius - a straight
        // drawLine() spanning the full width does NOT, and pokes past the rounded corners,
        // visually squaring them off (found by Adam: "session buttons no longer have a border
        // radius" - they still do, the border was just drawn over it). Clipping to the exact
        // rounded silhouette before stroking guarantees the border never exceeds it, matching CSS.
        juce::Graphics::ScopedSaveState save (g);
        juce::Path roundedPath;
        roundedPath.addRoundedRectangle (bounds, 3.0f);
        g.reduceClipRegion (roundedPath);
        g.setColour ((pressed ? borderBottom : borderTop).withAlpha (alpha));
        g.drawLine (bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f, 1.0f);
        g.setColour ((pressed ? borderTop : borderBottom).withAlpha (alpha));
        g.drawLine (bounds.getX(), bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f, 1.0f);
    }

    // .PanelButton{display:flex;flex-direction:column;justify-content:center;gap:5px} - the
    // LED+label group is CENTERED as a whole within the full button height (measured via
    // getBoundingClientRect(): LED top at 12px, label top at 23px in a 46px button - NOT anchored
    // to the top of a reduced/padded content area, which is what this used to do and left both
    // sitting noticeably higher than the real mockup).
    constexpr float ledSize = 6.0f;
    constexpr float ledLabelGap = 5.0f;
    constexpr float labelLineHeight = 10.8f; // real height of a <button>'s own label text - exempt
                                              // from ConcreteLookAndFeel::kSmallTextLineHeight (see
                                              // that constant's comment on why <button> text differs)
    auto content = bounds.reduced (4.0f, 0.0f);
    const auto groupHeight = isLitSource ? (ledSize + ledLabelGap + labelLineHeight) : labelLineHeight;
    auto group = content.withSizeKeepingCentre (content.getWidth(), groupHeight);

    if (isLitSource)
    {
        const auto ledBounds = juce::Rectangle<float> (ledSize, ledSize).withCentre ({ group.getCentreX(), group.getY() + ledSize * 0.5f });
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
        group.removeFromTop (ledSize + ledLabelGap);
    }

    g.setColour (labelColour.withAlpha (alpha));
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f).withExtraKerningFactor (0.09f));
    g.drawFittedText ((labelSource ? labelSource() : label).toUpperCase(), group.getSmallestIntegerContainer(),
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
