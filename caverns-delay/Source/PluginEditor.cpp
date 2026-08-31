// ============================================================================
// This file is the canonical reference implementation for the juce-hardware-
// panel-ui skill (~/.claude/skills/juce-hardware-panel-ui/SKILL.md).
//
// COPY-VERBATIM when adapting for a new plugin: rebuildChassisTexture(),
// drawHardwareSection(), and the chassis/panel/header-bar/footer-bar chrome
// inside paint() (everything before the four drawHardwareSection(...) calls,
// plus the seam line right after them). These don't reference any per-plugin
// content and should not be re-derived from a screenshot or from memory.
//
// PLUGIN-SPECIFIC by nature, not marked line-by-line: the constructor (which
// controls exist, their labels, the brand wordmark/tag line text and window
// size), resized() (column widths and which knob goes where -- this is
// exactly the part that has to differ per plugin's parameter set), and the
// section name strings passed to drawHardwareSection(). The footer's
// version string already reads live from JucePlugin_VersionString and needs
// no change; only the "Wild Jag"/company-name text needs updating per plugin.
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"

#include "../../common/Presets/FactoryPreset.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - CavernsTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* CavernsAudioProcessor::createEditor()
{
    return new CavernsAudioProcessorEditor(*this);
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

    constexpr int timingColumnWidth = 372;
    constexpr int characterColumnWidth = 250;
    constexpr int modulationColumnWidth = 232;   // matches the approved mockup exactly

    constexpr int sectionPaddingTop = 40;   // clearance for the badge straddling the top border
    constexpr int sectionPaddingSide = 12;
    constexpr int sectionPaddingBottom = 12;

    constexpr int defaultKnobSize = 88;
    constexpr int timeKnobSize = 112;       // L/R Time read larger than the rest, per design review
    constexpr int knobNameHeight = 28;      // gap between the knob and its value textbox, occupied
                                             // by the name label -- mockup DOM order is knob, name,
                                             // value (not name-above-knob like a typical plugin)
    constexpr int fieldNameHeight = 20;     // headroom for the fader name label, which IS above
                                             // (Dry/Wet match the more usual above-fader convention)
    constexpr int knobTextBoxHeight = 20;

    constexpr int buttonRowHeight = 28;
    constexpr int syncLinkButtonWidth = 96;
    constexpr int syncLinkGap = 10;
    constexpr int divisionComboWidth = 112;
    constexpr int comboRowHeight = 28;   // matches .combo{height:28px} -- same as Preset/pushbtn
    constexpr int fieldLabelHeight = 16;
}

void CavernsAudioProcessorEditor::commitRawTimeParam(const juce::String& paramID, float ms)
{
    // Writes straight to the parameter rather than through its Slider, since Slider::setValue()
    // silently drops the notification whenever the slider's displayed value already matches -
    // which it always does here, because the timer callback kept it visually in sync throughout.
    if (auto* param = processorRef.apvts.getParameter(paramID))
        param->setValueNotifyingHost(param->convertTo0to1(ms));
}

void CavernsAudioProcessorEditor::setupRotarySlider(juce::Slider& slider, juce::Label& label,
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
    // in resized() instead, in the gap CavernsLookAndFeel::drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void CavernsAudioProcessorEditor::setupVerticalSlider(juce::Slider& slider, juce::Label& label,
                                                        const juce::String& labelText)
{
    slider.setLookAndFeel(&lookAndFeel);   // see setupRotarySlider() for why this is necessary
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, knobTextBoxHeight);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the fader track itself is only ~13px wide,
    // and a Label attached that way is forced to match its owner's width - "DRY"/"WET" would
    // get silently ellipsized to "...". Positioned manually in resized() instead, wide enough
    // to fit the text, matching the mockup's fader-cell (label centred over the whole cell,
    // not clipped to the thin track).
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

