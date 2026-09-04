// ============================================================================
// This file follows the canonical reference implementation for the juce-
// hardware-panel-ui skill (~/.claude/skills/juce-hardware-panel-ui/SKILL.md),
// adapted from CavernsLookAndFeel's PluginEditor.cpp.
//
// COPY-VERBATIM: rebuildChassisTexture(), drawHardwareSection(), and the
// chassis/panel/header-bar/footer-bar chrome inside paint() (everything
// before the three drawHardwareSection(...) calls, plus the seam line right
// after them). These don't reference any per-plugin content.
//
// PLUGIN-SPECIFIC by nature, not marked line-by-line: the constructor (which
// controls exist, their labels, the brand wordmark/tag line text and window
// size), resized() (column widths and which knob goes where -- matched to
// Damage's own parameter set and the approved mockup), and the section name
// strings passed to drawHardwareSection(). The footer's version string
// already reads live from JucePlugin_VersionString; only "Wild Jag" and the
// footer bar's height (taller than Caverns', per design review) differ.
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

#include "../../common/Presets/FactoryPreset.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - DamageTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* DamageAudioProcessor::createEditor()
{
    return new DamageAudioProcessorEditor(*this);
}

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 62;
    constexpr int footerHeight = 30;   // more grey panel clearance below the footer text than the
                                        // textbox-hugging default would give, but not as tall as
                                        // an earlier draft that left too much dead space below it

    constexpr int contentPaddingTop = 22;
    constexpr int contentPaddingSide = 22;
    constexpr int contentPaddingBottom = 11;
    constexpr int columnGap = 14;

    constexpr int gainColumnWidth = 236;
    constexpr int characterColumnWidth = 248;
    constexpr int mixColumnWidth = 160;

    constexpr int sectionPaddingTop = 40;   // clearance for the badge straddling the top border
    constexpr int sectionPaddingSide = 12;
    constexpr int sectionPaddingBottom = 12;

    constexpr int knobSize = 88;          // every knob in Damage is this size -- no "hero" knob
    constexpr int knobNameHeight = 20;    // gap between the knob and its value textbox, occupied
                                           // by the name label -- knob, then name, then value
    constexpr int knobTextBoxHeight = 16;

    constexpr int buttonWidth = 78;       // fixed for every pushbutton (Bypass/Boost/Slow/On),
                                           // unlike Caverns' content-hugging Bypass width
    constexpr int buttonHeight = 28;
    constexpr int buttonRowGap = 24;
    constexpr int buttonRowTopMargin = 18;

    constexpr int gainKnobRowTopMargin = 26;
    constexpr int charStackTopMargin = 14;
    constexpr int charStackGap = 16;      // gap between the Hi Pass/Lo Pass row and the row below

    constexpr int faderCellWidth = 56;
    constexpr int faderCellGap = 10;
    constexpr int faderRowTopPadding = 20;
    constexpr int faderNameHeight = 22;
    constexpr int faderTextBoxHeight = 18;

    // Damage's parameters only declare their unit via AudioParameterFloatAttributes::withLabel(),
    // which (unlike a custom withStringFromValueFunction()) JUCE does NOT automatically fold into
    // the value text shown in a Slider's built-in textbox -- SliderParameterAttachment binds
    // Slider::textFromValueFunction to RangedAudioParameter::getText(), whose default
    // implementation is plain numeric text with no label. Caverns' parameters instead each supply
    // their own withStringFromValueFunction() directly in PluginProcessor.cpp; matching that
    // formatting here, at the editor/UI layer, gets the same suffixed value text (matching the
    // approved mockup, e.g. "-27.2 dB", "19.4x", "6.0 kHz") without touching the processor's
    // parameter definitions, which are out of scope for a UI restyle.
    juce::String formatHz(double v)
    {
        if (v >= 1000.0)
            return juce::String(v / 1000.0, 1) + " kHz";
        // Not juce::String(v, 0) -- JUCE's String(double, int) only applies fixed decimal-place
        // formatting when numberOfDecimalPlaces > 0; at exactly 0 it silently falls back to the
        // stream's default precision instead of rounding to an integer (see
        // NumberToStringConverters::writeDouble in juce_String.cpp), so e.g. 106.68 would print
        // as "106.68 Hz" instead of "107 Hz".
        return juce::String(juce::roundToInt(v)) + " Hz";
    }

    void setValueFormatter(juce::Slider& slider, std::function<juce::String(double)> fn)
    {
        slider.textFromValueFunction = std::move(fn);
        slider.updateText();
    }
}

