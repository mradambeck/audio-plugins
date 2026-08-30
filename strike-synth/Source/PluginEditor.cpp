// Hardware-panel editor, matching the approved mockup at mockups/strike-mockup.html pixel-for-
// pixel (reference screenshot: mockups/strike-mockup-reference.png). Adapted from Gradient's
// reference implementation of the juce-hardware-panel-ui skill (Source/PluginEditor.cpp) -
// COPY-VERBATIM: rebuildChassisTexture() and the chassis/panel/header-bar/footer-bar chrome inside
// paint() (drawHardwareSection() has one small PLUGIN-SPECIFIC deviation - see its own comment).
// PLUGIN-SPECIFIC: the constructor (which controls exist, labels, wordmark/tag text, window size)
// and resized() (Strike's own layout, matched to the approved mockup's rendered geometry).

#include "PluginEditor.h"
#include "BinaryData.h"

juce::AudioProcessorEditor* StrikeAudioProcessor::createEditor()
{
    return new StrikeAudioProcessorEditor(*this);
}

namespace
{
    // Everything below is the original approved-mockup geometry scaled by uiScale (0.8x) so the
    // whole window fits comfortably on smaller screens - per user request, the plugin was too
    // tall at its original 980x935. Text sizes deliberately are NOT scaled here: the knob-name/
    // value/combo/button fonts all come from the shared HardwarePanelLookAndFeel (fixed tokens
    // used across every plugin in the catalog, already close to a legibility floor at their
    // original sizes) - only the geometry around that fixed-size text shrinks. Title/tag/badge
    // fonts are the exception, since those are hand-painted directly in this file (no shared-code
    // conflict) and are scaled a little for overall proportion - see their own font-size literals
    // in the constructor/drawHardwareSection.
    constexpr float uiScale = 0.8f;
    constexpr int scaled(int v) { return (int) (v * uiScale); }

    constexpr int chassisMargin = scaled(15);
    constexpr int headerHeight = scaled(64);
    constexpr int footerHeight = scaled(31);

    constexpr int contentPaddingTop = scaled(18);
    constexpr int contentPaddingSide = scaled(16);
    constexpr int contentPaddingBottom = scaled(8);
    constexpr int rowGap = scaled(12);
    constexpr int columnGap = scaled(12);

    // Column widths and row heights matched to the approved mockup's rendered layout (six
    // sections, two rows of three - Strum/Color wider than Output on top, Filter widest on
    // bottom since it carries the most controls). See mockups/strike-mockup.html's CSS flex
    // ratios (section-narrow: 0.56, #filter-section: 1.1) for where these proportions come from.
    constexpr int strumWidth = scaled(349);
    constexpr int colorWidth = scaled(349);
    constexpr int outputWidth = scaled(196);
    constexpr int ringModWidth = scaled(188);
    constexpr int crossCoupleWidth = scaled(336);
    constexpr int filterWidth = scaled(370);
    constexpr int row1Height = scaled(398);
    constexpr int row2Height = scaled(374);

    constexpr int sectionPaddingTop = scaled(44);   // clearance below the badge

    constexpr int defaultKnobSize = scaled(88);
    constexpr int heroKnobSize = scaled(112);
    constexpr int knobBlockExtra = scaled(32);      // combined height for the name label + value textbox
    constexpr int knobNameHeight = scaled(16);
    constexpr int knobTextBoxHeight = scaled(16);
    constexpr int knobGap = scaled(22);             // gap between two side-by-side knobs
    constexpr int filterKnobGap = scaled(17);       // gap between Filter's three side-by-side knobs

    constexpr int smallGap = scaled(14);            // gap between stacked rows within a section
    constexpr int comboToKnobGap = scaled(6);       // gap between a knob block and its attached combo

    constexpr int buttonRowHeight = scaled(31);
    constexpr int comboRowHeight = scaled(29);
    constexpr int noiseColorComboWidth = scaled(124);
    constexpr int waveshaperComboWidth = scaled(96);
}

