// ============================================================================
// Adapted from the canonical reference implementation for the juce-hardware-
// panel-ui skill (~/.claude/skills/juce-hardware-panel-ui/SKILL.md), originally
// CavernsAudioProcessorEditor.cpp.
//
// COPY-VERBATIM: rebuildChassisTexture(), drawHardwareSection(), and the
// chassis/panel/header-bar/footer-bar chrome inside paint() (everything before
// the two drawHardwareSection(...) calls, plus the seam line right after them).
//
// PLUGIN-SPECIFIC: the constructor (controls, labels, brand wordmark/tag line,
// window size), resized() (column widths and knob placement -- Corrosion has three
// sections: Tone (Drive, Bias, Character, Color), Rect (Half/Full, Mix -- Mix
// doubles as Rect's on/off, there's no separate enable button), and Output
// (Dry, Comp, Wet -- Comp is itself a 0-100% blend knob, not a separate enable
// button)), plus a Bypass pushbutton in the header, and the section name
// strings passed to drawHardwareSection().
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - CorrosionTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* CorrosionAudioProcessor::createEditor()
{
    return new CorrosionAudioProcessorEditor(*this);
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

    constexpr int toneColumnWidth = 460;   // fits four knobs: Drive, Bias, Character, Color
    constexpr int rectColumnWidth = 260;   // fits two knobs: Half/Full, Mix
    // Output has no width constant -- it's the last column, so it just takes what's left
    // (currently sized to fit three knobs: Dry, Comp, Wet).

    constexpr int sectionPaddingTop = 60;    // extra clearance below the badge, per design review
    constexpr int sectionPaddingSide = 12;
    constexpr int sectionPaddingBottom = 28; // extra clearance above the border, per design review

    constexpr int defaultKnobSize = 88;
    constexpr int knobNameHeight = 28;   // gap between the knob and its value textbox, occupied
                                          // by the name label -- DOM order is knob, name, value
    constexpr int knobTextBoxHeight = 20;
}

void CorrosionAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                   const juce::String& labelText)
{
    // Slider is a member variable, so it's default-constructed (and builds its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body -- at that point getLookAndFeel() still resolves to JUCE's global
    // default, not ours. Unlike ComboBox, Slider has no parentHierarchyChanged() override to
    // rebuild that textbox once actually parented, so it's stuck with default styling (wrong
    // colour, wrong size) unless explicitly told about the real LookAndFeel here.
    slider.setLookAndFeel(&lookAndFeel);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the mockup's DOM order inside .knob-cell is
    // knob, then name, then value -- the name sits BELOW the knob (between it and the value
    // textbox), not above like attachToComponent(..., false) would place it. Positioned manually
    // in resized() instead, in the gap CorrosionLookAndFeel::drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

CorrosionAudioProcessorEditor::CorrosionAudioProcessorEditor(CorrosionAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("CORROSION", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe6df5c));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Overdrive").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    // Left unselected on startup (rather than showing the first preset's name) so that picking it
    // is always a real selection change - JUCE's ComboBox doesn't fire onChange when you choose the
    // item that's already showing, which would otherwise make the first preset unreachable from
    // this menu once the plugin loads with its own default parameter values rather than the
    // preset's.
    presetCombo.setLookAndFeel(&lookAndFeel);
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    presetCombo.setTextWhenNothingSelected("Preset");
    for (int i = 0; i < processorRef.getNumPrograms(); ++i)
        presetCombo.addItem(processorRef.getProgramName(i), i + 1);
    addAndMakeVisible(presetCombo);
    presetCombo.onChange = [this]
    {
        processorRef.setCurrentProgram(presetCombo.getSelectedItemIndex());
    };

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::bypassParamID, bypassButton);

    setupRotarySlider(driveSlider, driveLabel, "Drive");
    setupRotarySlider(biasSlider, biasLabel, "Bias");
    setupRotarySlider(characterSlider, characterLabel, "Character");
    setupRotarySlider(toneSlider, toneLabel, "Color");
    setupRotarySlider(rectBlendSlider, rectBlendLabel, "Half/Full");
    // Labelled "Blend" on the knob (the rectMix parameter/ID stay as-is) -- "Rect Blend" as a
    // host-facing name is already taken by rectBlendSlider's Half/Full knob above, so renaming
    // the parameter itself would give two automation entries the same name.
    setupRotarySlider(rectMixSlider, rectMixLabel, "Blend");
    setupRotarySlider(drySlider, dryLabel, "Dry");
    setupRotarySlider(compSlider, compLabel, "Comp");
    setupRotarySlider(outputSlider, outputLabel, "Wet");

    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::driveParamID, driveSlider);
    biasAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::biasParamID, biasSlider);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::characterParamID, characterSlider);
    toneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::toneParamID, toneSlider);
    rectBlendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::rectBlendParamID, rectBlendSlider);
    rectMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::rectMixParamID, rectMixSlider);
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::dryParamID, drySlider);
    compAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::compParamID, compSlider);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CorrosionAudioProcessor::outputParamID, outputSlider);

    setSize(1166, 420);
}

