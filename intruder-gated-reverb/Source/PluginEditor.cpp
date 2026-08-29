// ============================================================================
// Adapted from caverns-delay/Source/PluginEditor.cpp, the canonical reference implementation for
// the juce-hardware-panel-ui skill. rebuildChassisTexture(), drawHardwareSection(), and the
// chassis/panel/header-bar/footer-bar chrome inside paint() are copied verbatim (see that file's
// own banner comment) - they don't reference any per-plugin content. Everything else (constructor,
// resized() layout, section names) is Intruder-specific, matching the approved mockup at
// mockups/intruder-mockup-v1.html: three sections (Timing, Character, Levels), no preset combo
// (matches shields-reverb - getNumPrograms() == 1 here too).
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency.
juce::AudioProcessorEditor* IntruderAudioProcessor::createEditor()
{
    return new IntruderAudioProcessorEditor(*this);
}

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 32;

    constexpr int contentPaddingTop = 22;
    constexpr int contentPaddingSide = 22;
    constexpr int contentPaddingBottom = 12;
    constexpr int columnGap = 16;

    constexpr int timingColumnWidth = 132;
    constexpr int characterColumnWidth = 142;
    // Widened from 192 to fit a 3rd small knob (Threshold) in the top row alongside Gain/Volume -
    // see PluginEditor.h's class comment. 3*smallKnobSize (68) = 204, plus sectionPaddingSide*2
    // (28) leaves comfortable breathing room without shrinking the knob size.
    constexpr int levelsColumnWidth = 250;

    constexpr int sectionPaddingTop = 40;   // clearance for the badge straddling the top border
    constexpr int sectionPaddingSide = 14;
    constexpr int sectionPaddingBottom = 16;

    constexpr int defaultKnobSize = 88;
    constexpr int smallKnobSize = 68;
    constexpr int heroKnobSize = 112;
    constexpr int knobNameHeight = 28;      // gap between the knob and its value textbox, occupied
                                             // by the name label -- mockup DOM order is knob, name,
                                             // value (not name-above-knob like a typical plugin)
    constexpr int knobTextBoxHeight = 20;
    constexpr int knobColGap = 20;          // vertical gap between stacked knobs within a section
}

void IntruderAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                      const juce::String& labelText)
{
    // Slider is a member variable, so it's default-constructed (and builds its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body -- Slider has no parentHierarchyChanged() override to rebuild that
    // textbox once actually parented, so it's stuck with default styling unless told about the
    // real LookAndFeel here explicitly.
    slider.setLookAndFeel(&lookAndFeel);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the mockup's DOM order inside .knob-cell is
    // knob, then name, then value - the name sits BELOW the knob, not above. Positioned manually
    // in resized() instead, in the gap IntruderLookAndFeel::drawRotarySlider (inherited) leaves.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

IntruderAudioProcessorEditor::IntruderAudioProcessorEditor(IntruderAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("INTRUDER", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd3d95f));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Non-Linear Gated Reverb").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.22f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff586566));
    addAndMakeVisible(tagLabel);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, IntruderAudioProcessor::bypassParamID, bypassButton);

    setupRotarySlider(decaySlider, decayLabel, "Decay");
    setupRotarySlider(preDelaySlider, preDelayLabel, "Pre-Delay");
    setupRotarySlider(lowHighSlider, lowHighLabel, "Low/High");
    setupRotarySlider(diffusionSlider, diffusionLabel, "Smoothing");
    setupRotarySlider(gainSlider, gainLabel, "Gain");
    setupRotarySlider(volumeSlider, volumeLabel, "Volume");
    setupRotarySlider(thresholdSlider, thresholdLabel, "Threshold");
    setupRotarySlider(blendSlider, blendLabel, "Blend");

    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::decaySecondsParamID, decaySlider);
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::preDelayMsParamID, preDelaySlider);
    lowHighAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::tiltDbParamID, lowHighSlider);
    diffusionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::tighterParamID, diffusionSlider);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::inputGainDbParamID, gainSlider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::outputGainDbParamID, volumeSlider);
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::triggerThresholdDbParamID, thresholdSlider);
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, IntruderAudioProcessor::mixPercentParamID, blendSlider);

    setSize(634, 500); // +58 over the original 576 for levelsColumnWidth's growth (192 -> 250)
}

IntruderAudioProcessorEditor::~IntruderAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void IntruderAudioProcessorEditor::rebuildChassisTexture()
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

// COPY-VERBATIM (see banner at top of file).
void IntruderAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
                                                        const juce::String& label)
{
    g.setColour(juce::Colour(0xffe6ece6).withAlpha(0.62f));
    g.drawRoundedRectangle(bounds, 7.0f, 3.5f);

    const auto font = lookAndFeel.getDisplayFont(12.5f).withExtraKerningFactor(0.14f);
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, label.toUpperCase());
    constexpr float badgeHeight = 25.0f;
    const auto badgeBounds = juce::Rectangle<float>(textWidth + 36.0f, badgeHeight)
                                  .withCentre({bounds.getCentreX(), bounds.getY() + 12.0f + badgeHeight * 0.5f});

    g.setColour(lookAndFeel.getAccentColour());
    g.fillRoundedRectangle(badgeBounds, 2.0f);

    g.setColour(lookAndFeel.getBadgeInkColour());
    g.setFont(font);
    g.drawText(label.toUpperCase(), badgeBounds, juce::Justification::centred);
}

