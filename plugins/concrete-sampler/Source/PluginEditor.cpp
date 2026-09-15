#include "PluginEditor.h"

namespace
{
    // ---- Panel.module.css ----
    constexpr float panelPaddingX = 28.0f;
    constexpr float panelPaddingY = 20.0f;
    constexpr float sectionGap = 16.0f; // .Panel's own column gap (header/body/footer)
    constexpr float bodyGap = 24.0f;    // .body's row gap (leftColumn/rightColumn)

    // Measured via getBoundingClientRect() on the live mockup at a desktop-width viewport (not
    // eyeballed, and not the ~1000px-wide headless-Chrome window this was FIRST measured at by
    // mistake - that width is below the mockup's own `@media (max-width:1024px)` breakpoint and
    // silently drops the root font-size, throwing off every unstyled line-height on the page -
    // see ConcreteLookAndFeel::kSmallTextLineHeight's comment for the full story).
    constexpr float headerHeight = 28.0f;
    constexpr float footerHeight = 26.0f;

    constexpr float screenWellPadding = 16.0f;
    constexpr float leftColumnGap = 12.0f;
    constexpr float softKeysMarginTop = 8.0f;

    constexpr float rightColumnWidth = 300.0f;
    constexpr float rightColumnGap = 14.0f;

    const juce::Colour panelBackground { 0xff161616 };
    const juce::Colour screenWellFill { 0xff050505 };
    const juce::Colour wordmarkColour { 0xff7fa5f5 };
    const juce::Colour taglineColour { 0xff6f8280 };
    const juce::Colour footerLeftColour { 0xff586566 };
    const juce::Colour footerRightColour { 0xff3a4547 };
    const juce::Colour stripFill { 0xff1c1c1c };

    constexpr auto concreteVersion = "0.1.0"; // matches CMakeLists.txt's project() VERSION
}

