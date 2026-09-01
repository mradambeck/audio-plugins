// ============================================================================
// Adapted from the juce-hardware-panel-ui skill's canonical reference
// (caverns-delay/Source/PluginEditor.cpp). rebuildChassisTexture(),
// drawHardwareSection(), and the chassis/panel/header/footer chrome inside
// paint() are copied verbatim from that file. The constructor, resized(),
// section names, and which knob goes where are plugin-specific, matched to
// Flux's own LFO/Stages/Color/Mix parameter set.
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

#include "../../common/Presets/FactoryPreset.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - FluxTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* FluxAudioProcessor::createEditor()
{
    return new FluxAudioProcessorEditor(*this);
}

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 62;
    constexpr int footerHeight = 30;

    constexpr int contentPaddingTop = 22;
    constexpr int contentPaddingSide = 22;
    constexpr int contentPaddingBottom = 11;
    constexpr int columnGap = 14;

    constexpr int lfoColumnWidth = 340;
    // Widened from 130 to fit the Shift button in its own column to the left of the LED/number
    // list, side by side - Mix (the flexible/remainder column) absorbs the difference and still
    // has plenty of room around its fixed-width fader pair.
    constexpr int stagesColumnWidth = 180;
    constexpr int colorColumnWidth = 260;

    constexpr int sectionPaddingTop = 40;   // clearance for the badge straddling the top border
    constexpr int sectionPaddingSide = 12;
    constexpr int sectionPaddingBottom = 12;

    constexpr int defaultKnobSize = 88;
    constexpr int heroKnobSize = 112;       // Blend's larger "primary control" cap, matching the
                                             // treatment Caverns gives its L/R Time knobs
    constexpr int knobNameHeight = 28;      // gap between the knob and its value textbox, occupied
                                             // by the name label -- mockup DOM order is knob, name,
                                             // value (not name-above-knob like a typical plugin)
    constexpr int knobTextBoxHeight = 20;

    constexpr int buttonRowHeight = 28;
    constexpr int syncButtonWidth = 96;
    constexpr int divisionComboWidth = 112;
    constexpr int syncDivisionGap = 10;

    constexpr int shiftButtonWidth = 48;
    constexpr int shiftButtonHeight = 24;
    constexpr int shiftButtonColumnWidth = 64;
    constexpr int shiftButtonTopOffset = 24;
    constexpr int stagesInnerColumnGap = 10;
}

void FluxEditorContent::commitRawRateParam(float hz)
{
    // Writes straight to the parameter rather than through its Slider, since Slider::setValue()
    // silently drops the notification whenever the slider's displayed value already matches -
    // which it always does here, because the timer callback kept it visually in sync throughout.
    if (auto* param = processorRef.apvts.getParameter(FluxAudioProcessor::rateParamID))
        param->setValueNotifyingHost(param->convertTo0to1(hz));
}