void StrikeAudioProcessorEditor::setupRotaryKnob(StrikeRotaryKnob& knob, const juce::String& labelText,
                                                     const juce::String& paramID)
{
    // Slider is a member variable, so it's default-constructed (and builds its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body - Slider has no parentHierarchyChanged() override to rebuild that
    // textbox once actually parented, so it needs to be told about the real LookAndFeel here.
    knob.slider.setLookAndFeel(&lookAndFeel);
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    // -135deg to +135deg (a 270deg sweep) - matches the mockup's knob generator exactly
    // (mockups/strike-mockup.html's gen_knob.py used START_DEG=-135/END_DEG=135), not
    // Gradient's own wider 288deg sweep, which was tuned to Gradient's own separate mockup.
    knob.slider.setRotaryParameters(-juce::MathConstants<float>::pi * 0.75f,
                                     juce::MathConstants<float>::pi * 0.75f, true);
    addAndMakeVisible(knob.slider);

    // Not attachToComponent(): the mockup's DOM order inside .knob-cell is knob, then name, then
    // value - the name sits BELOW the knob (between it and the value textbox), not above like
    // attachToComponent(..., false) would place it. Positioned manually in resized() instead.
    knob.nameLabel.setText(labelText.toUpperCase(), juce::dontSendNotification);
    knob.nameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(knob.nameLabel);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, paramID, knob.slider);
}

void StrikeAudioProcessorEditor::setupToggle(StrikeToggle& toggle, const juce::String& buttonText,
                                                 const juce::String& paramID)
{
    toggle.button.setLookAndFeel(&lookAndFeel);
    toggle.button.setButtonText(buttonText);
    addAndMakeVisible(toggle.button);
    toggle.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, paramID, toggle.button);
}

void StrikeAudioProcessorEditor::setupCombo(StrikeCombo& combo, const juce::StringArray& items,
                                                const juce::String& paramID)
{
    combo.combo.setLookAndFeel(&lookAndFeel);
    combo.combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    combo.combo.addItemList(items, 1);
    addAndMakeVisible(combo.combo);
    combo.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, paramID, combo.combo);
}

