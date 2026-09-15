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
    const juce::Colour fileNameLabelColour { 0xff7fa5f5 };      // .fileNameLabel
    const juce::Colour dragOverBorderColour { 0xff0555eb };     // .padDragOver
    const juce::Colour dragOverFillColour { 0x260555eb };       // .padDragOver's rgba(5,85,235,0.15)
    const juce::Colour ownZoneAccentColour { 0xff0555eb };      // .padOwnZone's inset 2px 0 0 0 accent

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

juce::String ConcretePadGrid::ownZoneFileNameForPad (int index) const
{
    const auto sampleSet = processor.getCurrentSampleSet();
    if (sampleSet == nullptr)
        return {};

    const auto note = noteForPad (index);
    const auto zoneIndex = sampleSet->lookup (note, 100);
    if (zoneIndex < 0)
        return {};

    const auto& zone = sampleSet->zones[(size_t) zoneIndex];
    if (zone.keyLo != note || zone.keyHi != note)
        return {}; // resolved to the inherited main zone, not a zone of this pad's own

    return juce::File (zone.sourcePath).getFileName();
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
        const auto ownFileName = ownZoneFileNameForPad (i);
        const bool hasOwnZone = ownFileName.isNotEmpty();

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

            // .padOwnZone's inset 2px 0 0 0 #0555eb - a left-edge accent bar marking a pad with an
            // independent sample of its own, distinct from one just inheriting the main sample.
            if (hasOwnZone && i != dragOverPadIndex)
            {
                g.setColour (ownZoneAccentColour);
                g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getY(), 2.0f, bounds.getHeight()));
            }
        }

        // .padDragOver: dashed blue border + a faint blue tint, while a file drag is over THIS pad
        // specifically (not the whole grid) - matches drag-and-drop granularity in PadGrid.tsx.
        if (i == dragOverPadIndex)
        {
            g.setColour (dragOverFillColour);
            g.fillRoundedRectangle (bounds, 4.0f);
            juce::Path dashPath;
            dashPath.addRoundedRectangle (bounds.reduced (1.0f), 3.0f);
            float dashLengths[] { 4.0f, 3.0f };
            juce::Path dashed;
            juce::PathStrokeType (2.0f).createDashedStroke (dashed, dashPath, dashLengths, 2);
            g.setColour (dragOverBorderColour);
            g.strokePath (dashed, juce::PathStrokeType (2.0f));
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

        // .fileNameLabel: bottom-left, small and unobtrusive - only a pad with its own zone has
        // anything to show here (a pad inheriting the main sample already prints that filename on
        // the LCD's own Sample page, so repeating it on every pad would just be noise).
        if (hasOwnZone)
        {
            g.setColour (lit ? noteLabelLitColour : fileNameLabelColour);
            g.setFont (lookAndFeel.getSmallPrintFont (7.0f));
            g.drawFittedText (ownFileName, bounds.reduced (4.0f).getSmallestIntegerContainer(),
                               juce::Justification::bottomLeft, 1);
        }
    }
}

void ConcretePadGrid::mouseDown (const juce::MouseEvent& event)
{
    const auto index = padIndexAtPosition (event.position);
    if (index < 0)
        return;

    if (event.mods.isPopupMenu())
    {
        showContextMenu (index);
        return;
    }

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

bool ConcretePadGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (juce::File (path).hasFileExtension ("wav;aif;aiff"))
            return true;
    return false;
}

void ConcretePadGrid::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void ConcretePadGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    const auto index = padIndexAtPosition ({ (float) x, (float) y });
    if (index != dragOverPadIndex)
    {
        dragOverPadIndex = index;
        repaint();
    }
}

void ConcretePadGrid::fileDragExit (const juce::StringArray&)
{
    dragOverPadIndex = -1;
    repaint();
}

void ConcretePadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragOverPadIndex = -1;
    repaint();

    const auto index = padIndexAtPosition ({ (float) x, (float) y });
    if (index < 0)
        return;

    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.hasFileExtension ("wav;aif;aiff"))
        {
            const auto note = noteForPad (index);
            juce::Component::SafePointer<ConcretePadGrid> safeThis (this);
            processor.assignSampleToPadAsync (note, file, [safeThis] (bool)
            {
                if (auto* self = safeThis.getComponent())
                    self->repaint();
            });
            break;
        }
    }
}

void ConcretePadGrid::loadFileOntoPad (int index)
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a sample onto this pad...", juce::File(), "*.wav;*.aif;*.aiff");
    const auto note = noteForPad (index);
    juce::Component::SafePointer<ConcretePadGrid> safeThis (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safeThis, note] (const juce::FileChooser& chooser)
                               {
                                   auto* self = safeThis.getComponent();
                                   if (self == nullptr)
                                       return;
                                   const auto file = chooser.getResult();
                                   if (file == juce::File())
                                       return;
                                   self->processor.assignSampleToPadAsync (note, file, [safeThis] (bool)
                                   {
                                       if (auto* stillAlive = safeThis.getComponent())
                                           stillAlive->repaint();
                                   });
                               });
}

void ConcretePadGrid::showContextMenu (int index)
{
    const auto note = noteForPad (index);
    const bool hasOwnZone = ownZoneFileNameForPad (index).isNotEmpty();

    juce::PopupMenu menu;
    menu.addItem ("Load...", [this, index] { loadFileOntoPad (index); });
    menu.addItem ("Clear", hasOwnZone, false, [this, note] { processor.clearPadSample (note); repaint(); });
    menu.addItem ("Copy", hasOwnZone, false, [this, note]
    {
        const auto sampleSet = processor.getCurrentSampleSet();
        if (sampleSet == nullptr)
            return;
        const auto zoneIndex = sampleSet->lookup (note, 100);
        if (zoneIndex >= 0)
            clipboardFile = juce::File (sampleSet->zones[(size_t) zoneIndex].sourcePath);
    });
    menu.addItem ("Paste", clipboardFile != juce::File(), false, [this, note]
    {
        juce::Component::SafePointer<ConcretePadGrid> safeThis (this);
        processor.assignSampleToPadAsync (note, clipboardFile, [safeThis] (bool)
        {
            if (auto* self = safeThis.getComponent())
                self->repaint();
        });
    });

    menu.showMenuAsync (juce::PopupMenu::Options());
}
