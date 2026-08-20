// ============================================================================
// Adapted directly from caverns-delay/Source/PluginEditor.cpp, the canonical reference
// implementation for the juce-hardware-panel-ui skill. The chassis/panel/header/footer chrome in
// paint() (before the four drawHardwareSection(...) calls), rebuildChassisTexture(), and
// drawHardwareSection() are COPY-VERBATIM - see that file's own banner comment. Shields differs from
// Caverns in having no preset combo (no factory presets exist yet) and no Timer-driven
// sync/link display logic (none of Shields's parameters are tempo-derived).
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - ShieldsTests
// links only ShieldsFDNEngine.cpp against juce_core, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* ShieldsAudioProcessor::createEditor()
{
    return new ShieldsAudioProcessorEditor(*this);
}

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 30;

    constexpr int contentPaddingTop = 18;
    constexpr int contentPaddingSide = 18;
    constexpr int contentPaddingBottom = 0;   // the footer's own box provides the gap below the
                                               // sections - stacking extra padding here on top of
                                               // that pushed the footer text low within the combined
                                               // visual gap even though it was centred in its own box
    constexpr int columnGap = 14;

    constexpr int diffusionColumnWidth = 268;
    constexpr int decayColumnWidth = 268;
    constexpr int toneColumnWidth = 268;
    constexpr int motionColumnWidth = 160;   // one knob, so meaningfully narrower than the two-knob
                                              // sections - sized to give an 88px knob the same
                                              // generous framing (~24px each side) the Mix knobs get
    // Mix (the last column) has no named width constant, matching Caverns' own pattern - it just
    // takes whatever's left in `content` after the other four are removed (~220px).

    constexpr int sectionPaddingTop = 40;   // clearance for the badge (sits inside the border, not
                                             // straddling it - see MOCKUP_GROUND_TRUTH.md)
    constexpr int sectionPaddingSide = 12;
    constexpr int sectionPaddingBottom = 20;

    constexpr int defaultKnobSize = 88;
    constexpr int sizeKnobSize = 112;        // Size reads larger - it's Shields's de facto attack-
                                              // time control, the closest thing to a "hero" knob
    constexpr int knobNameHeight = 28;
    constexpr int knobTextBoxHeight = 20;
    // Dry/Wet share defaultKnobSize (not a separate smaller size) - two of these fit side by side
    // in the Mix column (~196px usable width) since the window is widened by exactly the delta
    // needed here (1040->1132) so Diffusion/Decay/Tone's own knob spacing is untouched.
}

void ShieldsAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                    const juce::String& labelText)
{
    // Slider is a member variable, so it's default-constructed (building its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body - at that point getLookAndFeel() still resolves to JUCE's global
    // default, not ours. Slider has no parentHierarchyChanged() override to rebuild that textbox
    // once actually parented, so it's stuck with default styling unless told about the real
    // LookAndFeel explicitly here.
    slider.setLookAndFeel(&lookAndFeel);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(slider);

    // Not attachToComponent(): the mockup's DOM order inside .knob-cell is knob, then name, then
    // value - the name sits BELOW the knob, not above. Positioned manually in resized() instead,
    // in the gap ShieldsLookAndFeel's (inherited) drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

ShieldsAudioProcessorEditor::ShieldsAudioProcessorEditor(ShieldsAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("SHIELDS", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffEC6594));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Diffuse Reverb").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::bypassParamID, bypassButton);

    setupRotarySlider(diffusionSlider, diffusionLabel, "Diffusion");
    setupRotarySlider(sizeSlider, sizeLabel, "Size");
    setupRotarySlider(feedbackSlider, feedbackLabel, "Feedback");
    setupRotarySlider(dampingSlider, dampingLabel, "Damping");
    setupRotarySlider(bandwidthSlider, bandwidthLabel, "Bandwidth");
    setupRotarySlider(bitDepthSlider, bitDepthLabel, "Bit Depth");
    setupRotarySlider(wobbleSlider, wobbleLabel, "Wobble");
    setupRotarySlider(drySlider, dryLabel, "Dry");
    setupRotarySlider(wetSlider, wetLabel, "Wet");

    diffusionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::diffusionParamID, diffusionSlider);
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::sizeParamID, sizeSlider);
    feedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::feedbackParamID, feedbackSlider);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::dampingParamID, dampingSlider);
    bandwidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::bandwidthHzParamID, bandwidthSlider);
    bitDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::bitDepthParamID, bitDepthSlider);
    wobbleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::wobbleParamID, wobbleSlider);
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::dryParamID, drySlider);
    wetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ShieldsAudioProcessor::wetParamID, wetSlider);

    setSize(1306, 362);
}

ShieldsAudioProcessorEditor::~ShieldsAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void ShieldsAudioProcessorEditor::rebuildChassisTexture()
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