CavernsAudioProcessorEditor::CavernsAudioProcessorEditor(CavernsAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("CAVERNS", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff34d6bb));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Bucket Brigade Delay").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    // See common/Presets/FactoryPreset.h's setupPresetCombo() for why it's left unselected on
    // startup rather than showing the first preset's name, and why its text colour is set directly
    // on the instance rather than via the shared LookAndFeel.
    wildjag::setupPresetCombo(presetCombo, lookAndFeel, *this, processorRef);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, CavernsAudioProcessor::bypassParamID, bypassButton);

    syncButton.setLookAndFeel(&lookAndFeel);
    syncButton.setButtonText("SYNC");
    addAndMakeVisible(syncButton);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, CavernsAudioProcessor::syncParamID, syncButton);

    // While Sync (or Link) is on, the time knobs only *display* the tempo- or link-derived value -
    // the raw L/R Time parameters underneath sit wherever they were last left, since the timer
    // callback below updates the sliders with dontSendNotification. If Sync/Link then switches off
    // and we didn't write that displayed value back into the raw parameters first, playback would
    // silently snap to those stale numbers even though the knobs still show the value it was just
    // running at. Writing through the Slider (setValue(..., sendNotificationSync)) doesn't work for
    // this: JUCE skips the notification whenever the value isn't actually changing, which is exactly
    // the case here since the timer already drove the slider to this same number - so the commit
    // has to go straight to the parameter instead.
    syncButton.onClick = [this]
    {
        if (!syncButton.getToggleState())
        {
            commitRawTimeParam(CavernsAudioProcessor::leftTimeParamID, processorRef.getCurrentLeftDelayMs());
            commitRawTimeParam(CavernsAudioProcessor::rightTimeParamID, processorRef.getCurrentRightDelayMs());
        }
    };

    linkButton.setLookAndFeel(&lookAndFeel);
    linkButton.setButtonText("LINK");
    addAndMakeVisible(linkButton);
    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, CavernsAudioProcessor::linkParamID, linkButton);

    linkButton.onClick = [this]
    {
        // Only the raw Time parameter matters here, not the sync division - while synced, Link
        // works by picking which division the audio thread reads, and that's never stale.
        if (!linkButton.getToggleState() && !syncButton.getToggleState())
            commitRawTimeParam(CavernsAudioProcessor::rightTimeParamID, processorRef.getCurrentRightDelayMs());
    };

    leftDivisionLabel.setText("L DIVISION", juce::dontSendNotification);
    leftDivisionLabel.setJustificationType(juce::Justification::centred);
    leftDivisionLabel.setFont(lookAndFeel.getDisplayFont(9.5f).withExtraKerningFactor(0.12f));
    leftDivisionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7f938f));
    addAndMakeVisible(leftDivisionLabel);

    leftDivisionCombo.setLookAndFeel(&lookAndFeel);
    leftDivisionCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    leftDivisionCombo.addItemList(CavernsAudioProcessor::getSubdivisionChoices(), 1);
    addAndMakeVisible(leftDivisionCombo);
    leftDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, CavernsAudioProcessor::leftSubdivisionParamID, leftDivisionCombo);

    rightDivisionLabel.setText("R DIVISION", juce::dontSendNotification);
    rightDivisionLabel.setJustificationType(juce::Justification::centred);
    rightDivisionLabel.setFont(lookAndFeel.getDisplayFont(9.5f).withExtraKerningFactor(0.12f));
    rightDivisionLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7f938f));
    addAndMakeVisible(rightDivisionLabel);

    rightDivisionCombo.setLookAndFeel(&lookAndFeel);
    rightDivisionCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    rightDivisionCombo.addItemList(CavernsAudioProcessor::getSubdivisionChoices(), 1);
    addAndMakeVisible(rightDivisionCombo);
    rightDivisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, CavernsAudioProcessor::rightSubdivisionParamID, rightDivisionCombo);

    setupRotarySlider(leftTimeSlider, leftTimeLabel, "L Time");
    setupRotarySlider(rightTimeSlider, rightTimeLabel, "R Time");
    setupRotarySlider(feedbackSlider, feedbackLabel, "Feedback");
    setupRotarySlider(lowCutSlider, lowCutLabel, "Low Cut");
    setupRotarySlider(highCutSlider, highCutLabel, "High Cut");
    setupRotarySlider(degradeSlider, degradeLabel, "Degrade");
    setupRotarySlider(modSpeedSlider, modSpeedLabel, "Mod Speed");
    setupRotarySlider(modDepthSlider, modDepthLabel, "Mod Depth");
    setupVerticalSlider(drySlider, dryLabel, "Dry");
    setupVerticalSlider(wetSlider, wetLabel, "Wet");

    leftTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::leftTimeParamID, leftTimeSlider);
    rightTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::rightTimeParamID, rightTimeSlider);
    feedbackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::feedbackParamID, feedbackSlider);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::lowCutParamID, lowCutSlider);
    highCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::highCutParamID, highCutSlider);
    degradeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::degradeParamID, degradeSlider);
    modSpeedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::modSpeedParamID, modSpeedSlider);
    modDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::modDepthParamID, modDepthSlider);
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::dryParamID, drySlider);
    wetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, CavernsAudioProcessor::wetParamID, wetSlider);

    setSize(1100, 500);

    startTimerHz(30);
}