CorrosionAudioProcessorEditor::~CorrosionAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void CorrosionAudioProcessorEditor::rebuildChassisTexture()
{
    const auto w = getWidth();
    const auto h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    chassisTexture = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics tg(chassisTexture);

    // Woven leatherette cross-hatch -- the mockup's actual grain is two overlapping sets of fine
    // diagonal pinstripes (CSS repeating-linear-gradient at +/-35deg, 3px period), not random
    // speckles. Drawn as full-canvas hairlines at a fixed spacing rather than per-pixel, so this
    // is a few hundred drawLine calls total, done once here and cached, not per paint().
    // Alpha here is pushed well past the mockup's literal CSS values (0.02-0.05) -- a browser's
    // subpixel-AA rendering of a repeating-linear-gradient at this period is visibly finer/higher
    // contrast than JUCE's plain line drawing at the same nominal alpha, so matching the CSS
    // numbers exactly produced a texture invisible at normal viewing scale (only visible zoomed
    // in 5x). Tuned by eye against an unzoomed screenshot instead of the CSS source of truth.
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

    // A couple of soft diagonal scuff highlights, as before.
    juce::ColourGradient scuff1(juce::Colours::transparentWhite, 0.0f, 0.0f,
                                 juce::Colours::transparentWhite, fw, fh, false);
    scuff1.addColour(0.35, juce::Colours::transparentWhite);
    scuff1.addColour(0.46, juce::Colours::white.withAlpha(0.035f));
    scuff1.addColour(0.60, juce::Colours::transparentWhite);
    tg.setGradientFill(scuff1);
    tg.fillRect(0, 0, w, h);

    // Soft radial highlight above the top-left corner, as if a light is angled down onto the
    // case -- matches the mockup's .device::before radial-gradient(... at 15% -20% ...).
    juce::ColourGradient hotspot(juce::Colours::white.withAlpha(0.07f), fw * 0.15f, fh * -0.2f,
                                  juce::Colours::transparentWhite, fw * 0.15f, fh * 0.7f, true);
    tg.setGradientFill(hotspot);
    tg.fillRect(0, 0, w, h);
}