void IntruderAudioProcessorEditor::paint(juce::Graphics& g)
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

        // Matching divider above the footer (per Adam's request) - Caverns itself doesn't have
        // this (just a background shade shift), a deliberate departure for Intruder.
        auto footerDividerBoundsCopy = fullPanelBounds;
        auto footerDividerArea = footerDividerBoundsCopy.removeFromBottom((float) footerHeight);
        g.setColour(juce::Colour(0xff33393b));
        g.drawLine(footerDividerArea.getX(), footerDividerArea.getY(), footerDividerArea.getRight(), footerDividerArea.getY(), 1.0f);

        g.restoreState();
    }

    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(fullPanelBounds, 8.0f, 1.0f);

    drawHardwareSection(g, timingSectionBounds, "Timing");
    drawHardwareSection(g, characterSectionBounds, "Character");
    drawHardwareSection(g, levelsSectionBounds, "Levels");

    auto footerBoundsCopy = fullPanelBounds;
    auto footerTextArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);
    footerTextArea.removeFromTop(6.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("INTRUDER \xC2\xB7 v") + JucePlugin_VersionString,
               footerTextArea.removeFromLeft(200.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerTextArea, juce::Justification::topRight);
}

void IntruderAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) IntruderLookAndFeel::buttonShadowMargin));

    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.22f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "INTRUDER") + 8;
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

    auto timingColumn = content.removeFromLeft(timingColumnWidth);
    content.removeFromLeft(columnGap);
    auto characterColumn = content.removeFromLeft(characterColumnWidth);
    content.removeFromLeft(columnGap);
    auto levelsColumn = content.removeFromLeft(levelsColumnWidth);

    timingSectionBounds = timingColumn.toFloat();
    characterSectionBounds = characterColumn.toFloat();
    levelsSectionBounds = levelsColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap + built-in
    // value textbox, and drawRotarySlider flush-tops the circle within that, leaving the name-gap
    // blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, int knobSize, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- Timing: Decay above Pre-Delay, stacked. ----
    auto timingInner = timingColumn;
    timingInner.removeFromTop(sectionPaddingTop);
    timingInner.removeFromLeft(sectionPaddingSide);
    timingInner.removeFromRight(sectionPaddingSide);
    timingInner.removeFromBottom(sectionPaddingBottom);

    const auto defaultKnobCellHeight = defaultKnobSize + knobNameHeight + knobTextBoxHeight;
    positionKnob(timingInner.removeFromTop(defaultKnobCellHeight), defaultKnobSize, decaySlider, decayLabel);
    timingInner.removeFromTop(knobColGap);
    positionKnob(timingInner.removeFromTop(defaultKnobCellHeight), defaultKnobSize, preDelaySlider, preDelayLabel);

    // ---- Character: Low/High above Diffusion, stacked. ----
    auto characterInner = characterColumn;
    characterInner.removeFromTop(sectionPaddingTop);
    characterInner.removeFromLeft(sectionPaddingSide);
    characterInner.removeFromRight(sectionPaddingSide);
    characterInner.removeFromBottom(sectionPaddingBottom);

    positionKnob(characterInner.removeFromTop(defaultKnobCellHeight), defaultKnobSize, lowHighSlider, lowHighLabel);
    characterInner.removeFromTop(knobColGap);
    positionKnob(characterInner.removeFromTop(defaultKnobCellHeight), defaultKnobSize, diffusionSlider, diffusionLabel);

    // ---- Levels: Gain/Volume as a small pair on top, Blend as a larger "hero" knob below,
    // centred - matches the approved mockup exactly (Blend is the plugin's primary continuous
    // control, same hierarchy convention as Caverns' larger L/R Time knobs). ----
    auto levelsInner = levelsColumn;
    levelsInner.removeFromTop(sectionPaddingTop);
    levelsInner.removeFromLeft(sectionPaddingSide);
    levelsInner.removeFromRight(sectionPaddingSide);
    levelsInner.removeFromBottom(sectionPaddingBottom);

    const auto smallKnobCellHeight = smallKnobSize + knobNameHeight + knobTextBoxHeight;
    auto smallRow = levelsInner.removeFromTop(smallKnobCellHeight);
    const auto smallThirdWidth = smallRow.getWidth() / 3;
    positionKnob(smallRow.removeFromLeft(smallThirdWidth), smallKnobSize, gainSlider, gainLabel);
    positionKnob(smallRow.removeFromLeft(smallThirdWidth), smallKnobSize, volumeSlider, volumeLabel);
    positionKnob(smallRow, smallKnobSize, thresholdSlider, thresholdLabel);

    levelsInner.removeFromTop(knobColGap);
    const auto heroKnobCellHeight = heroKnobSize + knobNameHeight + knobTextBoxHeight;
    positionKnob(levelsInner.removeFromTop(heroKnobCellHeight), heroKnobSize, blendSlider, blendLabel);

    rebuildChassisTexture();
}