ConcreteEditorContent::ConcreteEditorContent(ConcreteAudioProcessor& p)
    : processor(p),
      concreteScreen(p, lookAndFeel),
      softKeys(concreteScreen),
      padGrid(p, lookAndFeel),
      cutoffKnob(p, lookAndFeel, ConcreteAudioProcessor::filterCutoffParamID, "Cutoff",
                 [](float v) { return v >= 1000.0f ? juce::String(v / 1000.0f, 1) + "k" : juce::String(juce::roundToInt(v)) + "Hz"; }),
      resonanceKnob(p, lookAndFeel, ConcreteAudioProcessor::filterResonanceParamID, "Resonance",
                    [](float v) { return juce::String(v, 2); }),
      // Real zones[0].level (see PluginProcessor::setLevelForZone) - was a UI-only stand-in with
      // no connection to actual audio at all until Adam reported "Sample volume doesn't appear to
      // be working," which is exactly what that disconnect looked like from the outside. Reads/
      // writes whichever zone concreteScreen is currently displaying (see its own
      // getDisplayedZoneIndex()), not always the main zone, so this fader stays in sync with a
      // pad's own sample once one's been hit.
      sampleVolumeFader(lookAndFeel, "Sample", 0.0f, 120.0f,
                         [this]
                         {
                             const auto sampleSet = processor.getCurrentSampleSet();
                             const auto zoneIndex = concreteScreen.getDisplayedZoneIndex();
                             return (sampleSet != nullptr && zoneIndex < (int) sampleSet->zones.size())
                                        ? sampleSet->zones[(size_t) zoneIndex].level * 100.0f : 100.0f;
                         },
                         [this](float v) { processor.setLevelForZone(concreteScreen.getDisplayedZoneIndex(), v / 100.0f); },
                         [](float v) { return juce::String(juce::roundToInt(v)) + "%"; }),
      // Real masterVolumeParamID (see that constant's own comment) - a host-automatable output
      // gain, unlike Sample Volume's zone-list state above, wired the same convertFrom0to1/
      // convertTo0to1 way ConcreteScreen's own adjustParam() reads/writes every plain
      // AudioParameterFloat on the LCD pages.
      masterVolumeFader(lookAndFeel, "Master", 0.0f, 120.0f,
                         [this]
                         {
                             auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::masterVolumeParamID);
                             return param->convertFrom0to1(param->getValue()) * 100.0f;
                         },
                         [this](float v)
                         {
                             auto* param = processor.apvts.getParameter(ConcreteAudioProcessor::masterVolumeParamID);
                             param->setValueNotifyingHost(param->convertTo0to1(v / 100.0f));
                         },
                         [](float v) { return juce::String(juce::roundToInt(v)) + "%"; }),
      machineSelector(p, lookAndFeel),
      directionalPad(lookAndFeel),
      dataKnob(lookAndFeel),
      // Both buttons act on whichever zone concreteScreen is currently displaying (see its own
      // getDisplayedZoneIndex()), not always the main zone - same reasoning as sampleVolumeFader
      // above.
      oneShotButton(lookAndFeel, "One-Shot",
                    [this]
                    {
                        const auto sampleSet = processor.getCurrentSampleSet();
                        const auto zoneIndex = concreteScreen.getDisplayedZoneIndex();
                        if (sampleSet != nullptr && zoneIndex < (int) sampleSet->zones.size())
                            processor.setOneShotForZone(zoneIndex, !sampleSet->zones[(size_t) zoneIndex].oneShot);
                        concreteScreen.repaint();
                    },
                    [this]
                    {
                        const auto sampleSet = processor.getCurrentSampleSet();
                        const auto zoneIndex = concreteScreen.getDisplayedZoneIndex();
                        return sampleSet != nullptr && zoneIndex < (int) sampleSet->zones.size() && sampleSet->zones[(size_t) zoneIndex].oneShot;
                    }),
      loopButton(lookAndFeel, "Loop",
                 [this]
                 {
                     const auto sampleSet = processor.getCurrentSampleSet();
                     const auto zoneIndex = concreteScreen.getDisplayedZoneIndex();
                     if (sampleSet != nullptr && zoneIndex < (int) sampleSet->zones.size())
                         processor.setLoopEnabledForZone(zoneIndex, !sampleSet->zones[(size_t) zoneIndex].loopEnabled);
                     concreteScreen.repaint();
                 },
                 [this]
                 {
                     const auto sampleSet = processor.getCurrentSampleSet();
                     const auto zoneIndex = concreteScreen.getDisplayedZoneIndex();
                     return sampleSet != nullptr && zoneIndex < (int) sampleSet->zones.size() && sampleSet->zones[(size_t) zoneIndex].loopEnabled;
                 }),
      resampleButton(lookAndFeel, "Resample", [this] { processor.triggerBake(); }),
      saveSampleButton(lookAndFeel, "Save Sample",
                       [this] { processor.setEmbedSamplesOverride(!processor.getEmbedSamplesOverride()); }),
      loadClearButton(lookAndFeel, "Load Sample",
                      [this]
                      {
                          if (hasSampleLoaded())
                          {
                              concreteScreen.clearSample();
                              return;
                          }
                          fileChooser = std::make_unique<juce::FileChooser>(
                              "Load a sample...", juce::File(), "*.wav;*.aif;*.aiff");
                          fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                                    [this](const juce::FileChooser& chooser)
                                                    {
                                                        const auto file = chooser.getResult();
                                                        if (file != juce::File())
                                                            loadFile(file);
                                                    });
                      }),
      // Not in the mockup - Adam asked for a way to immediately stop any samples currently
      // playing, e.g. after triggering a long one-shot pad. Forces every voice silent right away
      // (see ConcreteAudioProcessor::stopAllVoices()), unlike an ordinary note-off which respects
      // a one-shot zone's "plays through to its own end" behavior.
      stopButton(lookAndFeel, "Stop", [this] { processor.stopAllVoices(); })
{
    setLookAndFeel(&lookAndFeel);

    concreteScreen.onPageChanged = [this] { softKeys.repaint(); };
    padGrid.onPadTriggered = [this](int note) { concreteScreen.showZoneForNote(note); };

    resampleButton.setTooltip("Re-runs the capture pass with the current settings");
    saveSampleButton.setTooltip(
        "Toggles whether the sample is saved inside this preset (portable, larger) or only "
        "referenced by file path (smaller, breaks if that file moves)");
    loadClearButton.labelSource = [this] { return hasSampleLoaded() ? "Clear Sample" : "Load Sample"; };
    stopButton.setTooltip("Immediately silences any samples currently playing");

    directionalPad.onUp = [this] { concreteScreen.moveSelectionVertical(-1); };
    directionalPad.onDown = [this] { concreteScreen.moveSelectionVertical(1); };
    directionalPad.onLeft = [this] { concreteScreen.moveSelectionHorizontal(-1); };
    directionalPad.onRight = [this] { concreteScreen.moveSelectionHorizontal(1); };
    dataKnob.onAdjust = [this](int delta) { concreteScreen.adjustSelected(delta); };

    // Explicit std::initializer_list<Component*> (not auto*) - a braced-init-list only allows a
    // SINGLE common element type under `auto` deduction, so mixed subclass pointers need the
    // target type spelled out for the usual derived*->base* conversions to kick in.
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &concreteScreen, &softKeys, &padGrid, &cutoffKnob, &resonanceKnob,
             &sampleVolumeFader, &masterVolumeFader, &machineSelector, &directionalPad, &dataKnob,
             &oneShotButton, &loopButton, &resampleButton, &saveSampleButton, &loadClearButton, &stopButton })
        addAndMakeVisible(c);

    // Session button LEDs (One-Shot/Loop) and the Load/Clear label can change from the LCD screen's
    // own field taps, not just from these buttons themselves - a small idle repaint keeps them from
    // going stale without wiring a dedicated change-broadcast path for what's a purely cosmetic
    // sync (matches how meters/LEDs are commonly kept live in this catalog).
    startTimerHz(15);

    // 936x762 - recomputed from the corrected section heights below (was 740 tall before the
    // kSmallTextLineHeight/kSilkscreenLabelHeight fixes; see resized()'s own math for how this
    // number is reached, and ConcreteLookAndFeel::kSmallTextLineHeight's comment for why the old
    // heights were wrong in the first place).
    setSize(936, 762);
}

