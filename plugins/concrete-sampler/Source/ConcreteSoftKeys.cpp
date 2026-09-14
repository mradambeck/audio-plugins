#include "ConcreteSoftKeys.h"

namespace
{
    // ---- SoftKeys.module.css ----
    constexpr int keyCount = 4;
    constexpr float totalWidth = 460.0f; // matches the screen's own footer tab-cell span exactly
    constexpr float keyHeight = 26.0f;
    constexpr float keyGap = 8.0f;
    constexpr float keyWidth = (totalWidth - (float) (keyCount - 1) * keyGap) / (float) keyCount;

    const juce::Colour keyFill { 0xff262626 };
    const juce::Colour keyBorderTop { 0xff454545 };
    const juce::Colour keyBorderBottom { 0xff050505 };
    const juce::Colour ledOff { 0xff0a0a0a };
    const juce::Colour ledLit { 0xff0555eb };
}

ConcreteSoftKeys::ConcreteSoftKeys (ConcreteScreen& screenIn) : screen (screenIn)
{
    setSize ((int) totalWidth, (int) keyHeight);
}

juce::Rectangle<float> ConcreteSoftKeys::keyBounds (int index) const noexcept
{
    return { (float) index * (keyWidth + keyGap), 0.0f, keyWidth, keyHeight };
}

void ConcreteSoftKeys::paint (juce::Graphics& g)
{
    const auto activePage = screen.getPageIndex();

    for (int i = 0; i < keyCount; ++i)
    {
        const auto bounds = keyBounds (i);

        g.setColour (keyFill);
        g.fillRoundedRectangle (bounds, 3.0f);

        // Two-tone top/bottom border edge (CSS border-top/border-bottom), same technique as every
        // other flat sampler-style cap in this codebase (ConcretePadGrid/ConcreteKnob).
        g.setColour (keyBorderTop);
        g.drawLine (bounds.getX() + 3.0f, bounds.getY() + 0.5f, bounds.getRight() - 3.0f, bounds.getY() + 0.5f, 1.0f);
        g.setColour (keyBorderBottom);
        g.drawLine (bounds.getX() + 3.0f, bounds.getBottom() - 1.0f, bounds.getRight() - 3.0f, bounds.getBottom() - 1.0f, 2.0f);

        const bool lit = i == activePage;
        const auto ledBounds = bounds.withSizeKeepingCentre (6.0f, 6.0f);
        g.setColour (lit ? ledLit : ledOff);
        g.fillEllipse (ledBounds);
        if (lit)
        {
            juce::DropShadow glow (ledLit.withAlpha (0.8f), 5, {});
            juce::Path ledPath;
            ledPath.addEllipse (ledBounds);
            glow.drawForPath (g, ledPath);
        }
    }
}

void ConcreteSoftKeys::mouseDown (const juce::MouseEvent& event)
{
    for (int i = 0; i < keyCount; ++i)
    {
        if (keyBounds (i).contains (event.position))
        {
            screen.setPageIndex (i);
            repaint();
            return;
        }
    }
}