void FluxEditorContent::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                  const juce::String& labelText)
{
    // Slider is a member variable, so it's default-constructed (and builds its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body -- at that point getLookAndFeel() still resolves to JUCE's global
    // default, not ours. Unlike ComboBox, Slider has no parentHierarchyChanged() override to
    // rebuild that textbox once actually parented, so it's stuck with default styling unless
    // explicitly told about the real LookAndFeel here.
    slider.setLookAndFeel(&lookAndFeel);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the name sits BELOW the knob (between it and
    // the value textbox), not above like attachToComponent(..., false) would place it. Positioned
    // manually in resized() instead, in the gap FluxLookAndFeel::drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void FluxEditorContent::setupShiftButton()
{
    shiftButton.setLookAndFeel(&lookAndFeel);
    // No text - just a small blank pushbutton (see FluxLookAndFeel::drawButtonText, which simply
    // draws whatever getButtonText() returns).
    addAndMakeVisible(shiftButton);

    // Momentary trigger, not a toggle: each click advances to the next-largest stage count in
    // FluxAudioProcessor::getStageChoices() (2, 4, 6, 8, 12, 24, 36, ...), wrapping from the
    // largest back to the smallest rather than stopping at either end.
    shiftButton.onClick = [this]
    {
        auto* param = processorRef.apvts.getParameter(FluxAudioProcessor::stagesParamID);
        if (param == nullptr)
            return;
        const auto currentIndex = (int) processorRef.apvts.getRawParameterValue(FluxAudioProcessor::stagesParamID)->load();
        const auto nextIndex = (currentIndex + 1) % numStageChoices;
        param->setValueNotifyingHost(param->convertTo0to1((float) nextIndex));
    };
}

FluxEditorContent::FluxEditorContent(FluxAudioProcessor& p)
    : processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("FLUX", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb47bf0));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Analog Phase Shifter").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8a7f96));
    addAndMakeVisible(tagLabel);

    // See common/Presets/FactoryPreset.h's setupPresetCombo() for why it's left unselected on
    // startup rather than showing the first preset's name.
    wildjag::setupPresetCombo(presetCombo, lookAndFeel, *this, processorRef);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, FluxAudioProcessor::bypassParamID, bypassButton);

    // ---- LFO ----
    syncButton.setLookAndFeel(&lookAndFeel);
    syncButton.setButtonText("SYNC");
    addAndMakeVisible(syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, FluxAudioProcessor::syncParamID, syncButton);

    // While Sync is on, the Rate knob only *displays* the tempo-derived value - the raw Rate
    // parameter underneath sits wherever it was last left, since the timer callback below updates
    // the slider with dontSendNotification. If Sync then switches off without writing that
    // displayed value back into the raw parameter first, playback would silently snap to whatever
    // stale number was last there even though the knob still shows the value it was just running
    // at. See commitRawRateParam() for why this has to go straight to the parameter rather than
    // through the Slider.
    syncButton.onClick = [this]
    {
        if (!syncButton.getToggleState())
            commitRawRateParam(processorRef.getCurrentLfoRateHz());
    };

    divisionCombo.setLookAndFeel(&lookAndFeel);
    divisionCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0d4ec));
    divisionCombo.addItemList(FluxAudioProcessor::getDivisionChoices(), 1);
    addAndMakeVisible(divisionCombo);
    divisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, FluxAudioProcessor::divisionParamID, divisionCombo);

    setupRotarySlider(rateSlider, rateLabel, "Rate");
    setupRotarySlider(depthSlider, depthLabel, "Depth");
    setupRotarySlider(shapeSlider, shapeLabel, "Shape");

    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::rateParamID, rateSlider);
    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::depthParamID, depthSlider);
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::shapeParamID, shapeSlider);

    // ---- Stages ----
    setupShiftButton();

    // ---- Color ----
    setupRotarySlider(offsetSlider, offsetLabel, "Offset");
    setupRotarySlider(feedbackSlider, feedbackLabel, "Feedback");
    setupRotarySlider(brightnessSlider, brightnessLabel, "Brightness");
    setupRotarySlider(gritSlider, gritLabel, "Grit");

    offsetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::offsetParamID, offsetSlider);
    feedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::feedbackParamID, feedbackSlider);
    brightnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::brightnessParamID, brightnessSlider);
    gritAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::gritParamID, gritSlider);

    // ---- Mix ----
    setupRotarySlider(blendSlider, blendLabel, "Blend");

    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, FluxAudioProcessor::blendParamID, blendSlider);

    setSize(1100, 500);

    startTimerHz(30);
}

FluxEditorContent::~FluxEditorContent()
{
    setLookAndFeel(nullptr);
}