StrikeAudioProcessorEditor::StrikeAudioProcessorEditor(StrikeAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("STRIKE", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(20.8f).withExtraKerningFactor(0.04f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6bc490));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("String Modeling Synth").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(8.8f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    setupToggle(monoToggle, "Mono", StrikeAudioProcessor::monoParamID);

    // ---- STRUM ----
    setupRotaryKnob(decayKnob, "Decay", StrikeAudioProcessor::dampingParamID);
    setupRotaryKnob(brightnessKnob, "Brightness", StrikeAudioProcessor::brightnessParamID);
    setupRotaryKnob(bowAmountKnob, "Pluck / Bow", StrikeAudioProcessor::bowAmountParamID);
    setupRotaryKnob(bowForceKnob, "Bow Force", StrikeAudioProcessor::bowForceParamID);
    setupCombo(noiseColorCombo, {"Cold", "Warm", "Dark"}, StrikeAudioProcessor::noiseColorParamID);

    // ---- COLOR ----
    // Off = Pre Filter, On = Post Filter (see PluginProcessor.cpp's distortionPositionParamID) -
    // an LED toggle rather than a combo, per user request during mockup review.
    setupToggle(postFilterToggle, "Post Filter", StrikeAudioProcessor::distortionPositionParamID);
    setupRotaryKnob(structureKnob, "Structure", StrikeAudioProcessor::structureParamID);
    setupRotaryKnob(stringPositionKnob, "Position", StrikeAudioProcessor::positionParamID);
    setupRotaryKnob(waveshapeKnob, "Waveshape", StrikeAudioProcessor::waveshapeParamID);
    setupCombo(waveshaperTypeCombo, {"Fold", "BitCrush"}, StrikeAudioProcessor::waveshaperTypeParamID);

    // ---- OUTPUT ----
    setupRotaryKnob(volumeKnob, "Volume", StrikeAudioProcessor::outputLevelParamID);

    // ---- RING MOD ----
    setupRotaryKnob(ringModAmountKnob, "Amount", StrikeAudioProcessor::ringModAmountParamID);
    setupRotaryKnob(ringModFrequencyKnob, "Frequency", StrikeAudioProcessor::ringModFrequencyParamID);

    // ---- CROSS COUPLE ----
    // Off = Single, On = Dual (see PluginProcessor.cpp's topologyParamID).
    setupToggle(crossCoupleOnToggle, "On", StrikeAudioProcessor::topologyParamID);
    setupRotaryKnob(crossCoupleAmountKnob, "Amount", StrikeAudioProcessor::crossCoupleParamID);
    setupRotaryKnob(coupleDelayKnob, "Delay", StrikeAudioProcessor::coupleDelayParamID);
    setupRotaryKnob(detuneKnob, "Detune", StrikeAudioProcessor::detuneParamID);

    // ---- FILTER ----
    // Off = Two-Point Average, On = Resonant (see PluginProcessor.cpp's loopFilterTypeParamID).
    setupToggle(filterOnToggle, "On", StrikeAudioProcessor::loopFilterTypeParamID);
    setupRotaryKnob(filterCutoffKnob, "Cutoff", StrikeAudioProcessor::filterCutoffParamID);
    setupRotaryKnob(resonanceKnob, "Resonance", StrikeAudioProcessor::resonanceParamID);
    setupRotaryKnob(filterAttackKnob, "Attack", StrikeAudioProcessor::filterAttackParamID);
    setupRotaryKnob(filterEnvAmountKnob, "Envelope", StrikeAudioProcessor::filterEnvAmountParamID);
    setupRotaryKnob(filterDecayKnob, "Decay", StrikeAudioProcessor::filterDecayParamID);

    setSize((int) (980 * uiScale), (int) (935 * uiScale));
}

StrikeAudioProcessorEditor::~StrikeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void StrikeAudioProcessorEditor::rebuildChassisTexture()
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

// Unbroken border + centred badge that hugs its text, sitting inside the border rather than
// straddling it - matches every other plugin's drawHardwareSection EXCEPT the badge fill colour
// (see comment below).
void StrikeAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
                                                          const juce::String& label)
{
    g.setColour(juce::Colour(0xffe6ece6).withAlpha(0.62f));
    g.drawRoundedRectangle(bounds, 7.0f, 3.0f);

    const auto font = lookAndFeel.getDisplayFont(9.2f).withExtraKerningFactor(0.09f);
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, label.toUpperCase());
    constexpr float badgeHeight = 17.6f;
    const auto badgeBounds = juce::Rectangle<float>(textWidth + 25.6f, badgeHeight)
                                  .withCentre({bounds.getCentreX(), bounds.getY() + 9.6f + badgeHeight * 0.5f});

    // PLUGIN-SPECIFIC: badges are deliberately brighter than plain accentMuted - the midpoint
    // between accentMuted and accentBrightLo, per user request during mockup review ("make the
    // badges a little brighter, halfway between where it currently is and the full highlight
    // color"). Every other plugin's drawHardwareSection uses lookAndFeel.getAccentColour()
    // directly for the badge fill; this is Strike's one intentional deviation from that.
    g.setColour(lookAndFeel.getAccentColour().interpolatedWith(juce::Colour{0xff469c6a}, 0.5f));
    g.fillRoundedRectangle(badgeBounds, 4.0f);

    g.setColour(lookAndFeel.getBadgeInkColour());
    g.setFont(font);
    g.drawText(label.toUpperCase(), badgeBounds, juce::Justification::centred);
}

void StrikeAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto deviceBounds = getLocalBounds().toFloat();
    juce::Path devicePath;
    devicePath.addRoundedRectangle(deviceBounds, 16.0f);

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
        panelClip.addRoundedRectangle(fullPanelBounds, 9.0f);
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

    // Thin dark seam between the chassis and the panel face, per mockup review.
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(fullPanelBounds, 9.0f, 1.0f);

    drawHardwareSection(g, strumSectionBounds, "Strum");
    drawHardwareSection(g, colorSectionBounds, "Color");
    drawHardwareSection(g, outputSectionBounds, "Output");
    drawHardwareSection(g, ringModSectionBounds, "Ring Mod");
    drawHardwareSection(g, crossCoupleSectionBounds, "Cross Couple");
    drawHardwareSection(g, filterSectionBounds, "Filter");

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("STRIKE \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void StrikeAudioProcessorEditor::positionKnob(juce::Rectangle<int> topLeftCell, int knobSize, StrikeRotaryKnob& knob)
{
    auto knobBounds = topLeftCell.withSize(knobSize, knobSize + knobBlockExtra);
    knob.slider.setBounds(knobBounds);
    knob.nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
}

void StrikeAudioProcessorEditor::positionToggle(juce::Rectangle<int> cell, StrikeToggle& toggle)
{
    const auto font = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, toggle.button.getButtonText().toUpperCase());
    const auto width = (int) std::ceil(9.0f + 8.0f + textWidth + 24.0f);
    toggle.button.setBounds(cell.withSizeKeepingCentre(width, buttonRowHeight)
                                 .expanded((int) StrikeLookAndFeel::buttonShadowMargin));
}

void StrikeAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(20, 0);

    const auto monoFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto monoTextWidth = juce::GlyphArrangement::getStringWidth(monoFont, "MONO");
    const auto monoWidth = (int) std::ceil(9.0f + 8.0f + monoTextWidth + 24.0f);
    monoToggle.button.setBounds(header.removeFromRight(monoWidth).withSizeKeepingCentre(monoWidth, buttonRowHeight)
                                     .expanded((int) StrikeLookAndFeel::buttonShadowMargin));

    const auto titleFont = lookAndFeel.getDisplayFont(20.8f).withExtraKerningFactor(0.04f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(8.8f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "STRIKE") + 8;
    const auto baselineY = (float) header.getY() + (float) header.getHeight() * 0.62f;

    auto titleBounds = header.removeFromLeft(titleWidth);
    titleBounds.setY((int) (baselineY - titleFont.getAscent()));
    titleBounds.setHeight((int) std::ceil(titleFont.getHeight()));
    titleLabel.setBounds(titleBounds);

    header.removeFromLeft(14);
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

    auto row1 = content.removeFromTop(row1Height);
    content.removeFromTop(rowGap);
    auto row2 = content.removeFromTop(row2Height);

    auto strumColumn = row1.removeFromLeft(strumWidth);
    row1.removeFromLeft(columnGap);
    auto colorColumn = row1.removeFromLeft(colorWidth);
    row1.removeFromLeft(columnGap);
    auto outputColumn = row1;

    auto ringModColumn = row2.removeFromLeft(ringModWidth);
    row2.removeFromLeft(columnGap);
    auto crossCoupleColumn = row2.removeFromLeft(crossCoupleWidth);
    row2.removeFromLeft(columnGap);
    auto filterColumn = row2;

    strumSectionBounds = strumColumn.toFloat();
    colorSectionBounds = colorColumn.toFloat();
    outputSectionBounds = outputColumn.toFloat();
    ringModSectionBounds = ringModColumn.toFloat();
    crossCoupleSectionBounds = crossCoupleColumn.toFloat();
    filterSectionBounds = filterColumn.toFloat();

    // ---- STRUM: Decay/Brightness, then Pluck-Bow/Bow Force, then Noise Color combo. A hidden
    // spacer matches Color's toggle row height so both sections' knob rows line up. ----
    {
        auto inner = strumColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromTop(buttonRowHeight);
        inner.removeFromTop(smallGap);

        auto row1Cell = inner.removeFromTop(defaultKnobSize + knobBlockExtra);
        auto row1Block = row1Cell.withSizeKeepingCentre(defaultKnobSize * 2 + knobGap, defaultKnobSize + knobBlockExtra);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, decayKnob);
        row1Block.removeFromLeft(knobGap);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, brightnessKnob);

        inner.removeFromTop(smallGap);

        auto row2Cell = inner.removeFromTop(defaultKnobSize + knobBlockExtra);
        auto row2Block = row2Cell.withSizeKeepingCentre(defaultKnobSize * 2 + knobGap, defaultKnobSize + knobBlockExtra);
        positionKnob(row2Block.removeFromLeft(defaultKnobSize), defaultKnobSize, bowAmountKnob);
        row2Block.removeFromLeft(knobGap);
        positionKnob(row2Block.removeFromLeft(defaultKnobSize), defaultKnobSize, bowForceKnob);

        inner.removeFromTop(comboToKnobGap);
        noiseColorCombo.combo.setBounds(inner.removeFromTop(comboRowHeight)
                                             .withSizeKeepingCentre(noiseColorComboWidth, comboRowHeight));
    }

    // ---- COLOR: Post Filter toggle, Structure/Position, then Waveshape + its Fold/BitCrush
    // combo stacked directly beneath it. ----
    {
        auto inner = colorColumn;
        inner.removeFromTop(sectionPaddingTop);

        positionToggle(inner.removeFromTop(buttonRowHeight), postFilterToggle);
        inner.removeFromTop(smallGap);

        auto row1Cell = inner.removeFromTop(defaultKnobSize + knobBlockExtra);
        auto row1Block = row1Cell.withSizeKeepingCentre(defaultKnobSize * 2 + knobGap, defaultKnobSize + knobBlockExtra);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, structureKnob);
        row1Block.removeFromLeft(knobGap);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, stringPositionKnob);

        inner.removeFromTop(smallGap);

        auto waveshapeCell = inner.removeFromTop(defaultKnobSize + knobBlockExtra)
                                  .withSizeKeepingCentre(defaultKnobSize, defaultKnobSize + knobBlockExtra);
        positionKnob(waveshapeCell, defaultKnobSize, waveshapeKnob);

        inner.removeFromTop(comboToKnobGap);
        waveshaperTypeCombo.combo.setBounds(inner.removeFromTop(comboRowHeight)
                                                 .withSizeKeepingCentre(waveshaperComboWidth, comboRowHeight));
    }

    // ---- OUTPUT: single hero Volume knob, centred in the column. ----
    {
        auto inner = outputColumn;
        inner.removeFromTop(sectionPaddingTop);
        positionKnob(inner.withSizeKeepingCentre(heroKnobSize, heroKnobSize + knobBlockExtra), heroKnobSize, volumeKnob);
    }

    // ---- RING MOD: Amount above Frequency, each its own row. Hidden spacer matches Cross
    // Couple/Filter's toggle row height. ----
    {
        auto inner = ringModColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromTop(buttonRowHeight);
        inner.removeFromTop(smallGap);

        positionKnob(inner.removeFromTop(defaultKnobSize + knobBlockExtra).withSizeKeepingCentre(defaultKnobSize, defaultKnobSize + knobBlockExtra),
                     defaultKnobSize, ringModAmountKnob);
        inner.removeFromTop(smallGap);
        positionKnob(inner.removeFromTop(defaultKnobSize + knobBlockExtra).withSizeKeepingCentre(defaultKnobSize, defaultKnobSize + knobBlockExtra),
                     defaultKnobSize, ringModFrequencyKnob);
    }

    // ---- CROSS COUPLE: On toggle, Amount/Delay, then Detune. ----
    {
        auto inner = crossCoupleColumn;
        inner.removeFromTop(sectionPaddingTop);

        positionToggle(inner.removeFromTop(buttonRowHeight), crossCoupleOnToggle);
        inner.removeFromTop(smallGap);

        auto row1Cell = inner.removeFromTop(defaultKnobSize + knobBlockExtra);
        auto row1Block = row1Cell.withSizeKeepingCentre(defaultKnobSize * 2 + knobGap, defaultKnobSize + knobBlockExtra);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, crossCoupleAmountKnob);
        row1Block.removeFromLeft(knobGap);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, coupleDelayKnob);

        inner.removeFromTop(smallGap);
        positionKnob(inner.removeFromTop(defaultKnobSize + knobBlockExtra).withSizeKeepingCentre(defaultKnobSize, defaultKnobSize + knobBlockExtra),
                     defaultKnobSize, detuneKnob);
    }

    // ---- FILTER: On toggle, Cutoff/Resonance, then Attack/Envelope/Decay (three across). ----
    {
        auto inner = filterColumn;
        inner.removeFromTop(sectionPaddingTop);

        positionToggle(inner.removeFromTop(buttonRowHeight), filterOnToggle);
        inner.removeFromTop(smallGap);

        auto row1Cell = inner.removeFromTop(defaultKnobSize + knobBlockExtra);
        auto row1Block = row1Cell.withSizeKeepingCentre(defaultKnobSize * 2 + knobGap, defaultKnobSize + knobBlockExtra);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, filterCutoffKnob);
        row1Block.removeFromLeft(knobGap);
        positionKnob(row1Block.removeFromLeft(defaultKnobSize), defaultKnobSize, resonanceKnob);

        inner.removeFromTop(smallGap);
        auto row2Cell = inner.removeFromTop(defaultKnobSize + knobBlockExtra);
        auto row2Block = row2Cell.withSizeKeepingCentre(defaultKnobSize * 3 + filterKnobGap * 2, defaultKnobSize + knobBlockExtra);
        positionKnob(row2Block.removeFromLeft(defaultKnobSize), defaultKnobSize, filterAttackKnob);
        row2Block.removeFromLeft(filterKnobGap);
        positionKnob(row2Block.removeFromLeft(defaultKnobSize), defaultKnobSize, filterEnvAmountKnob);
        row2Block.removeFromLeft(filterKnobGap);
        positionKnob(row2Block.removeFromLeft(defaultKnobSize), defaultKnobSize, filterDecayKnob);
    }

    rebuildChassisTexture();
}
