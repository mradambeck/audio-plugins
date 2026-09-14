#include "ConcretePadGrid.h"

namespace
{
    // ---- PadGrid.module.css ----
    constexpr float gridWidth = 260.0f;
    constexpr float gridGap = 6.0f;
    constexpr int cols = 4;
    constexpr float padSize = (gridWidth - (float) (cols - 1) * gridGap) / (float) cols; // 60.5

    const juce::Colour padFill { 0xff262626 };
    const juce::Colour padBorderTop { 0xff3d3d3d };
    const juce::Colour padBorderBottom { 0xff050505 };
    const juce::Colour padLitFill { 0xff9a9a9a };
    const juce::Colour padLitBorder { 0xffc4c4c4 };
    const juce::Colour noteLabelColour { 0xff6a6a6a };
    const juce::Colour noteLabelLitColour { 0xff1c1c1c };

    const float labelHeight = ConcreteLookAndFeel::kSilkscreenLabelHeight; // see that constant's own comment
}

ConcretePadGrid::ConcretePadGrid (ConcreteAudioProcessor& processorIn, ConcreteLookAndFeel& lookAndFeelIn)
    : processor (processorIn), lookAndFeel (lookAndFeelIn)
{
    setSize ((int) gridWidth, (int) (labelHeight + (float) rowsCount * padSize + (float) (rowsCount - 1) * gridGap));
}

int ConcretePadGrid::currentRootNote() const noexcept
{
    const auto sampleSet = processor.getCurrentSampleSet();
    if (sampleSet != nullptr && ! sampleSet->zones.empty())
        return sampleSet->zones[0].rootNote;
    return 60; // ConcreteSampleZone's own default, so pads print sensible names before a load
}

int ConcretePadGrid::noteForPad (int index) const noexcept
{
    const auto domRow = index / colsCount;
    const auto col = index % colsCount;
    const auto rowFromBottom = rowsCount - 1 - domRow;
    return currentRootNote() + rowFromBottom * colsCount + col;
}

juce::Rectangle<float> ConcretePadGrid::padBounds (int index) const noexcept
{
    const auto domRow = index / colsCount;
    const auto col = index % colsCount;
    const auto x = (float) col * (padSize + gridGap);
    const auto y = labelHeight + (float) domRow * (padSize + gridGap);
    return { x, y, padSize, padSize };
}

int ConcretePadGrid::padIndexAtPosition (juce::Point<float> position) const noexcept
{
    for (int i = 0; i < padCount; ++i)
        if (padBounds (i).contains (position))
            return i;
    return -1;
}

void ConcretePadGrid::paint (juce::Graphics& g)
{
    // SilkscreenLabel.tsx renders this uppercase (text-transform:uppercase) - drawing the literal
    // "Pads" here (this component predates ConcreteLookAndFeel::paintSilkscreenLabel, which was
    // extracted later from this exact code and already handles the uppercase/kerning/rule
    // correctly) was a real, uncaught bug, not a style choice. Delegating to that shared helper
    // instead of the hand-rolled version this used to have.
    lookAndFeel.paintSilkscreenLabel (g, getLocalBounds().toFloat().withHeight (labelHeight), "Pads", false);

    for (int i = 0; i < padCount; ++i)
    {
        const auto bounds = padBounds (i);
        const bool lit = i == litPadIndex;

        g.setColour (lit ? padLitFill : padFill);
        g.fillRoundedRectangle (bounds, 4.0f);

        // Two-tone top/bottom border edge (CSS border-top/border-bottom, 1px each) rather than a
        // uniform stroke - a flat drawRoundedRectangle outline reads noticeably different from the
        // mockup's subtle bevel. Clipped to the rounded silhouette (was a manual 4px inset guess
        // before - the exact clip matches every other component's border now, see
        // ConcretePanelButton's own comment on why a straight full-width line needs this at all).
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path roundedPath;
            roundedPath.addRoundedRectangle (bounds, 4.0f);
            g.reduceClipRegion (roundedPath);
            g.setColour (lit ? padLitBorder : padBorderTop);
            g.drawLine (bounds.getX(), bounds.getY() + 0.5f, bounds.getRight(), bounds.getY() + 0.5f, 1.0f);
            g.setColour (lit ? padLitBorder : padBorderBottom);
            g.drawLine (bounds.getX(), bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f, 1.0f);
        }

        if (lit)
        {
            juce::DropShadow glow (juce::Colours::white.withAlpha (0.3f), 6, {});
            juce::Path padPath;
            padPath.addRoundedRectangle (bounds, 4.0f);
            glow.drawForPath (g, padPath);
        }

        g.setColour (lit ? noteLabelLitColour : noteLabelColour);
        g.setFont (lookAndFeel.getSmallPrintFont (8.0f).withExtraKerningFactor (0.04f));
        g.drawText (juce::MidiMessage::getMidiNoteName (noteForPad (i), true, true, 3),
                    bounds.reduced (4.0f), juce::Justification::topRight);
    }
}

void ConcretePadGrid::mouseDown (const juce::MouseEvent& event)
{
    const auto index = padIndexAtPosition (event.position);
    if (index < 0)
        return;

    litPadIndex = index;
    processor.keyboardState.noteOn (midiChannel, noteForPad (index), 1.0f);
    repaint();
}

void ConcretePadGrid::mouseUp (const juce::MouseEvent&)
{
    releaseLitPad();
}

void ConcretePadGrid::mouseExit (const juce::MouseEvent&)
{
    releaseLitPad();
}

void ConcretePadGrid::releaseLitPad()
{
    if (litPadIndex < 0)
        return;
    processor.keyboardState.noteOff (midiChannel, noteForPad (litPadIndex), 0.0f);
    litPadIndex = -1;
    repaint();
}