ConcreteEditorContent::~ConcreteEditorContent()
{
    setLookAndFeel(nullptr);
}

bool ConcreteEditorContent::hasSampleLoaded() const
{
    const auto sampleSet = processor.getCurrentSampleSet();
    return sampleSet != nullptr && !sampleSet->zones.empty() && sampleSet->zones[0].sourceBuffer != nullptr;
}

void ConcreteEditorContent::timerCallback()
{
    oneShotButton.repaint();
    loopButton.repaint();
    resampleButton.repaint();
    loadClearButton.repaint();
}

void ConcreteEditorContent::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);

    auto bounds = getLocalBounds().toFloat().reduced(panelPaddingX, panelPaddingY);

    auto headerArea = bounds.removeFromTop(headerHeight);
    g.setColour(wordmarkColour);
    g.setFont(lookAndFeel.getDisplayFont(24.0f));
    const auto wordmarkWidth = juce::GlyphArrangement::getStringWidth(lookAndFeel.getDisplayFont(24.0f), "CONCRETE");
    g.drawText("CONCRETE", headerArea.removeFromLeft(wordmarkWidth), juce::Justification::centredLeft);
    headerArea.removeFromLeft(12.0f);
    g.setColour(taglineColour);
    g.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(1.6f / 11.0f));
    g.drawText("VARIABLE RATE SAMPLER", headerArea, juce::Justification::centredLeft);

    bounds.removeFromTop(sectionGap);

    // Screen well: a 16px-padded #050505 recess around the LCD - the only bevel this chrome has
    // (ui-plan.md's "Three divergences": no chassis, no fluted-rim knobs elsewhere).
    const auto screenWellBounds = juce::Rectangle<float>(concreteScreen.getX() - screenWellPadding,
                                                           concreteScreen.getY() - screenWellPadding,
                                                           concreteScreen.getWidth() + 2.0f * screenWellPadding,
                                                           concreteScreen.getHeight() + 2.0f * screenWellPadding);
    g.setColour(screenWellFill);
    g.fillRoundedRectangle(screenWellBounds, 6.0f);
    juce::DropShadow innerShadow(juce::Colours::black.withAlpha(0.7f), 8, { 0, 3 });
    juce::Path wellPath;
    wellPath.addRoundedRectangle(screenWellBounds, 6.0f);
    innerShadow.drawForPath(g, wellPath);

    // The Performance strip and Volume section each get a flat #1c1c1c backing box (.stripControlsVertical/
    // .volumeControls) - PadGrid paints its own pads directly with no such backing, matching the mockup.
    auto stripBox = juce::Rectangle<float>(cutoffKnob.getX() - 20.0f, cutoffKnob.getY() - 14.0f,
                                            cutoffKnob.getWidth() + 40.0f, resonanceKnob.getBottom() - cutoffKnob.getY() + 28.0f);
    g.setColour(stripFill);
    g.fillRoundedRectangle(stripBox, 4.0f);
    lookAndFeel.paintSilkscreenLabel(
        g, stripBox.withY(stripBox.getY() - ConcreteLookAndFeel::kSilkscreenLabelHeight).withHeight(ConcreteLookAndFeel::kSilkscreenLabelHeight),
        "Performance", true);

    // Height/Y match stripBox exactly (both are the SAME padsPerformanceRow, stretched by
    // align-items:stretch to PadGrid's own height in the mockup) rather than being derived from
    // sampleVolumeFader's own (shorter) bounds - that previously left Volume's box visibly
    // shorter than Performance's, since a fader doesn't fill its box the way it's centered within.
    auto volumeBox = juce::Rectangle<float>(sampleVolumeFader.getX() - 20.0f, stripBox.getY(),
                                             masterVolumeFader.getRight() - sampleVolumeFader.getX() + 40.0f,
                                             stripBox.getHeight());
    g.setColour(stripFill);
    g.fillRoundedRectangle(volumeBox, 4.0f);
    lookAndFeel.paintSilkscreenLabel(
        g, volumeBox.withY(volumeBox.getY() - ConcreteLookAndFeel::kSilkscreenLabelHeight).withHeight(ConcreteLookAndFeel::kSilkscreenLabelHeight),
        "Volume", true);

    // Right column's own section labels/backing boxes (Edit/Session). Derived from
    // machineSelector's own bottom edge (NOT from directionalPad/dataKnob's positions) - those two
    // are vertically centered WITHIN this box at possibly-different offsets from each other (see
    // resized()), so building the box from their positions instead risks the box's top edge
    // encroaching on the label above it (exactly the bug this comment used to not warn about: the
    // box was painted 9px into the label's own rectangle, silently covering the text underneath -
    // found by swapping this fill to solid red and seeing the box sit higher than the "Edit" text
    // ever appeared).
    const auto editSectionTop = (float) machineSelector.getBottom() + rightColumnGap;
    lookAndFeel.paintSilkscreenLabel(
        g, { (float) machineSelector.getX(), editSectionTop, rightColumnWidth, ConcreteLookAndFeel::kSilkscreenLabelHeight },
        "Edit", false);
    const auto editContentBottom = (float) juce::jmax(directionalPad.getBottom(), dataKnob.getBottom());
    const auto editBoxTopEdge = editSectionTop + ConcreteLookAndFeel::kSilkscreenLabelHeight;
    auto editBox = juce::Rectangle<float>((float) machineSelector.getX(), editBoxTopEdge,
                                           rightColumnWidth, editContentBottom - editBoxTopEdge + 12.0f);
    g.setColour(stripFill);
    g.fillRoundedRectangle(editBox, 4.0f);
    // Redraw the pad/knob's own transparent-background children on top would be wrong order-wise -
    // paint() runs before children, so this box is drawn first and the child components composite
    // over it normally (see resized(), which positions them inside this same rect).

    lookAndFeel.paintSilkscreenLabel(
        g, { (float) machineSelector.getX(), (float) oneShotButton.getY() - ConcreteLookAndFeel::kSilkscreenLabelHeight,
             rightColumnWidth, ConcreteLookAndFeel::kSilkscreenLabelHeight },
        "Session", false);

    auto footerArea = getLocalBounds().toFloat().reduced(panelPaddingX, panelPaddingY);
    footerArea = footerArea.removeFromBottom(footerHeight);
    g.setColour(footerLeftColour);
    g.setFont(lookAndFeel.getSmallPrintFont(10.0f).withExtraKerningFactor(0.08f));
    // juce::String's raw (const char*) constructor does NOT reliably assume UTF-8 the way
    // juce::CharPointer_UTF8 explicitly does - a bare "\xc2\xb7" literal here rendered as the
    // mojibake "Â·" instead of "·" (found by Adam, not caught by this session's own screenshots).
    g.drawText(juce::String(juce::CharPointer_UTF8("CONCRETE \xc2\xb7 v")) + concreteVersion,
               footerArea, juce::Justification::centredLeft);
    g.setColour(footerRightColour);
    g.drawText("WILD JAG", footerArea, juce::Justification::centredRight);
}