void FluxEditorContent::timerCallback()
{
    const auto syncOn = syncButton.getToggleState();

    divisionCombo.setEnabled(syncOn);
    divisionCombo.setAlpha(syncOn ? 1.0f : 0.35f);

    rateSlider.setEnabled(!syncOn);
    rateSlider.setAlpha(syncOn ? 0.5f : 1.0f);
    if (syncOn)
        rateSlider.setValue(processorRef.getCurrentLfoRateHz(), juce::dontSendNotification);

    const auto currentStageIndex = (int) processorRef.apvts.getRawParameterValue(FluxAudioProcessor::stagesParamID)->load();
    if (currentStageIndex != lastPaintedStageIndex)
    {
        lastPaintedStageIndex = currentStageIndex;
        repaint(stageListBounds);
    }
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void FluxEditorContent::rebuildChassisTexture()
{
    const auto w = getWidth();
    const auto h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    chassisTexture = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics tg(chassisTexture);

    constexpr float period = 4.0f;
    const auto fw = (float) w, fh = (float) h;

    for (float offset = -fh; offset < fw; offset += period)
    {
        tg.setColour(juce::Colours::white.withAlpha(0.09f));
        tg.drawLine(offset, 0.0f, offset + fh, fh, 1.6f);
        tg.setColour(juce::Colours::black.withAlpha(0.20f));
        tg.drawLine(offset + 2.0f, 0.0f, offset + 2.0f + fh, fh, 1.6f);
    }
    for (float offset = 0.0f; offset < fw + fh; offset += period)
    {
        tg.setColour(juce::Colours::white.withAlpha(0.07f));
        tg.drawLine(offset, 0.0f, offset - fh, fh, 1.6f);
        tg.setColour(juce::Colours::black.withAlpha(0.16f));
        tg.drawLine(offset + 2.0f, 0.0f, offset + 2.0f - fh, fh, 1.6f);
    }

    juce::ColourGradient scuff1(juce::Colours::transparentWhite, 0.0f, 0.0f,
                                 juce::Colours::transparentWhite, fw, fh, false);
    scuff1.addColour(0.35, juce::Colours::transparentWhite);
    scuff1.addColour(0.46, juce::Colours::white.withAlpha(0.035f));
    scuff1.addColour(0.60, juce::Colours::transparentWhite);
    tg.setGradientFill(scuff1);
    tg.fillRect(0, 0, w, h);

    juce::ColourGradient hotspot(juce::Colours::white.withAlpha(0.07f), fw * 0.15f, fh * -0.2f,
                                  juce::Colours::transparentWhite, fw * 0.15f, fh * 0.7f, true);
    tg.setGradientFill(hotspot);
    tg.fillRect(0, 0, w, h);
}

// COPY-VERBATIM (see banner at top of file): unbroken border + centred badge that hugs its
// text, sitting inside the border rather than straddling it.
void FluxEditorContent::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
                                                     const juce::String& label)
{
    g.setColour(juce::Colour(0xffe6ece6).withAlpha(0.62f));
    g.drawRoundedRectangle(bounds, 7.0f, 3.5f);

    const auto font = lookAndFeel.getDisplayFont(12.5f).withExtraKerningFactor(0.14f);
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, label.toUpperCase());
    constexpr float badgeHeight = 25.0f;
    const auto badgeBounds = juce::Rectangle<float>(textWidth + 36.0f, badgeHeight)
                                  .withCentre({bounds.getCentreX(), bounds.getY() + 12.0f + badgeHeight * 0.5f});

    // 90%, not fully opaque - a deliberate deviation from the copy-verbatim reference elsewhere
    // in this catalog, specific to Flux's badges.
    g.setColour(lookAndFeel.getAccentColour().withAlpha(0.9f));
    g.fillRoundedRectangle(badgeBounds, 2.0f);

    g.setColour(lookAndFeel.getBadgeInkColour());
    g.setFont(font);
    g.drawText(label.toUpperCase(), badgeBounds, juce::Justification::centred);
}