// COPY-VERBATIM (see banner at top of file): unbroken border + centred badge that hugs its
// text, sitting inside the border rather than straddling it. Badge/ink colours are read from
// the LookAndFeel (getAccentColour()/getBadgeInkColour()) rather than held as separate literals
// here, so the accent pair only has to change in one place (CorrosionLookAndFeel.cpp) when this
// is adapted for a new plugin.
void CorrosionAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void CorrosionAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto deviceBounds = getLocalBounds().toFloat();
    juce::Path devicePath;
    devicePath.addRoundedRectangle(deviceBounds, 14.0f);

    // Outer drop shadow -- the device reads as a real object sitting on a surface, not a flat
    // rect, matching the mockup's box-shadow (0 30px 60px rgba(0,0,0,0.55)).
    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 24, {0, 10}).drawForPath(g, devicePath);

    // Outer chassis -- textured, near-black.
    g.saveState();
    g.reduceClipRegion(devicePath);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c1f20), 0.0f, 0.0f,
                                            juce::Colour(0xff0a0c0d), (float) getWidth(), (float) getHeight(), false));
    g.fillAll();
    if (chassisTexture.isValid())
        g.drawImageAt(chassisTexture, 0, 0);

    // A faux wrapped-edge bezel around the whole chassis. Top: a crisp bright hairline, as if
    // catching light (mockup: 0 2px 0 rgba(255,255,255,0.04) inset -- a hard line, no blur).
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawLine(deviceBounds.getX() + 14.0f, deviceBounds.getY() + 1.5f, deviceBounds.getRight() - 14.0f, deviceBounds.getY() + 1.5f, 1.5f);

    // Bottom: a soft *blurred* inward shadow, not a hairline -- this is what actually reads as
    // the case curving away into shadow at the edge (mockup: 0 -2px 10px rgba(0,0,0,0.5) inset).
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

        // Three-stop gradient across the panel face, as if lit from the upper-left -- a plain
        // two-stop corner-to-corner fade read as flat/dead by comparison.
        juce::ColourGradient panelGradient(juce::Colour(0xff262d2f), fullPanelBounds.getX(), fullPanelBounds.getY(),
                                            juce::Colour(0xff171c1d), fullPanelBounds.getRight(), fullPanelBounds.getBottom(), false);
        panelGradient.addColour(0.55, juce::Colour(0xff1d2325));
        g.setGradientFill(panelGradient);
        g.fillRect(fullPanelBounds);

        // A brighter light-source hotspot over the upper-left, on top of the base gradient --
        // the three flat gradient stops alone read as too even/dim; this is what actually sells
        // "light shining on it" rather than just a diagonal tint.
        juce::ColourGradient hotspot(juce::Colours::white.withAlpha(0.10f), fullPanelBounds.getX(), fullPanelBounds.getY(),
                                      juce::Colours::transparentWhite, fullPanelBounds.getCentreX(), fullPanelBounds.getBottom(), true);
        g.setGradientFill(hotspot);
        g.fillRect(fullPanelBounds);

        // Soft vignette -- the panel darkens toward its edges, the same "light falls off toward
        // the corners" cue the mockup's 40px inset shadow gives the panel. Lighter and tighter to
        // the very edge than before, so it no longer competes with the hotspot above.
        juce::ColourGradient vignette(juce::Colours::transparentBlack, fullPanelBounds.getCentreX(), fullPanelBounds.getCentreY(),
                                       juce::Colours::black.withAlpha(0.16f), fullPanelBounds.getX(), fullPanelBounds.getY(), true);
        vignette.addColour(0.82, juce::Colours::transparentBlack);
        g.setGradientFill(vignette);
        g.fillRect(fullPanelBounds);

        // Thin top highlight, as if the panel's top edge catches light.
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

    // Thin black seam right at the panel's own edge, separating it from the chassis around it
    // (mockup: .panel box-shadow includes "0 0 0 1px rgba(0,0,0,0.6)", a hard 1px ring, distinct
    // from the softer inset highlight/shadow lines already drawn above).
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(fullPanelBounds, 8.0f, 1.0f);

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to the new plugin's actual params. ---
    drawHardwareSection(g, toneSectionBounds, "Tone");
    drawHardwareSection(g, rectSectionBounds, "Rect");
    drawHardwareSection(g, outputSectionBounds, "Output");
    // --- END PLUGIN-SPECIFIC ---

    // Mockup's .footer uses align-items:flex-start (top-aligned within the 30px row), not
    // centred -- centredLeft/Right here put this text noticeably lower than the mockup.
    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("CORROSION \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    // Wild Jag wordmark -- plain styled text, matching the mockup's .footer .wm exactly.
    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void CorrosionAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    // Content-based width (LED + gap + text + padding), matching the mockup's .pushbtn -- a
    // fixed 80px was noticeably narrower than "BYPASS" actually needs at this font/tracking.
    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) CorrosionLookAndFeel::buttonShadowMargin));
    header.removeFromRight(14);
    presetCombo.setBounds(header.removeFromRight(128).withSizeKeepingCentre(128, 28));

    // Baseline-align "CORROSION" and the tag line (mockup: .brand{align-items:baseline}) --
    // simply giving both labels the same box and vertically centring each independently doesn't
    // line up their baselines, since the two fonts differ so much in size (27px vs 11px).
    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "CORROSION") + 8;
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

    auto toneColumn = content.removeFromLeft(toneColumnWidth);
    content.removeFromLeft(columnGap);
    auto rectColumn = content.removeFromLeft(rectColumnWidth);
    content.removeFromLeft(columnGap);
    auto outputColumn = content;

    toneSectionBounds = toneColumn.toFloat();
    rectSectionBounds = rectColumn.toFloat();
    outputSectionBounds = outputColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap +
    // built-in value textbox, and CorrosionLookAndFeel::drawRotarySlider flush-tops the circle
    // within that, leaving the name-gap blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, int knobSize, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- Tone: Drive, Bias, and Color, side by side. ----
    auto toneInner = toneColumn;
    toneInner.removeFromTop(sectionPaddingTop);
    toneInner.removeFromLeft(sectionPaddingSide);
    toneInner.removeFromRight(sectionPaddingSide);
    toneInner.removeFromBottom(sectionPaddingBottom);

    // Constrained to exactly the knob's own height, not left as the full remaining column
    // height -- positionKnob() centres within whatever rectangle it's given, so leaving this
    // unconstrained would centre the knobs vertically in the leftover space instead of sitting
    // flush under the badge.
    auto driveRow = toneInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto toneQuarterWidth = driveRow.getWidth() / 4;
    auto driveCell = driveRow.removeFromLeft(toneQuarterWidth);
    auto biasCell = driveRow.removeFromLeft(toneQuarterWidth);
    auto characterCell = driveRow.removeFromLeft(toneQuarterWidth);
    auto colorCell = driveRow;
    positionKnob(driveCell, defaultKnobSize, driveSlider, driveLabel);
    positionKnob(biasCell, defaultKnobSize, biasSlider, biasLabel);
    positionKnob(characterCell, defaultKnobSize, characterSlider, characterLabel);
    positionKnob(colorCell, defaultKnobSize, toneSlider, toneLabel);

    // ---- Rect: Half/Full and Mix, side by side. Mix doubles as Rect's on/off, so there's no
    // separate enable button here -- same top-aligned, no-button treatment as Output below. ----
    auto rectInner = rectColumn;
    rectInner.removeFromTop(sectionPaddingTop);
    rectInner.removeFromLeft(sectionPaddingSide);
    rectInner.removeFromRight(sectionPaddingSide);
    rectInner.removeFromBottom(sectionPaddingBottom);

    auto rectBlendRow = rectInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto rectHalfWidth = rectBlendRow.getWidth() / 2;
    positionKnob(rectBlendRow.removeFromLeft(rectHalfWidth), defaultKnobSize, rectBlendSlider, rectBlendLabel);
    positionKnob(rectBlendRow, defaultKnobSize, rectMixSlider, rectMixLabel);

    // ---- Output: Dry, Comp, and Wet, side by side. ----
    auto outputInner = outputColumn;
    outputInner.removeFromTop(sectionPaddingTop);
    outputInner.removeFromLeft(sectionPaddingSide);
    outputInner.removeFromRight(sectionPaddingSide);
    outputInner.removeFromBottom(sectionPaddingBottom);

    auto outputRow = outputInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto outputThirdWidth = outputRow.getWidth() / 3;
    auto dryCell = outputRow.removeFromLeft(outputThirdWidth);
    auto compCell = outputRow.removeFromLeft(outputThirdWidth);
    auto wetCell = outputRow;
    positionKnob(dryCell, defaultKnobSize, drySlider, dryLabel);
    positionKnob(compCell, defaultKnobSize, compSlider, compLabel);
    positionKnob(wetCell, defaultKnobSize, outputSlider, outputLabel);

    rebuildChassisTexture();
}