void DamageEditorContent::setupRotarySlider(juce::Slider& slider, juce::Label& label,
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
    // in resized() instead, in the gap DamageLookAndFeel::drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void DamageEditorContent::setupVerticalSlider(juce::Slider& slider, juce::Label& label,
                                                     const juce::String& labelText)
{
    slider.setLookAndFeel(&lookAndFeel);   // see setupRotarySlider() for why this is necessary
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, faderCellWidth, faderTextBoxHeight);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the fader track itself is only ~12px wide, and
    // a Label attached that way is forced to match its owner's width - "FX VOLUME" would get
    // silently ellipsized. Positioned manually in resized() instead, wide enough to fit the text,
    // matching the mockup's fader-cell (label allowed to overflow its slim track, not clipped).
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

DamageEditorContent::DamageEditorContent(DamageAudioProcessor& p)
    : processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("DAMAGE", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(30.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff04c42));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("FM Mangled Fuzz").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    // See common/Presets/FactoryPreset.h's setupPresetCombo() for why it's left unselected on
    // startup rather than showing the first preset's name.
    wildjag::setupPresetCombo(presetCombo, lookAndFeel, *this, processorRef);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("Bypass");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, DamageAudioProcessor::bypassParamID, bypassButton);

    // ---- Gain: Drive (+ Boost), Gate (+ Slow) ----
    setupRotarySlider(driveSlider, driveLabel, "Drive");
    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::driveParamID, driveSlider);
    setValueFormatter(driveSlider, [](double v) { return juce::String(v, 1) + "x"; });

    boostButton.setLookAndFeel(&lookAndFeel);
    boostButton.setButtonText("Boost");
    addAndMakeVisible(boostButton);
    boostAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, DamageAudioProcessor::squareParamID, boostButton);

    setupRotarySlider(gateSlider, gateLabel, "Gate");
    gateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::gateParamID, gateSlider);
    setValueFormatter(gateSlider, [](double v) { return juce::String(v, 1) + " dB"; });

    slowButton.setLookAndFeel(&lookAndFeel);
    slowButton.setButtonText("Slow");
    addAndMakeVisible(slowButton);
    slowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, DamageAudioProcessor::slowReleaseParamID, slowButton);

    // ---- Character: Hi Pass + Lo Pass on row 1, Pulse Width + FM Freq on row 2 (+ On) ----
    setupRotarySlider(hiPassSlider, hiPassLabel, "Hi Pass");
    hiPassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::highPassParamID, hiPassSlider);
    setValueFormatter(hiPassSlider, [](double v) { return formatHz(v); });

    setupRotarySlider(loPassSlider, loPassLabel, "Lo Pass");
    loPassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::lowPassParamID, loPassSlider);
    setValueFormatter(loPassSlider, [](double v) { return formatHz(v); });

    setupRotarySlider(widthSlider, widthLabel, "Pulse Width");
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::widthParamID, widthSlider);
    setValueFormatter(widthSlider, [](double v) { return juce::String(v, 1) + "%"; });

    setupRotarySlider(oscFreqSlider, oscFreqLabel, "FM Freq");
    oscFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::oscFreqParamID, oscFreqSlider);
    setValueFormatter(oscFreqSlider, [](double v) { return formatHz(v); });

    onButton.setLookAndFeel(&lookAndFeel);
    onButton.setButtonText("On");
    addAndMakeVisible(onButton);
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, DamageAudioProcessor::oscillateParamID, onButton);

    // ---- Mix: Dry, Wet (vertical faders) ----
    setupVerticalSlider(drySlider, dryLabel, "Dry");
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::dryParamID, drySlider);
    setValueFormatter(drySlider, [](double v) { return juce::String(v, 1) + "%"; });

    setupVerticalSlider(wetSlider, wetLabel, "Wet");
    wetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, DamageAudioProcessor::wetParamID, wetSlider);
    setValueFormatter(wetSlider, [](double v) { return juce::String(v, 1) + "%"; });

    // Width trimmed 12px from the mockup's 760 -- Mix's fixed column width left an oversized gap
    // of bare panel to its right, of which the header/footer's right-edge-anchored chrome
    // (Bypass, Wild Jag) forms one edge; narrowing the chassis brings both in to match rather
    // than repositioning them independently. Height is 30px taller than the mockup's 540, all of
    // it added below the Character section's On button so every section outline sits lower with
    // it -- footerHeight dropping from an earlier draft's 50 back down to 30 exactly offsets that,
    // so the net change versus the mockup is +30, not +50.
    setSize(748, 550);

    startTimerHz(30);
}