CavernsAudioProcessorEditor::~CavernsAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void CavernsAudioProcessorEditor::timerCallback()
{
    const auto syncOn = syncButton.getToggleState();
    const auto linkOn = linkButton.getToggleState();

    leftDivisionCombo.setEnabled(syncOn);
    leftDivisionCombo.setAlpha(syncOn ? 1.0f : 0.35f);

    leftTimeSlider.setEnabled(!syncOn);
    leftTimeSlider.setAlpha(syncOn ? 0.5f : 1.0f);
    if (syncOn)
        leftTimeSlider.setValue(processorRef.getCurrentLeftDelayMs(), juce::dontSendNotification);

    const auto rightControlsUnlocked = !linkOn;

    rightDivisionCombo.setEnabled(syncOn && rightControlsUnlocked);
    rightDivisionCombo.setAlpha((syncOn && rightControlsUnlocked) ? 1.0f : 0.35f);

    rightTimeSlider.setEnabled(!syncOn && rightControlsUnlocked);
    if (syncOn)
        rightTimeSlider.setValue(processorRef.getCurrentRightDelayMs(), juce::dontSendNotification);
    else if (linkOn)
        rightTimeSlider.setValue(leftTimeSlider.getValue(), juce::dontSendNotification);
    rightTimeSlider.setAlpha((syncOn || linkOn) ? 0.5f : 1.0f);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void CavernsAudioProcessorEditor::rebuildChassisTexture()
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
// here, so the accent pair only has to change in one place (CavernsLookAndFeel.cpp) when this
// is adapted for a new plugin.
void CavernsAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void CavernsAudioProcessorEditor::paint(juce::Graphics& g)
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
    drawHardwareSection(g, timingSectionBounds, "Timing");
    drawHardwareSection(g, characterSectionBounds, "Character");
    drawHardwareSection(g, modulationSectionBounds, "Modulation");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    // --- END PLUGIN-SPECIFIC ---

    // Mockup's .footer uses align-items:flex-start (top-aligned within the 30px row), not
    // centred -- centredLeft/Right here put this text noticeably lower than the mockup.
    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("CAVERNS \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    // Wild Jag wordmark -- plain styled text, matching the mockup's .footer .wm exactly, rather
    // than the separate logo graphic that has no equivalent in the mockup to match against.
    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void CavernsAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    // Content-based width (LED + gap + text + padding), matching the mockup's .pushbtn -- a
    // fixed 80px was noticeably narrower than "BYPASS" actually needs at this font/tracking.
    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) CavernsLookAndFeel::buttonShadowMargin));
    header.removeFromRight(14);
    presetCombo.setBounds(header.removeFromRight(128).withSizeKeepingCentre(128, 28));

    // Baseline-align "CAVERNS" and the tag line (mockup: .brand{align-items:baseline}) -- simply
    // giving both labels the same box and vertically centring each independently doesn't line up
    // their baselines, since the two fonts differ so much in size (27px vs 11px).
    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "CAVERNS") + 8;
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
    auto modulationColumn = content.removeFromLeft(modulationColumnWidth);
    content.removeFromLeft(columnGap);
    auto mixColumn = content;

    timingSectionBounds = timingColumn.toFloat();
    characterSectionBounds = characterColumn.toFloat();
    modulationSectionBounds = modulationColumn.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap +
    // built-in value textbox, and CavernsLookAndFeel::drawRotarySlider flush-tops the circle
    // within that, leaving the name-gap blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, int knobSize, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- Timing: sync/link buttons, then division combos, then the (larger) time knobs. ----
    auto timingInner = timingColumn;
    timingInner.removeFromTop(sectionPaddingTop);
    timingInner.removeFromLeft(sectionPaddingSide);
    timingInner.removeFromRight(sectionPaddingSide);
    timingInner.removeFromBottom(sectionPaddingBottom);

    timingInner.removeFromTop(10);
    auto syncRow = timingInner.removeFromTop(buttonRowHeight)
                       .withSizeKeepingCentre(syncLinkButtonWidth * 2 + syncLinkGap, buttonRowHeight);
    const auto buttonShadowMargin = (int) CavernsLookAndFeel::buttonShadowMargin;
    syncButton.setBounds(syncRow.removeFromLeft(syncLinkButtonWidth).expanded(buttonShadowMargin));
    syncRow.removeFromLeft(syncLinkGap);
    linkButton.setBounds(syncRow.removeFromLeft(syncLinkButtonWidth).expanded(buttonShadowMargin));

    timingInner.removeFromTop(14);
    timingInner.removeFromTop(fieldLabelHeight);
    auto divisionRow = timingInner.removeFromTop(comboRowHeight);
    const auto timingHalfWidth = timingInner.getWidth() / 2;
    auto leftDivField = divisionRow.removeFromLeft(timingHalfWidth);
    auto rightDivField = divisionRow;
    leftDivisionCombo.setBounds(leftDivField.withSizeKeepingCentre(divisionComboWidth, comboRowHeight));
    rightDivisionCombo.setBounds(rightDivField.withSizeKeepingCentre(divisionComboWidth, comboRowHeight));
    leftDivisionLabel.setBounds(timingColumn.getX() + sectionPaddingSide, leftDivisionCombo.getY() - fieldLabelHeight,
                                 timingHalfWidth, fieldLabelHeight);
    rightDivisionLabel.setBounds(leftDivisionLabel.getRight(), rightDivisionCombo.getY() - fieldLabelHeight,
                                  timingHalfWidth, fieldLabelHeight);

    timingInner.removeFromTop(14);
    auto timeRow = timingInner;
    const auto timeHalfWidth = timeRow.getWidth() / 2;
    positionKnob(timeRow.removeFromLeft(timeHalfWidth), timeKnobSize, leftTimeSlider, leftTimeLabel);
    positionKnob(timeRow, timeKnobSize, rightTimeSlider, rightTimeLabel);

    // ---- Character: two rows of two -- Feedback/Degrade, then Low Cut/High Cut. ----
    auto characterInner = characterColumn;
    characterInner.removeFromTop(sectionPaddingTop);
    characterInner.removeFromLeft(sectionPaddingSide);
    characterInner.removeFromRight(sectionPaddingSide);
    characterInner.removeFromBottom(sectionPaddingBottom);

    characterInner.removeFromTop(14);
    auto charRow1 = characterInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto charHalfWidth = charRow1.getWidth() / 2;
    positionKnob(charRow1.removeFromLeft(charHalfWidth), defaultKnobSize, feedbackSlider, feedbackLabel);
    positionKnob(charRow1, defaultKnobSize, degradeSlider, degradeLabel);

    characterInner.removeFromTop(8);
    auto charRow2 = characterInner;
    positionKnob(charRow2.removeFromLeft(charHalfWidth), defaultKnobSize, lowCutSlider, lowCutLabel);
    positionKnob(charRow2, defaultKnobSize, highCutSlider, highCutLabel);

    // ---- Modulation: LFO speed and depth for delay-time modulation. ----
    auto modInner = modulationColumn;
    modInner.removeFromTop(sectionPaddingTop);
    modInner.removeFromLeft(sectionPaddingSide);
    modInner.removeFromRight(sectionPaddingSide);
    modInner.removeFromBottom(sectionPaddingBottom);

    modInner.removeFromTop(14);
    // Constrained to exactly the knob's own height, not left as the full remaining column height -
    // positionKnob() centres within whatever rectangle it's given, so leaving this unconstrained
    // (spanning all the way to the section's bottom padding) was centring the knobs vertically in
    // that leftover space instead of sitting flush under the badge like every other section.
    auto modRow = modInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto modHalfWidth = modRow.getWidth() / 2;
    positionKnob(modRow.removeFromLeft(modHalfWidth), defaultKnobSize, modSpeedSlider, modSpeedLabel);
    positionKnob(modRow, defaultKnobSize, modDepthSlider, modDepthLabel);

    // ---- Mix: independent dry/wet vertical faders. Unlike the rotary knobs above, the mockup's
    // fader-cell DOM order is name, THEN track, then value -- name above is correct here. ----
    auto mixInner = mixColumn;
    mixInner.removeFromTop(sectionPaddingTop);
    mixInner.removeFromLeft(sectionPaddingSide);
    mixInner.removeFromRight(sectionPaddingSide);
    mixInner.removeFromBottom(sectionPaddingBottom);

    auto mixLabelRow = mixInner.removeFromTop(fieldNameHeight);
    const auto mixHalfWidth = mixInner.getWidth() / 2;
    dryLabel.setBounds(mixLabelRow.removeFromLeft(mixHalfWidth));
    wetLabel.setBounds(mixLabelRow);
    // Reduced only a little horizontally -- the visual track itself stays slim (drawLinearSlider
    // caps it at ~12px regardless of component width), but the built-in value textbox needs the
    // fuller width or "100%"/"35%"-style values get silently ellipsized to "...".
    drySlider.setBounds(mixInner.removeFromLeft(mixHalfWidth).reduced(1, 4));
    wetSlider.setBounds(mixInner.reduced(1, 4));

    rebuildChassisTexture();
}
