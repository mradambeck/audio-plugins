#include "ConcreteDirectionalPad.h"

namespace
{
    // ---- DirectionalPad.module.css ----
    constexpr float cellSize = 24.0f;
    constexpr float cellGap = 3.0f;
    constexpr float gridSize = 3.0f * cellSize + 2.0f * cellGap;
    constexpr float legendGap = 6.0f;
    // Real ~26px tall, not ~11px - see ConcreteLookAndFeel::kSmallTextLineHeight's comment.
    const float legendHeight = ConcreteLookAndFeel::kSmallTextLineHeight;

    const juce::Colour keyFill { 0xff2c2c2c };
    const juce::Colour keyBorderTop { 0xff4a4a4a };
    const juce::Colour keyBorderBottom { 0xff050505 };
    const juce::Colour glyphColour { 0xff9a9a9a };
    const juce::Colour legendColour { 0xff6a6a6a };
}

ConcreteDirectionalPad::ConcreteDirectionalPad (ConcreteLookAndFeel& lookAndFeelIn) : lookAndFeel (lookAndFeelIn)
{
    setSize ((int) gridSize, (int) (gridSize + legendGap + legendHeight));
}

juce::Rectangle<float> ConcreteDirectionalPad::cellBounds (int row, int col) const noexcept
{
    return { (float) col * (cellSize + cellGap), (float) row * (cellSize + cellGap), cellSize, cellSize };
}

void ConcreteDirectionalPad::paint (juce::Graphics& g)
{
    struct Key { int row, col; const char* glyphUtf8; };
    static const Key keys[] {
        { 0, 1, "\xe2\x96\xb2" }, // up
        { 1, 0, "\xe2\x97\x80" }, // left
        { 1, 2, "\xe2\x96\xb6" }, // right
        { 2, 1, "\xe2\x96\xbc" }, // down
    };

    // CSS says font-size:11px, but Oswald (the font this size is meant for) has no glyphs for
    // U+25B2/25BC/25C0/25B6 - both the browser and JUCE fall back to a system font for these
    // specific characters, and the two picked noticeably different-sized fallbacks at the "same"
    // nominal size (found by Adam: "Navigate arrows are the wrong size" - confirmed by a direct
    // crop comparison against the live mockup, not just this comment's say-so). Sized up
    // empirically to visually match the mockup's own arrow glyphs rather than the nominal CSS
    // value, since there's no shared font metric to derive an exact number from here.
    g.setFont (lookAndFeel.getSmallPrintFont (16.0f));
    for (const auto& key : keys)
    {
        const auto bounds = cellBounds (key.row, key.col);
        g.setColour (keyFill);
        g.fillRoundedRectangle (bounds, 3.0f);
        {
            // Clipped to the rounded silhouette - see ConcretePanelButton's own comment on why a
            // straight full-width drawLine() otherwise squares off the corners.
            juce::Graphics::ScopedSaveState save (g);
            juce::Path roundedPath;
            roundedPath.addRoundedRectangle (bounds, 3.0f);
            g.reduceClipRegion (roundedPath);
            g.setColour (keyBorderTop);
            g.drawLine (bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f, 1.0f);
            g.setColour (keyBorderBottom);
            g.drawLine (bounds.getX(), bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f, 1.0f);
        }
        g.setColour (glyphColour);
        g.drawText (juce::String (juce::CharPointer_UTF8 (key.glyphUtf8)), bounds, juce::Justification::centred);
    }

    auto legendArea = getLocalBounds().toFloat();
    legendArea.removeFromTop (gridSize + legendGap);
    g.setColour (legendColour);
    g.setFont (lookAndFeel.getSmallPrintFont (9.0f).withExtraKerningFactor (0.11f));
    g.drawText ("NAVIGATE", legendArea, juce::Justification::centred);
}

void ConcreteDirectionalPad::mouseDown (const juce::MouseEvent& event)
{
    if (cellBounds (0, 1).contains (event.position)) { if (onUp) onUp(); }
    else if (cellBounds (1, 0).contains (event.position)) { if (onLeft) onLeft(); }
    else if (cellBounds (1, 2).contains (event.position)) { if (onRight) onRight(); }
    else if (cellBounds (2, 1).contains (event.position)) { if (onDown) onDown(); }
}