// Static read-only display, not a Component per row - the Shift button (see setupShiftButton())
// is the only interactive control in this section. An LED lights up next to whichever stage
// count is currently selected; the rest stay dim.
void FluxEditorContent::drawStageList(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto& choices = FluxAudioProcessor::getStageChoices();
    const auto currentIndex = (int) processorRef.apvts.getRawParameterValue(FluxAudioProcessor::stagesParamID)->load();

    const auto rowHeight = bounds.getHeight() / (float) numStageChoices;
    const auto font = lookAndFeel.getDisplayFont(13.0f).withExtraKerningFactor(0.05f);

    for (int i = 0; i < numStageChoices; ++i)
    {
        auto row = bounds.removeFromTop(rowHeight);
        const auto isSelected = (i == currentIndex);

        constexpr float ledDiameter = 9.0f;
        const auto ledBounds = row.removeFromLeft(ledDiameter + 14.0f).withSizeKeepingCentre(ledDiameter, ledDiameter);

        if (isSelected)
        {
            // A soft blurred glow, matching drawToggleButton's own LED treatment - not a crisp
            // ring outline.
            juce::Path ledPath;
            ledPath.addEllipse(ledBounds);
            juce::DropShadow(lookAndFeel.getLedOnColour().withAlpha(0.85f), 7, {0, 0}).drawForPath(g, ledPath);
        }
        g.setColour(isSelected ? lookAndFeel.getLedOnColour() : lookAndFeel.getLedOffColour());
        g.fillEllipse(ledBounds);

        g.setFont(font);
        g.setColour(juce::Colour(0xffdcece9).withAlpha(isSelected ? 1.0f : 0.6f));
        g.drawText(choices[i], row, juce::Justification::centredLeft);
    }
}