DamageEditorContent::~DamageEditorContent()
{
    setLookAndFeel(nullptr);
}

void DamageEditorContent::timerCallback()
{
    gateSlider.setLevelDb(processorRef.getGateLevelDb());
    gateSlider.repaint();

    const auto onState = onButton.getToggleState();
    oscFreqSlider.setEnabled(onState);
    oscFreqSlider.setAlpha(onState ? 1.0f : 0.35f);
    oscFreqLabel.setAlpha(onState ? 1.0f : 0.35f);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void DamageEditorContent::rebuildChassisTexture()
{
    const auto w = getWidth();
    const auto h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    chassisTexture = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics tg(chassisTexture);

    // Woven leatherette cross-hatch -- two overlapping sets of fine diagonal pinstripes, not
    // random speckles. Drawn as full-canvas hairlines at a fixed spacing rather than per-pixel,
    // so this is a few hundred drawLine calls total, done once here and cached, not per paint().
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

    // A couple of soft diagonal scuff highlights.
    juce::ColourGradient scuff1(juce::Colours::transparentWhite, 0.0f, 0.0f,
                                 juce::Colours::transparentWhite, fw, fh, false);
    scuff1.addColour(0.35, juce::Colours::transparentWhite);
    scuff1.addColour(0.46, juce::Colours::white.withAlpha(0.035f));
    scuff1.addColour(0.60, juce::Colours::transparentWhite);
    tg.setGradientFill(scuff1);
    tg.fillRect(0, 0, w, h);

    // Soft radial highlight above the top-left corner, as if a light is angled down onto the case.
    juce::ColourGradient hotspot(juce::Colours::white.withAlpha(0.07f), fw * 0.15f, fh * -0.2f,
                                  juce::Colours::transparentWhite, fw * 0.15f, fh * 0.7f, true);
    tg.setGradientFill(hotspot);
    tg.fillRect(0, 0, w, h);
}

// COPY-VERBATIM (see banner at top of file): unbroken border + centred badge that hugs its
// text, sitting inside the border rather than straddling it. Badge/ink colours are read from
// the LookAndFeel (getAccentColour()/getBadgeInkColour()) rather than held as separate literals
// here, so the accent pair only has to change in one place (DamageLookAndFeel.cpp) when this is
// adapted for a new plugin.
void DamageEditorContent::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

    // 5% less than fully opaque -- a deliberate, slightly softer badge label than Caverns' default.
    g.setColour(lookAndFeel.getBadgeInkColour().withAlpha(0.95f));
    g.setFont(font);
    g.drawText(label.toUpperCase(), badgeBounds, juce::Justification::centred);
}

