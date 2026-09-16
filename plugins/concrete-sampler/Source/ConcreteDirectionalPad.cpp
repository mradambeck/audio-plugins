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

namespace
{
    // A hand-drawn triangle, not a Unicode glyph (U+25B2/25BC/25C0/25B6) rendered via a font - both
    // the browser and JUCE fall back to a system font for these code points (Oswald has no glyphs
    // for them), and the two fallbacks disagreed sharply on how much of the em-box the ink
    // actually fills - no font-size value made JUCE's rendering match the mockup's (found by Adam,
    // twice: first "wrong size", then "still way too big" after a same-technique retry). Drawing
    // the shape directly removes the font entirely from the equation - `size` is the triangle's
    // own width/height, chosen to match the mockup's visual proportion within its 24px cell, not a
    // font metric.
    juce::Path arrowTriangle (juce::Point<float> centre, float size, int direction /* 0=up,1=left,2=right,3=down */)
    {
        const float h = size * 0.5f;
        juce::Path p;
        switch (direction)
        {
            case 0: p.addTriangle (centre.x, centre.y - h, centre.x - h, centre.y + h, centre.x + h, centre.y + h); break;
            case 1: p.addTriangle (centre.x - h, centre.y, centre.x + h, centre.y - h, centre.x + h, centre.y + h); break;
            case 2: p.addTriangle (centre.x + h, centre.y, centre.x - h, centre.y - h, centre.x - h, centre.y + h); break;
            default: p.addTriangle (centre.x, centre.y + h, centre.x - h, centre.y - h, centre.x + h, centre.y - h); break;
        }
        return p;
    }
}

void ConcreteDirectionalPad::paint (juce::Graphics& g)
{
    struct Key { int row, col; int direction; };
    static const Key keys[] {
        { 0, 1, 0 }, // up
        { 1, 0, 1 }, // left
        { 1, 2, 2 }, // right
        { 2, 1, 3 }, // down
    };

    constexpr float arrowSize = 11.0f;
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
        g.fillPath (arrowTriangle (bounds.getCentre(), arrowSize, key.direction));
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