// COPY-VERBATIM (see banner at top of file): unbroken border + centred badge that hugs its text,
// sitting inside the border rather than straddling it.
void ShieldsAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void ShieldsAudioProcessorEditor::paint(juce::Graphics& g)
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

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to Shields's actual parameters. ---
    drawHardwareSection(g, diffusionSectionBounds, "Diffusion");
    drawHardwareSection(g, decaySectionBounds, "Decay");
    drawHardwareSection(g, toneSectionBounds, "Tone");
    drawHardwareSection(g, motionSectionBounds, "Motion");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(18.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("SHIELDS \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::centredLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::centredRight);
}

void ShieldsAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) ShieldsLookAndFeel::buttonShadowMargin));

    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "SHIELDS") + 8;
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

    auto diffusionColumn = content.removeFromLeft(diffusionColumnWidth);
    content.removeFromLeft(columnGap);
    auto decayColumn = content.removeFromLeft(decayColumnWidth);
    content.removeFromLeft(columnGap);
    auto toneColumn = content.removeFromLeft(toneColumnWidth);
    content.removeFromLeft(columnGap);
    auto motionColumn = content.removeFromLeft(motionColumnWidth);
    content.removeFromLeft(columnGap);
    auto mixColumn = content;

    diffusionSectionBounds = diffusionColumn.toFloat();
    decaySectionBounds = decayColumn.toFloat();
    toneSectionBounds = toneColumn.toFloat();
    motionSectionBounds = motionColumn.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap + built-in
    // value textbox, and ShieldsLookAndFeel's (inherited) drawRotarySlider flush-tops the circle
    // within that, leaving the name-gap blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, int knobSize, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- Diffusion: Diffusion + Size (the larger "hero" knob - Shields's attack-time control). ----
    auto diffusionInner = diffusionColumn;
    diffusionInner.removeFromTop(sectionPaddingTop);
    diffusionInner.removeFromLeft(sectionPaddingSide);
    diffusionInner.removeFromRight(sectionPaddingSide);
    diffusionInner.removeFromBottom(sectionPaddingBottom);

    auto diffusionRow = diffusionInner.removeFromTop(sizeKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto diffusionHalfWidth = diffusionRow.getWidth() / 2;
    positionKnob(diffusionRow.removeFromLeft(diffusionHalfWidth), defaultKnobSize, diffusionSlider, diffusionLabel);
    positionKnob(diffusionRow, sizeKnobSize, sizeSlider, sizeLabel);

    // ---- Decay: Feedback + Damping. ----
    auto decayInner = decayColumn;
    decayInner.removeFromTop(sectionPaddingTop);
    decayInner.removeFromLeft(sectionPaddingSide);
    decayInner.removeFromRight(sectionPaddingSide);
    decayInner.removeFromBottom(sectionPaddingBottom);

    auto decayRow = decayInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto decayHalfWidth = decayRow.getWidth() / 2;
    positionKnob(decayRow.removeFromLeft(decayHalfWidth), defaultKnobSize, feedbackSlider, feedbackLabel);
    positionKnob(decayRow, defaultKnobSize, dampingSlider, dampingLabel);

    // ---- Tone: Bandwidth + Bit Depth (the lo-fi coloration controls). ----
    auto toneInner = toneColumn;
    toneInner.removeFromTop(sectionPaddingTop);
    toneInner.removeFromLeft(sectionPaddingSide);
    toneInner.removeFromRight(sectionPaddingSide);
    toneInner.removeFromBottom(sectionPaddingBottom);

    auto toneRow = toneInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto toneHalfWidth = toneRow.getWidth() / 2;
    positionKnob(toneRow.removeFromLeft(toneHalfWidth), defaultKnobSize, bandwidthSlider, bandwidthLabel);
    positionKnob(toneRow, defaultKnobSize, bitDepthSlider, bitDepthLabel);

    // ---- Motion: a single Wobble knob (see ShieldsFDNEngine::setWobble()'s comment) - off/0 by
    // default, the one optional-modulation control from the original build order. ----
    auto motionInner = motionColumn;
    motionInner.removeFromTop(sectionPaddingTop);
    motionInner.removeFromLeft(sectionPaddingSide);
    motionInner.removeFromRight(sectionPaddingSide);
    motionInner.removeFromBottom(sectionPaddingBottom);

    auto motionRow = motionInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(motionRow, defaultKnobSize, wobbleSlider, wobbleLabel);

    // ---- Mix: independent dry/wet, as small knobs side by side (a vertical fader pair, matching
    // Caverns exactly, was tried first but read poorly at this section's shorter height). ----
    auto mixInner = mixColumn;
    mixInner.removeFromTop(sectionPaddingTop);
    mixInner.removeFromLeft(sectionPaddingSide);
    mixInner.removeFromRight(sectionPaddingSide);
    mixInner.removeFromBottom(sectionPaddingBottom);

    auto mixRow = mixInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto mixHalfWidth = mixRow.getWidth() / 2;
    positionKnob(mixRow.removeFromLeft(mixHalfWidth), defaultKnobSize, drySlider, dryLabel);
    positionKnob(mixRow, defaultKnobSize, wetSlider, wetLabel);

    rebuildChassisTexture();
}