void DamageEditorContent::paint(juce::Graphics& g)
{
    const auto deviceBounds = getLocalBounds().toFloat();
    juce::Path devicePath;
    devicePath.addRoundedRectangle(deviceBounds, 14.0f);

    // Outer drop shadow -- the device reads as a real object sitting on a surface, not a flat rect.
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
    // catching light.
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawLine(deviceBounds.getX() + 14.0f, deviceBounds.getY() + 1.5f, deviceBounds.getRight() - 14.0f, deviceBounds.getY() + 1.5f, 1.5f);

    // Bottom: a soft *blurred* inward shadow, not a hairline -- this is what reads as the case
    // curving away into shadow at the edge.
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

        // Three-stop gradient across the panel face, as if lit from the upper-left.
        juce::ColourGradient panelGradient(juce::Colour(0xff262d2f), fullPanelBounds.getX(), fullPanelBounds.getY(),
                                            juce::Colour(0xff171c1d), fullPanelBounds.getRight(), fullPanelBounds.getBottom(), false);
        panelGradient.addColour(0.55, juce::Colour(0xff1d2325));
        g.setGradientFill(panelGradient);
        g.fillRect(fullPanelBounds);

        // A brighter light-source hotspot over the upper-left, on top of the base gradient.
        juce::ColourGradient hotspot(juce::Colours::white.withAlpha(0.10f), fullPanelBounds.getX(), fullPanelBounds.getY(),
                                      juce::Colours::transparentWhite, fullPanelBounds.getCentreX(), fullPanelBounds.getBottom(), true);
        g.setGradientFill(hotspot);
        g.fillRect(fullPanelBounds);

        // Soft vignette -- the panel darkens toward its edges.
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

    // Thin black seam right at the panel's own edge, separating it from the chassis around it.
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(fullPanelBounds, 8.0f, 1.0f);

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to Damage's actual parameters. ---
    drawHardwareSection(g, gainSectionBounds, "Gain");
    drawHardwareSection(g, characterSectionBounds, "Character");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    // --- END PLUGIN-SPECIFIC ---

    // Mockup's footer uses flex-start (top-aligned within the bar), not centred -- centredLeft/
    // Right here would put this text noticeably lower than approved.
    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("DAMAGE \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void DamageEditorContent::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(contentPaddingSide, 0);

    const auto bypassShadowMargin = (int) DamageLookAndFeel::buttonShadowMargin;
    bypassButton.setBounds(header.removeFromRight(buttonWidth).withSizeKeepingCentre(buttonWidth, buttonHeight)
                                .expanded(bypassShadowMargin));
    header.removeFromRight(14);
    presetCombo.setBounds(header.removeFromRight(128).withSizeKeepingCentre(128, 28));

    // Baseline-align "DAMAGE" and the tag line -- simply giving both labels the same box and
    // vertically centring each independently doesn't line up their baselines, since the two
    // fonts differ so much in size (30px vs 11px).
    const auto titleFont = lookAndFeel.getDisplayFont(30.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "DAMAGE") + 8;
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

    auto gainColumn = content.removeFromLeft(gainColumnWidth);
    content.removeFromLeft(columnGap);
    auto characterColumn = content.removeFromLeft(characterColumnWidth);
    content.removeFromLeft(columnGap);
    // Fixed width, not the remainder of `content` -- the approved mockup's three columns don't
    // sum to the full content width, leaving a deliberate sliver of bare panel after Mix rather
    // than stretching it to fill.
    auto mixColumn = content.removeFromLeft(mixColumnWidth);

    gainSectionBounds = gainColumn.toFloat();
    characterSectionBounds = characterColumn.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap + built-
    // in value textbox, and DamageLookAndFeel::drawRotarySlider flush-tops the circle within that,
    // leaving the name-gap blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- Gain: Drive + Boost on the left, Gate + Slow on the right. ----
    auto gainInner = gainColumn;
    gainInner.removeFromTop(sectionPaddingTop);
    gainInner.removeFromLeft(sectionPaddingSide);
    gainInner.removeFromRight(sectionPaddingSide);
    gainInner.removeFromBottom(sectionPaddingBottom);

    gainInner.removeFromTop(gainKnobRowTopMargin);
    auto gainKnobRow = gainInner.removeFromTop(knobSize + knobNameHeight + knobTextBoxHeight);
    const auto gainHalfWidth = gainKnobRow.getWidth() / 2;
    positionKnob(gainKnobRow.removeFromLeft(gainHalfWidth), driveSlider, driveLabel);
    positionKnob(gainKnobRow, gateSlider, gateLabel);

    gainInner.removeFromTop(buttonRowTopMargin);
    auto gainButtonRow = gainInner.removeFromTop(buttonHeight)
                             .withSizeKeepingCentre(buttonWidth * 2 + buttonRowGap, buttonHeight);
    const auto buttonShadowMargin = (int) DamageLookAndFeel::buttonShadowMargin;
    boostButton.setBounds(gainButtonRow.removeFromLeft(buttonWidth).expanded(buttonShadowMargin));
    gainButtonRow.removeFromLeft(buttonRowGap);
    slowButton.setBounds(gainButtonRow.removeFromLeft(buttonWidth).expanded(buttonShadowMargin));

    // ---- Character: Hi Pass + Lo Pass on row 1, Pulse Width + FM Freq on row 2, On button below FM Freq. ----
    auto characterInner = characterColumn;
    characterInner.removeFromTop(sectionPaddingTop);
    characterInner.removeFromLeft(sectionPaddingSide);
    characterInner.removeFromRight(sectionPaddingSide);
    characterInner.removeFromBottom(sectionPaddingBottom);

    characterInner.removeFromTop(charStackTopMargin);
    auto charRow1 = characterInner.removeFromTop(knobSize + knobNameHeight + knobTextBoxHeight);
    const auto charHalfWidth = charRow1.getWidth() / 2;
    positionKnob(charRow1.removeFromLeft(charHalfWidth), hiPassSlider, hiPassLabel);
    positionKnob(charRow1, loPassSlider, loPassLabel);

    characterInner.removeFromTop(charStackGap);
    auto charRow2 = characterInner.removeFromTop(knobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(charRow2.removeFromLeft(charHalfWidth), widthSlider, widthLabel);
    positionKnob(charRow2, oscFreqSlider, oscFreqLabel);

    characterInner.removeFromTop(buttonRowTopMargin);
    auto charButtonRow = characterInner.removeFromTop(buttonHeight);
    charButtonRow.removeFromLeft(charHalfWidth);
    onButton.setBounds(charButtonRow.withSizeKeepingCentre(buttonWidth, buttonHeight).expanded(buttonShadowMargin));

    // ---- Mix: Dry + Wet, independent vertical faders (not a crossfade -- see processBlock()).
    // Unlike the rotary knobs above, the fader-cell DOM order is name, THEN track, then value --
    // name above is correct here. ----
    auto mixInner = mixColumn;
    mixInner.removeFromTop(sectionPaddingTop);
    mixInner.removeFromLeft(sectionPaddingSide);
    mixInner.removeFromRight(sectionPaddingSide);
    mixInner.removeFromBottom(sectionPaddingBottom);

    mixInner.removeFromTop(faderRowTopPadding);

    auto mixLabelRow = mixInner.removeFromTop(faderNameHeight);
    constexpr int faderPairWidth = faderCellWidth * 2 + faderCellGap;
    auto faderLabelPairArea = mixLabelRow.withSizeKeepingCentre(faderPairWidth, mixLabelRow.getHeight());
    auto faderSliderPairArea = mixInner.withSizeKeepingCentre(faderPairWidth, mixInner.getHeight());

    dryLabel.setBounds(faderLabelPairArea.removeFromLeft(faderCellWidth));
    faderLabelPairArea.removeFromLeft(faderCellGap);
    wetLabel.setBounds(faderLabelPairArea);

    // Reduced only a little vertically -- the visual track itself stays slim (drawLinearSlider
    // caps it at ~12px regardless of component width), but the built-in value textbox needs the
    // fuller width or "100.0%"-style values get silently ellipsized to "...".
    drySlider.setBounds(faderSliderPairArea.removeFromLeft(faderCellWidth).reduced(0, 2));
    faderSliderPairArea.removeFromLeft(faderCellGap);
    wetSlider.setBounds(faderSliderPairArea.reduced(0, 2));

    rebuildChassisTexture();
}

DamageAudioProcessorEditor::DamageAudioProcessorEditor(DamageAudioProcessor& p)
    : AudioProcessorEditor(&p), content(p), zoomHandler(*this, content, {748, 550})
{
    addAndMakeVisible(content);
}