void ConcreteEditorContent::resized()
{
    auto bounds = getLocalBounds().toFloat().reduced(panelPaddingX, panelPaddingY);

    bounds.removeFromTop(headerHeight);
    bounds.removeFromTop(sectionGap);
    bounds.removeFromBottom(footerHeight);
    bounds.removeFromBottom(sectionGap);

    auto rightColumnArea = bounds.removeFromRight(rightColumnWidth);
    bounds.removeFromRight(bodyGap);
    auto leftColumnArea = bounds; // whatever's left is the left column's own content width

    // --- Left column ---
    // padsPerformanceRow is the widest thing in this column (PadGrid + Performance strip +
    // Volume section, each with its own gap) - screenWell/SoftKeys re-center under IT, matching
    // leftColumn's own align-items:center (see Panel.module.css's comment on why).
    // stripWidth is 100 (20+20 padding + 60 knob width), NOT the mockup's own real 84 (20+20+44) -
    // ConcreteKnob is deliberately 60px wide, not the mockup's 44px, to avoid clipping "RESONANCE"
    // (JUCE ellipsizes overflow text; a browser div without overflow:hidden doesn't - see
    // ConcreteKnob.cpp's own comment). That intentional +16px necessarily carries through to this
    // box and therefore to the whole row/leftColumn (556px here vs the mockup's real 540px,
    // confirmed via getBoundingClientRect()) - a traced-through, accepted consequence of that
    // earlier fix, not a new discrepancy.
    constexpr float stripWidth = 100.0f;
    constexpr float volumeWidth = 148.0f;  // 20+20 padding + 2*44 fader width + 20 gap - matches the mockup exactly
    const float padsPerformanceRowWidth = 260.0f + bodyGap + stripWidth + bodyGap + volumeWidth;

    auto leftColumn = leftColumnArea.withWidth(padsPerformanceRowWidth);

    auto screenWellSlot = leftColumn.withWidth(concreteScreen.getWidth() + 2.0f * screenWellPadding);
    screenWellSlot.setX(leftColumn.getX() + (padsPerformanceRowWidth - screenWellSlot.getWidth()) * 0.5f);
    concreteScreen.setTopLeftPosition((int) (screenWellSlot.getX() + screenWellPadding),
                                       (int) (leftColumn.getY() + screenWellPadding));

    auto softKeysSlot = leftColumn.withWidth((float) softKeys.getWidth());
    softKeysSlot.setX(leftColumn.getX() + (padsPerformanceRowWidth - softKeysSlot.getWidth()) * 0.5f);
    softKeys.setTopLeftPosition((int) softKeysSlot.getX(),
                                (int) (concreteScreen.getBottom() + screenWellPadding + leftColumnGap + softKeysMarginTop));

    const auto padsRowY = softKeys.getBottom() + leftColumnGap;
    padGrid.setTopLeftPosition((int) leftColumn.getX(), (int) padsRowY);

    const auto stripX = padGrid.getRight() + bodyGap;
    cutoffKnob.setTopLeftPosition((int) (stripX + 20.0f),
                                   (int) (padsRowY + ConcreteLookAndFeel::kSilkscreenLabelHeight + 14.0f));
    const float knobGapForStretch = (260.0f - 2.0f * cutoffKnob.getHeight()) - 2.0f * 14.0f; // stretched to PadGrid's own height
    resonanceKnob.setTopLeftPosition(cutoffKnob.getX(),
                                      (int) (cutoffKnob.getBottom() + juce::jmax(0.0f, knobGapForStretch)));

    const auto volumeX = stripX + stripWidth + bodyGap;
    const auto volumeBoxTop = padsRowY + ConcreteLookAndFeel::kSilkscreenLabelHeight;
    const auto volumeBoxHeight = 260.0f;
    const auto faderHeight = (float) sampleVolumeFader.getHeight();
    const auto faderY = volumeBoxTop + (volumeBoxHeight - faderHeight) * 0.5f;
    sampleVolumeFader.setTopLeftPosition((int) (volumeX + 20.0f), (int) faderY);
    masterVolumeFader.setTopLeftPosition(sampleVolumeFader.getRight() + 20, (int) faderY);

    // --- Right column ---
    machineSelector.setBounds((int) rightColumnArea.getX(), (int) rightColumnArea.getY(),
                               (int) rightColumnWidth, machineSelector.getHeight());

    const auto editBoxTop = machineSelector.getBottom() + rightColumnGap
                             + ConcreteLookAndFeel::kSilkscreenLabelHeight + 12.0f /* box padding */;
    const auto editControlsGap = 44.0f;
    const auto editContentWidth = directionalPad.getWidth() + editControlsGap + dataKnob.getWidth();
    const auto editContentX = rightColumnArea.getX() + (rightColumnWidth - editContentWidth) * 0.5f;
    const auto editContentHeight = (float) juce::jmax(directionalPad.getHeight(), dataKnob.getHeight());
    directionalPad.setTopLeftPosition((int) editContentX, (int) (editBoxTop + (editContentHeight - directionalPad.getHeight()) * 0.5f));
    dataKnob.setTopLeftPosition((int) (directionalPad.getRight() + editControlsGap),
                                (int) (editBoxTop + (editContentHeight - dataKnob.getHeight()) * 0.5f));

    const auto sessionTop = juce::jmax (directionalPad.getBottom(), dataKnob.getBottom()) + 12.0f + rightColumnGap
                             + ConcreteLookAndFeel::kSilkscreenLabelHeight;
    const auto buttonGap = 6.0f;
    const auto buttonWidth = (rightColumnWidth - 2.0f * buttonGap) / 3.0f;

    oneShotButton.setBounds((int) rightColumnArea.getX(), (int) sessionTop, (int) buttonWidth, oneShotButton.getHeight());
    loopButton.setBounds(oneShotButton.getRight() + (int) buttonGap, (int) sessionTop, (int) buttonWidth, loopButton.getHeight());
    resampleButton.setBounds(loopButton.getRight() + (int) buttonGap, (int) sessionTop, (int) buttonWidth, resampleButton.getHeight());

    // Same 3-equal-column grid as the row above (buttonWidth), not a 2-column halfButtonWidth one -
    // adding Stop needed a third slot, and lining both rows up on the same columns reads better
    // than a mismatched second row. ConcretePanelButton's label already wraps/shrinks to fit (see
    // its own drawFittedText call), so "Save Sample"/"Clear Sample" are still fully legible at the
    // narrower width.
    const auto secondRowTop = sessionTop + oneShotButton.getHeight() + 8.0f;
    saveSampleButton.setBounds((int) rightColumnArea.getX(), (int) secondRowTop, (int) buttonWidth, saveSampleButton.getHeight());
    loadClearButton.setBounds(saveSampleButton.getRight() + (int) buttonGap, (int) secondRowTop, (int) buttonWidth, loadClearButton.getHeight());
    stopButton.setBounds(loadClearButton.getRight() + (int) buttonGap, (int) secondRowTop, (int) buttonWidth, stopButton.getHeight());
}

bool ConcreteEditorContent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("wav;aif;aiff"))
            return true;
    }
    return false;
}

void ConcreteEditorContent::filesDropped(const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        const juce::File file(path);
        if (file.hasFileExtension("wav;aif;aiff"))
        {
            loadFile(file);
            break;
        }
    }
}

void ConcreteEditorContent::loadFile(const juce::File& file)
{
    // Delegates to ConcreteScreen, which owns the async load + its own loading-animation display -
    // both this path and the Session block's Load button funnel through here rather than
    // duplicating that logic at each call site.
    concreteScreen.loadFile(file);
}

ConcreteAudioProcessorEditor::ConcreteAudioProcessorEditor(ConcreteAudioProcessor& p)
    : AudioProcessorEditor(&p), content(p),
      zoomHandler(*this, content, { content.getWidth(), content.getHeight() })
{
    addAndMakeVisible(content);
}

juce::AudioProcessorEditor* ConcreteAudioProcessor::createEditor()
{
    return new ConcreteAudioProcessorEditor(*this);
}