void FluxEditorContent::paint(juce::Graphics& g)
{
    const auto deviceBounds = getLocalBounds().toFloat();
    juce::Path devicePath;
    devicePath.addRoundedRectangle(deviceBounds, 14.0f);

    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 24, {0, 10}).drawForPath(g, devicePath);

    g.saveState();
    g.reduceClipRegion(devicePath);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c1f20), 0.0f, 0.0f,
                                            juce::Colour(0xff0a0c0d), (float) getWidth(), (float) getHeight(), false));
    g.fillAll();
    if (chassisTexture.isValid())
        g.drawImageAt(chassisTexture, 0, 0);

    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawLine(deviceBounds.getX() + 14.0f, deviceBounds.getY() + 1.5f, deviceBounds.getRight() - 14.0f, deviceBounds.getY() + 1.5f, 1.5f);

    juce::ColourGradient bottomShadow(juce::Colours::transparentBlack, 0.0f, deviceBounds.getBottom() - 18.0f,
                                       juce::Colours::black.withAlpha(0.45f), 0.0f, deviceBounds.getBottom(), false);
    g.setGradientFill(bottomShadow);
    g.fillRect(deviceBounds.withTop(deviceBounds.getBottom() - 18.0f));
    g.restoreState();

    const auto fullPanelBounds = deviceBounds.reduced((float) chassisMargin);

    {
        juce::Path panelClip;
        panelClip.addRoundedRectangle(fullPanelBounds, 8.0f);
        g.saveState();
        g.reduceClipRegion(panelClip);

        juce::ColourGradient panelGradient(juce::Colour(0xff262d2f), fullPanelBounds.getX(), fullPanelBounds.getY(),
                                            juce::Colour(0xff171c1d), fullPanelBounds.getRight(), fullPanelBounds.getBottom(), false);
        panelGradient.addColour(0.55, juce::Colour(0xff1d2325));
        g.setGradientFill(panelGradient);
        g.fillRect(fullPanelBounds);

        juce::ColourGradient hotspot(juce::Colours::white.withAlpha(0.10f), fullPanelBounds.getX(), fullPanelBounds.getY(),
                                      juce::Colours::transparentWhite, fullPanelBounds.getCentreX(), fullPanelBounds.getBottom(), true);
        g.setGradientFill(hotspot);
        g.fillRect(fullPanelBounds);

        juce::ColourGradient vignette(juce::Colours::transparentBlack, fullPanelBounds.getCentreX(), fullPanelBounds.getCentreY(),
                                       juce::Colours::black.withAlpha(0.16f), fullPanelBounds.getX(), fullPanelBounds.getY(), true);
        vignette.addColour(0.82, juce::Colours::transparentBlack);
        g.setGradientFill(vignette);
        g.fillRect(fullPanelBounds);

        g.setColour(juce::Colours::white.withAlpha(0.09f));
        g.drawLine(fullPanelBounds.getX(), fullPanelBounds.getY() + 0.5f, fullPanelBounds.getRight(), fullPanelBounds.getY() + 0.5f, 1.0f);

        auto headerArea = fullPanelBounds.withHeight((float) headerHeight);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff14181a), headerArea.getTopLeft(),
                                                juce::Colour(0xff0d1011), headerArea.getBottomLeft(), false));
        g.fillRect(headerArea);
        g.setColour(juce::Colour(0xff33393b));
        g.drawLine(headerArea.getX(), headerArea.getBottom(), headerArea.getRight(), headerArea.getBottom(), 1.0f);

        g.restoreState();
    }

    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(fullPanelBounds, 8.0f, 1.0f);

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to Flux's own parameters. ---
    drawHardwareSection(g, lfoSectionBounds, "LFO");
    drawHardwareSection(g, stagesSectionBounds, "Stages");
    drawHardwareSection(g, colorSectionBounds, "Color");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    drawStageList(g, stageListBounds.toFloat());
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("FLUX \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void FluxEditorContent::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) FluxLookAndFeel::buttonShadowMargin));
    header.removeFromRight(14);
    presetCombo.setBounds(header.removeFromRight(128).withSizeKeepingCentre(128, 28));

    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "FLUX") + 8;
    const auto baselineY = (float) header.getY() + (float) header.getHeight() * 0.62f;

    auto titleBounds = header.removeFromLeft(titleWidth);
    titleBounds.setY((int) (baselineY - titleFont.getAscent()));
    titleBounds.setHeight((int) std::ceil(titleFont.getHeight()));
    titleLabel.setBounds(titleBounds);

    header.removeFromLeft(12);
    auto tagBounds = header;
    tagBounds.setY((int) (baselineY - tagFont.getAscent()));
    tagBounds.setHeight((int) std::ceil(tagFont.getHeight()));
    tagLabel.setBounds(tagBounds);

    panelArea.removeFromBottom(footerHeight);

    auto content = panelArea;
    content.removeFromLeft(contentPaddingSide);
    content.removeFromRight(contentPaddingSide);
    content.removeFromTop(contentPaddingTop);
    content.removeFromBottom(contentPaddingBottom);

    auto lfoColumn = content.removeFromLeft(lfoColumnWidth);
    content.removeFromLeft(columnGap);
    auto stagesColumn = content.removeFromLeft(stagesColumnWidth);
    content.removeFromLeft(columnGap);
    auto colorColumn = content.removeFromLeft(colorColumnWidth);
    content.removeFromLeft(columnGap);
    auto mixColumn = content;

    lfoSectionBounds = lfoColumn.toFloat();
    stagesSectionBounds = stagesColumn.toFloat();
    colorSectionBounds = colorColumn.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap +
    // built-in value textbox, and FluxLookAndFeel::drawRotarySlider flush-tops the circle within
    // that, leaving the name-gap blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, int knobSize, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- LFO: Sync button, Division combo (only live while synced), then Rate/Depth/Shape. ----
    auto lfoInner = lfoColumn;
    lfoInner.removeFromTop(sectionPaddingTop);
    lfoInner.removeFromLeft(sectionPaddingSide);
    lfoInner.removeFromRight(sectionPaddingSide);
    lfoInner.removeFromBottom(sectionPaddingBottom);

    lfoInner.removeFromTop(14);
    auto syncDivisionRow = lfoInner.removeFromTop(buttonRowHeight)
                                .withSizeKeepingCentre(syncButtonWidth + syncDivisionGap + divisionComboWidth, buttonRowHeight);
    syncButton.setBounds(syncDivisionRow.removeFromLeft(syncButtonWidth).expanded((int) FluxLookAndFeel::buttonShadowMargin));
    syncDivisionRow.removeFromLeft(syncDivisionGap);
    divisionCombo.setBounds(syncDivisionRow);

    lfoInner.removeFromTop(20);
    auto lfoKnobRow = lfoInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto lfoThirdWidth = lfoKnobRow.getWidth() / 3;
    positionKnob(lfoKnobRow.removeFromLeft(lfoThirdWidth), defaultKnobSize, rateSlider, rateLabel);
    positionKnob(lfoKnobRow.removeFromLeft(lfoThirdWidth), defaultKnobSize, depthSlider, depthLabel);
    positionKnob(lfoKnobRow, defaultKnobSize, shapeSlider, shapeLabel);

    // ---- Stages: a small Shift button in its own column at the top left, and the read-only
    // LED/text list (drawn in drawStageList(), not laid out here beyond reserving its bounds)
    // filling the column to its right. ----
    auto stagesInner = stagesColumn;
    stagesInner.removeFromTop(sectionPaddingTop);
    stagesInner.removeFromLeft(sectionPaddingSide);
    stagesInner.removeFromRight(sectionPaddingSide);
    stagesInner.removeFromBottom(sectionPaddingBottom);

    stagesInner.removeFromTop(14);
    auto shiftButtonColumn = stagesInner.removeFromLeft(shiftButtonColumnWidth);
    stagesInner.removeFromLeft(stagesInnerColumnGap);

    shiftButtonColumn.removeFromTop(shiftButtonTopOffset);
    auto shiftBounds = shiftButtonColumn.removeFromTop(shiftButtonHeight).withSizeKeepingCentre(shiftButtonWidth, shiftButtonHeight);
    shiftButton.setBounds(shiftBounds.expanded((int) FluxLookAndFeel::buttonShadowMargin));

    stageListBounds = stagesInner;

    // ---- Color: two rows of two -- Offset/Feedback, then Brightness/Grit. ----
    auto colorInner = colorColumn;
    colorInner.removeFromTop(sectionPaddingTop);
    colorInner.removeFromLeft(sectionPaddingSide);
    colorInner.removeFromRight(sectionPaddingSide);
    colorInner.removeFromBottom(sectionPaddingBottom);

    colorInner.removeFromTop(14);
    auto colorRow1 = colorInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto colorHalfWidth = colorRow1.getWidth() / 2;
    positionKnob(colorRow1.removeFromLeft(colorHalfWidth), defaultKnobSize, offsetSlider, offsetLabel);
    positionKnob(colorRow1, defaultKnobSize, feedbackSlider, feedbackLabel);

    colorInner.removeFromTop(8);
    auto colorRow2 = colorInner;
    positionKnob(colorRow2.removeFromLeft(colorHalfWidth), defaultKnobSize, brightnessSlider, brightnessLabel);
    positionKnob(colorRow2, defaultKnobSize, gritSlider, gritLabel);

    // ---- Mix: a single hero-sized Blend knob (equal-power crossfade, not independent Dry/Wet
    // gains) - left = 100% dry, right = 100% wet. The larger cap size matches the "primary
    // control" treatment Caverns gives its L/R Time knobs, since Blend is the one control in
    // this section rather than one of a pair. ----
    auto mixInner = mixColumn;
    mixInner.removeFromTop(sectionPaddingTop);
    mixInner.removeFromLeft(sectionPaddingSide);
    mixInner.removeFromRight(sectionPaddingSide);
    mixInner.removeFromBottom(sectionPaddingBottom);

    mixInner.removeFromTop(14);
    auto blendCell = mixInner.removeFromTop(heroKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(blendCell, heroKnobSize, blendSlider, blendLabel);

    rebuildChassisTexture();
}

FluxAudioProcessorEditor::FluxAudioProcessorEditor(FluxAudioProcessor& p)
    : AudioProcessorEditor(&p), content(p), zoomHandler(*this, content, {1100, 500})
{
    addAndMakeVisible(content);
}
