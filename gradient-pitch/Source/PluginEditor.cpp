// Adapted from Caverns' reference implementation of the juce-hardware-panel-ui skill.
// COPY-VERBATIM: rebuildChassisTexture(), drawHardwareSection(), and the chassis/panel/
// header-bar/footer-bar chrome inside paint(). PLUGIN-SPECIFIC: the constructor (which controls
// exist, labels, wordmark/tag text, window size) and resized() (Gradient's own layout, matching
// the approved mockup pixel-for-pixel per measurements taken from the rendered mockup HTML).

#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    constexpr int chassisMargin = 13;
    constexpr int headerHeight = 61;
    constexpr int footerHeight = 31;

    // Column widths and section heights matched exactly to the approved mockup's rendered layout
    // (measured via getBoundingClientRect() on the live HTML, not eyeballed) - row1's two sections
    // split evenly, but row2's three don't (PITCH's hero knobs force it wider than flexbox's equal
    // thirds would give, with DELAY/REGEN and DRIFT/OUTPUT taking the remainder).
    constexpr int modeColumnWidth = 402;
    constexpr int pitchColumnWidth = 288;
    constexpr int delayColumnWidth = 251;
    constexpr int row1Height = 272;
    constexpr int row2Height = 417;

    constexpr int contentPaddingTop = 18;
    constexpr int contentPaddingSide = 18;
    constexpr int contentPaddingBottom = 14;
    constexpr int rowGap = 14;
    constexpr int columnGap = 14;

    constexpr int sectionPaddingTop = 48;   // clearance below the badge (badge sits inside the
                                             // border with a visible gap, not straddling it)

    constexpr int defaultKnobSize = 88;
    constexpr int heroKnobSize = 112;        // A/B Pitch - the core identity control
    constexpr int compactKnobSize = 72;      // Drift/Mix/Out stack
    constexpr int knobBlockExtra = 32;       // combined height for the name label + value textbox,
                                              // below the knob itself - constant regardless of knob
                                              // diameter (measured from the mockup)
    constexpr int knobNameHeight = 16;
    constexpr int knobTextBoxHeight = 16;

    constexpr int buttonRowHeight = 31;
    constexpr int comboRowHeight = 29;
    constexpr int comboWidth = 142;
    constexpr int fieldLabelHeight = 15;

    constexpr int unitTagWidth = 26;
    constexpr int unitTagHeight = 16;

    constexpr int syncToggleWidth = 70;
    constexpr int subdivisionComboWidth = 92;
}

void GradientAudioProcessorEditor::setupRotaryKnob(GradientRotaryKnob& knob, const juce::String& labelText,
                                                     const juce::String& paramID)
{
    // Slider is a member variable, so it's default-constructed (and builds its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body - Slider has no parentHierarchyChanged() override to rebuild that
    // textbox once actually parented, so it needs to be told about the real LookAndFeel here.
    knob.slider.setLookAndFeel(&lookAndFeel);
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    knob.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                     juce::MathConstants<float>::pi * 2.8f, true);
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

void GradientAudioProcessorEditor::setupToggle(GradientToggle& toggle, const juce::String& labelText,
                                                 const juce::String& paramID)
{
    toggle.button.setLookAndFeel(&lookAndFeel);
    toggle.button.setButtonText(labelText);
    addAndMakeVisible(toggle.button);
    toggle.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, paramID, toggle.button);
}

void GradientAudioProcessorEditor::setupSpliceCombo(GradientCombo& combo, const juce::String& paramID)
{
    combo.combo.setLookAndFeel(&lookAndFeel);
    combo.combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    combo.combo.addItemList({"Normal", "Soft", "Smart"}, 1);
    addAndMakeVisible(combo.combo);

    combo.nameLabel.setText("Splice Mode", juce::dontSendNotification);
    combo.nameLabel.setJustificationType(juce::Justification::centred);
    combo.nameLabel.setFont(lookAndFeel.getDisplayFont(9.5f).withExtraKerningFactor(0.08f));
    combo.nameLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b9997));
    addAndMakeVisible(combo.nameLabel);

    combo.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, paramID, combo.combo);
}

void GradientAudioProcessorEditor::setupSubdivisionCombo(GradientCombo& combo, const juce::String& paramID)
{
    combo.combo.setLookAndFeel(&lookAndFeel);
    combo.combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    combo.combo.addItemList(GradientAudioProcessor::getSubdivisionChoices(), 1);
    addAndMakeVisible(combo.combo);

    combo.nameLabel.setText("Division", juce::dontSendNotification);
    combo.nameLabel.setJustificationType(juce::Justification::centred);
    combo.nameLabel.setFont(lookAndFeel.getDisplayFont(9.5f).withExtraKerningFactor(0.08f));
    combo.nameLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b9997));
    addAndMakeVisible(combo.nameLabel);

    combo.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, paramID, combo.combo);
}

GradientAudioProcessorEditor::GradientAudioProcessorEditor(GradientAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("GRADIENT", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(24.0f).withExtraKerningFactor(0.02f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffF0A177));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Pitch Shifting Delay").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(10.0f).withExtraKerningFactor(0.2f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7c8a88));
    addAndMakeVisible(tagLabel);

    presetCombo.setLookAndFeel(&lookAndFeel);
    presetCombo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    presetCombo.setTextWhenNothingSelected("Preset");
    addAndMakeVisible(presetCombo);

    setupToggle(bypassToggle, "BYPASS", GradientAudioProcessor::bypassParamID);

    // ---- MODE ----
    setupToggle(dualModeToggle, "DUAL MODE", GradientAudioProcessor::dualModeEnabledParamID);
    setupToggle(linkToggle, "LINK", GradientAudioProcessor::linkEnabledParamID);
    setupToggle(crossFeedbackToggle, "CROSS-FDBK", GradientAudioProcessor::crossFeedbackEnabledParamID);
    setupRotaryKnob(widthKnob, "Stereo", GradientAudioProcessor::widthPercentParamID);
    setupRotaryKnob(linkPitchKnob, "Link Pitch", GradientAudioProcessor::linkPitchIntervalSemitonesParamID);
    setupRotaryKnob(linkDelayKnob, "Link Delay", GradientAudioProcessor::linkDelayIntervalMsParamID);

    // ---- SPLICE ----
    setupSpliceCombo(spliceModeComboA, GradientAudioProcessor::spliceModeAParamID);
    setupSpliceCombo(spliceModeComboB, GradientAudioProcessor::spliceModeBParamID);
    setupRotaryKnob(xfadeKnobA, "Xfade", GradientAudioProcessor::crossfadeLengthMsAParamID);
    setupRotaryKnob(xfadeKnobB, "Xfade", GradientAudioProcessor::crossfadeLengthMsBParamID);

    auto setupUnitTag = [this](juce::Label& tag, const juce::String& text)
    {
        tag.setText(text, juce::dontSendNotification);
        tag.setJustificationType(juce::Justification::centred);
        tag.setFont(lookAndFeel.getDisplayFont(9.5f).withExtraKerningFactor(0.14f));
        tag.setColour(juce::Label::textColourId, juce::Colour(0xff6f7c7a));
        addAndMakeVisible(tag);
    };
    setupUnitTag(spliceUnitTagA, "A");
    setupUnitTag(spliceUnitTagB, "B");
    setupUnitTag(pitchUnitTagA, "A");
    setupUnitTag(pitchUnitTagB, "B");
    setupUnitTag(delayUnitTagA, "A");
    setupUnitTag(delayUnitTagB, "B");
    setupUnitTag(driftOutputUnitTagA, "A");
    setupUnitTag(driftOutputUnitTagB, "B");

    // ---- PITCH ----
    setupRotaryKnob(pitchKnobA, "Pitch", GradientAudioProcessor::pitchSemitonesAParamID);
    setupRotaryKnob(fineKnobA, "Fine", GradientAudioProcessor::pitchFineCentsAParamID);
    setupRotaryKnob(pitchKnobB, "Pitch", GradientAudioProcessor::pitchSemitonesBParamID);
    setupRotaryKnob(fineKnobB, "Fine", GradientAudioProcessor::pitchFineCentsBParamID);

    // ---- DELAY / REGEN ----
    setupToggle(delaySyncToggleA, "SYNC", GradientAudioProcessor::delaySyncEnabledAParamID);
    setupToggle(delaySyncToggleB, "SYNC", GradientAudioProcessor::delaySyncEnabledBParamID);
    setupSubdivisionCombo(delaySubdivisionComboA, GradientAudioProcessor::delaySubdivisionAParamID);
    setupSubdivisionCombo(delaySubdivisionComboB, GradientAudioProcessor::delaySubdivisionBParamID);
    setupRotaryKnob(delayKnobA, "Delay", GradientAudioProcessor::delayTimeMsAParamID);
    setupRotaryKnob(feedbackKnobA, "Regen", GradientAudioProcessor::feedbackPercentAParamID);
    setupRotaryKnob(delayKnobB, "Delay", GradientAudioProcessor::delayTimeMsBParamID);
    setupRotaryKnob(feedbackKnobB, "Regen", GradientAudioProcessor::feedbackPercentBParamID);

    // ---- DRIFT / OUTPUT ----
    setupRotaryKnob(driftKnobA, "Drift", GradientAudioProcessor::driftAmountAParamID);
    setupRotaryKnob(mixKnobA, "Mix", GradientAudioProcessor::mixPercentAParamID);
    setupRotaryKnob(outputKnobA, "Out", GradientAudioProcessor::outputTrimDbAParamID);
    setupRotaryKnob(driftKnobB, "Drift", GradientAudioProcessor::driftAmountBParamID);
    setupRotaryKnob(mixKnobB, "Mix", GradientAudioProcessor::mixPercentBParamID);
    setupRotaryKnob(outputKnobB, "Out", GradientAudioProcessor::outputTrimDbBParamID);

    setSize(880, 853);

    startTimerHz(30);
}

GradientAudioProcessorEditor::~GradientAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void GradientAudioProcessorEditor::timerCallback()
{
    // Dual Mode gates whether unit B's controls (and the global controls that only matter with a
    // second channel - Link, Cross-feedback, Width) do anything at all, matching Flux's Sync/Rate
    // dimming precedent. Link additionally overrides B's own Pitch/Delay while it's on, matching
    // the same precedent a second time for that specific pair.
    const bool dualOn = dualModeToggle.button.getToggleState();
    const bool linkOn = linkToggle.button.getToggleState();

    const auto setEnabledAndAlpha = [](juce::Component& c, bool enabled)
    {
        c.setEnabled(enabled);
        c.setAlpha(enabled ? 1.0f : 0.38f);
    };

    setEnabledAndAlpha(linkToggle.button, dualOn);
    setEnabledAndAlpha(crossFeedbackToggle.button, dualOn);
    setEnabledAndAlpha(widthKnob.slider, dualOn);
    setEnabledAndAlpha(widthKnob.nameLabel, dualOn);
    setEnabledAndAlpha(linkPitchKnob.slider, dualOn);
    setEnabledAndAlpha(linkPitchKnob.nameLabel, dualOn);
    setEnabledAndAlpha(linkDelayKnob.slider, dualOn);
    setEnabledAndAlpha(linkDelayKnob.nameLabel, dualOn);

    for (juce::Component* c : { (juce::Component*) &pitchKnobB.slider, (juce::Component*) &fineKnobB.slider,
                                 (juce::Component*) &delayKnobB.slider, (juce::Component*) &feedbackKnobB.slider,
                                 (juce::Component*) &spliceModeComboB.combo, (juce::Component*) &xfadeKnobB.slider,
                                 (juce::Component*) &driftKnobB.slider, (juce::Component*) &mixKnobB.slider,
                                 (juce::Component*) &outputKnobB.slider })
        setEnabledAndAlpha(*c, dualOn);
    for (juce::Component* l : { (juce::Component*) &pitchKnobB.nameLabel, (juce::Component*) &fineKnobB.nameLabel,
                                 (juce::Component*) &delayKnobB.nameLabel, (juce::Component*) &feedbackKnobB.nameLabel,
                                 (juce::Component*) &spliceModeComboB.nameLabel, (juce::Component*) &xfadeKnobB.nameLabel,
                                 (juce::Component*) &driftKnobB.nameLabel, (juce::Component*) &mixKnobB.nameLabel,
                                 (juce::Component*) &outputKnobB.nameLabel, (juce::Component*) &spliceUnitTagB,
                                 (juce::Component*) &pitchUnitTagB, (juce::Component*) &delayUnitTagB,
                                 (juce::Component*) &driftOutputUnitTagB })
        setEnabledAndAlpha(*l, dualOn);

    // Link overrides B's own Pitch/Delay specifically - dim those two even independent of the
    // broader Dual-Mode dimming above (they're already dimmed when Dual Mode is off; this adds
    // the same treatment whenever Link is on, regardless of Dual Mode, since Link's override in
    // the processor takes effect any time it's enabled).
    const bool bPitchDelayLive = dualOn && !linkOn;
    setEnabledAndAlpha(pitchKnobB.slider, bPitchDelayLive);
    setEnabledAndAlpha(pitchKnobB.nameLabel, bPitchDelayLive);
    setEnabledAndAlpha(fineKnobB.slider, bPitchDelayLive);
    setEnabledAndAlpha(fineKnobB.nameLabel, bPitchDelayLive);
    setEnabledAndAlpha(delayKnobB.slider, bPitchDelayLive);
    setEnabledAndAlpha(delayKnobB.nameLabel, bPitchDelayLive);

    setEnabledAndAlpha(linkPitchKnob.slider, dualOn && linkOn);
    setEnabledAndAlpha(linkPitchKnob.nameLabel, dualOn && linkOn);
    setEnabledAndAlpha(linkDelayKnob.slider, dualOn && linkOn);
    setEnabledAndAlpha(linkDelayKnob.nameLabel, dualOn && linkOn);

    // Tempo Sync (per unit): while a unit's Sync toggle is on, its Subdivision combo is the live
    // control and the raw Delay knob just visually tracks the tempo-derived value (matching
    // Caverns' Sync/Division precedent exactly - see getCurrentDelayMsA()/B()'s header comment).
    // B's sync controls are additionally dimmed whenever Link is overriding B's delay, same as
    // B's own raw Delay knob already is above.
    const bool syncOnA = delaySyncToggleA.button.getToggleState();
    setEnabledAndAlpha(delaySubdivisionComboA.combo, syncOnA);
    setEnabledAndAlpha(delaySubdivisionComboA.nameLabel, syncOnA);
    setEnabledAndAlpha(delayKnobA.slider, !syncOnA);
    if (syncOnA)
        delayKnobA.slider.setValue(processorRef.getCurrentDelayMsA(), juce::dontSendNotification);

    const bool syncOnB = delaySyncToggleB.button.getToggleState() && bPitchDelayLive;
    setEnabledAndAlpha(delaySyncToggleB.button, bPitchDelayLive);
    setEnabledAndAlpha(delaySubdivisionComboB.combo, syncOnB);
    setEnabledAndAlpha(delaySubdivisionComboB.nameLabel, syncOnB);
    setEnabledAndAlpha(delayKnobB.slider, bPitchDelayLive && !syncOnB);
    if (syncOnB)
        delayKnobB.slider.setValue(processorRef.getCurrentDelayMsB(), juce::dontSendNotification);
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void GradientAudioProcessorEditor::rebuildChassisTexture()
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
void GradientAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
                                                          const juce::String& label)
{
    g.setColour(juce::Colour(0xffe6ece6).withAlpha(0.62f));
    g.drawRoundedRectangle(bounds, 7.0f, 3.0f);

    const auto font = lookAndFeel.getDisplayFont(11.5f).withExtraKerningFactor(0.09f);
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, label.toUpperCase());
    constexpr float badgeHeight = 22.0f;
    const auto badgeBounds = juce::Rectangle<float>(textWidth + 32.0f, badgeHeight)
                                  .withCentre({bounds.getCentreX(), bounds.getY() + 12.0f + badgeHeight * 0.5f});

    g.setColour(lookAndFeel.getAccentColour());
    g.fillRoundedRectangle(badgeBounds, 4.0f);

    g.setColour(lookAndFeel.getBadgeInkColour());
    g.setFont(font);
    g.drawText(label.toUpperCase(), badgeBounds, juce::Justification::centred);
}

void GradientAudioProcessorEditor::paint(juce::Graphics& g)
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

    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(fullPanelBounds, 9.0f, 1.0f);

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to Gradient's actual params. ---
    drawHardwareSection(g, modeSectionBounds, "Mode");
    drawHardwareSection(g, spliceSectionBounds, "Splice");
    drawHardwareSection(g, pitchSectionBounds, "Pitch");
    drawHardwareSection(g, delaySectionBounds, "Delay / Regen");
    drawHardwareSection(g, driftOutputSectionBounds, "Drift / Output");
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("GRADIENT \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void GradientAudioProcessorEditor::positionKnob(juce::Rectangle<int> topLeftCell, int knobSize, GradientRotaryKnob& knob)
{
    auto knobBounds = topLeftCell.withSize(knobSize, knobSize + knobBlockExtra);
    knob.slider.setBounds(knobBounds);
    knob.nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
}

void GradientAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(20, 0);

    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassToggle.button.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, buttonRowHeight)
                                       .expanded((int) GradientLookAndFeel::buttonShadowMargin));
    header.removeFromRight(14);
    presetCombo.setBounds(header.removeFromRight(84).withSizeKeepingCentre(84, comboRowHeight));

    const auto titleFont = lookAndFeel.getDisplayFont(24.0f).withExtraKerningFactor(0.02f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(10.0f).withExtraKerningFactor(0.2f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "GRADIENT") + 8;
    const auto baselineY = (float) header.getY() + (float) header.getHeight() * 0.62f;

    auto titleBounds = header.removeFromLeft(titleWidth);
    titleBounds.setY((int) (baselineY - titleFont.getAscent()));
    titleBounds.setHeight((int) std::ceil(titleFont.getHeight()));
    titleLabel.setBounds(titleBounds);

    header.removeFromLeft(11);
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

    auto modeColumn = row1.removeFromLeft(modeColumnWidth);
    row1.removeFromLeft(columnGap);
    auto spliceColumn = row1;

    auto pitchColumn = row2.removeFromLeft(pitchColumnWidth);
    row2.removeFromLeft(columnGap);
    auto delayColumn = row2.removeFromLeft(delayColumnWidth);
    row2.removeFromLeft(columnGap);
    auto driftOutputColumn = row2;

    modeSectionBounds = modeColumn.toFloat();
    spliceSectionBounds = spliceColumn.toFloat();
    pitchSectionBounds = pitchColumn.toFloat();
    delaySectionBounds = delayColumn.toFloat();
    driftOutputSectionBounds = driftOutputColumn.toFloat();

    // ---- MODE: toggle row above knob row, the whole block vertically centred in the section
    // (the one section that isn't top-anchored under its badge - a deliberate design choice
    // confirmed in the mockup). ----
    {
        constexpr int toggleGap = 10, knobGap = 20, rowToRowGap = 18;
        constexpr int dualW = 112, linkW = 72, crossW = 116;
        const auto toggleRowWidth = dualW + toggleGap + linkW + toggleGap + crossW;
        const auto knobRowWidth = defaultKnobSize * 3 + knobGap * 2;
        const auto blockHeight = buttonRowHeight + rowToRowGap + defaultKnobSize + knobBlockExtra;

        auto block = modeColumn.withSizeKeepingCentre(juce::jmax(toggleRowWidth, knobRowWidth), blockHeight);
        const auto shadowMargin = (int) GradientLookAndFeel::buttonShadowMargin;

        auto toggleRow = block.removeFromTop(buttonRowHeight).withSizeKeepingCentre(toggleRowWidth, buttonRowHeight);
        dualModeToggle.button.setBounds(toggleRow.removeFromLeft(dualW).expanded(shadowMargin));
        toggleRow.removeFromLeft(toggleGap);
        linkToggle.button.setBounds(toggleRow.removeFromLeft(linkW).expanded(shadowMargin));
        toggleRow.removeFromLeft(toggleGap);
        crossFeedbackToggle.button.setBounds(toggleRow.removeFromLeft(crossW).expanded(shadowMargin));

        block.removeFromTop(rowToRowGap);
        auto knobRow = block.withSizeKeepingCentre(knobRowWidth, defaultKnobSize + knobBlockExtra);
        positionKnob(knobRow.removeFromLeft(defaultKnobSize), defaultKnobSize, widthKnob);
        knobRow.removeFromLeft(knobGap);
        positionKnob(knobRow.removeFromLeft(defaultKnobSize), defaultKnobSize, linkPitchKnob);
        knobRow.removeFromLeft(knobGap);
        positionKnob(knobRow.removeFromLeft(defaultKnobSize), defaultKnobSize, linkDelayKnob);
    }

    // ---- SPLICE: unit tags, then combo boxes, then crossfade knobs, per A/B column. ----
    {
        auto spliceInner = spliceColumn;
        spliceInner.removeFromTop(sectionPaddingTop);
        spliceInner.removeFromLeft(18);
        spliceInner.removeFromRight(18);

        const auto halfWidth = spliceInner.getWidth() / 2;
        auto colA = spliceInner.removeFromLeft(halfWidth);
        auto colB = spliceInner;

        auto layoutSpliceUnit = [](juce::Rectangle<int> col, juce::Label& tag, GradientCombo& combo, GradientRotaryKnob& xfade)
        {
            auto tagArea = col.removeFromTop(unitTagHeight).withSizeKeepingCentre(unitTagWidth, unitTagHeight);
            tag.setBounds(tagArea);
            col.removeFromTop(8);

            auto comboCell = col.removeFromTop(fieldLabelHeight + comboRowHeight).withSizeKeepingCentre(comboWidth, fieldLabelHeight + comboRowHeight);
            combo.nameLabel.setBounds(comboCell.removeFromTop(fieldLabelHeight));
            combo.combo.setBounds(comboCell);

            col.removeFromTop(16);
            auto knobArea = col.withSizeKeepingCentre(defaultKnobSize, defaultKnobSize + knobBlockExtra);
            xfade.slider.setBounds(knobArea);
            xfade.nameLabel.setBounds(knobArea.getX(), knobArea.getY() + defaultKnobSize, defaultKnobSize, knobNameHeight);
        };

        layoutSpliceUnit(colA, spliceUnitTagA, spliceModeComboA, xfadeKnobA);
        layoutSpliceUnit(colB, spliceUnitTagB, spliceModeComboB, xfadeKnobB);
    }

    // ---- PITCH: hero Pitch knob above the smaller Fine knob, per A/B column. ----
    {
        auto pitchInner = pitchColumn;
        pitchInner.removeFromTop(sectionPaddingTop);
        pitchInner.removeFromLeft(21);
        pitchInner.removeFromRight(21);

        const auto halfWidth = pitchInner.getWidth() / 2;
        auto colA = pitchInner.removeFromLeft(halfWidth);
        auto colB = pitchInner;

        auto layoutPitchUnit = [this](juce::Rectangle<int> col, juce::Label& tag, GradientRotaryKnob& pitchKnob, GradientRotaryKnob& fineKnob)
        {
            auto tagArea = col.removeFromTop(unitTagHeight).withSizeKeepingCentre(unitTagWidth, unitTagHeight);
            tag.setBounds(tagArea);
            col.removeFromTop(12);

            auto heroCell = col.removeFromTop(heroKnobSize + knobBlockExtra).withSizeKeepingCentre(heroKnobSize, heroKnobSize + knobBlockExtra);
            positionKnob(heroCell.withHeight(heroKnobSize), heroKnobSize, pitchKnob);

            col.removeFromTop(24);
            auto fineCell = col.withSizeKeepingCentre(defaultKnobSize, defaultKnobSize + knobBlockExtra);
            positionKnob(fineCell.withHeight(defaultKnobSize), defaultKnobSize, fineKnob);
        };

        layoutPitchUnit(colA, pitchUnitTagA, pitchKnobA, fineKnobA);
        layoutPitchUnit(colB, pitchUnitTagB, pitchKnobB, fineKnobB);
    }

    // ---- DELAY / REGEN: unit tag, Sync toggle, Subdivision combo (dims/lives per Sync state -
    // see timerCallback()), then Delay above Regen (feedback), per A/B column. Knobs use the
    // compact size here specifically to make room for the Sync/Subdivision controls without
    // growing the section. ----
    {
        auto delayInner = delayColumn;
        delayInner.removeFromTop(sectionPaddingTop);
        delayInner.removeFromLeft(27);
        delayInner.removeFromRight(27);

        const auto halfWidth = delayInner.getWidth() / 2;
        auto colA = delayInner.removeFromLeft(halfWidth);
        auto colB = delayInner;

        const auto shadowMargin = (int) GradientLookAndFeel::buttonShadowMargin;

        auto layoutDelayUnit = [this](juce::Rectangle<int> col, juce::Label& tag,
                                                      GradientToggle& syncToggle, GradientCombo& subdivisionCombo,
                                                      GradientRotaryKnob& delayKnob, GradientRotaryKnob& regenKnob)
        {
            auto tagArea = col.removeFromTop(unitTagHeight).withSizeKeepingCentre(unitTagWidth, unitTagHeight);
            tag.setBounds(tagArea);
            col.removeFromTop(8);

            auto toggleArea = col.removeFromTop(buttonRowHeight).withSizeKeepingCentre(syncToggleWidth, buttonRowHeight);
            syncToggle.button.setBounds(toggleArea.expanded(shadowMargin));
            col.removeFromTop(8);

            auto comboCell = col.removeFromTop(fieldLabelHeight + comboRowHeight).withSizeKeepingCentre(subdivisionComboWidth, fieldLabelHeight + comboRowHeight);
            subdivisionCombo.nameLabel.setBounds(comboCell.removeFromTop(fieldLabelHeight));
            subdivisionCombo.combo.setBounds(comboCell);
            col.removeFromTop(12);

            auto delayCell = col.removeFromTop(compactKnobSize + knobBlockExtra).withSizeKeepingCentre(compactKnobSize, compactKnobSize + knobBlockExtra);
            positionKnob(delayCell.withHeight(compactKnobSize), compactKnobSize, delayKnob);

            col.removeFromTop(16);
            auto regenCell = col.withSizeKeepingCentre(compactKnobSize, compactKnobSize + knobBlockExtra);
            positionKnob(regenCell.withHeight(compactKnobSize), compactKnobSize, regenKnob);
        };

        layoutDelayUnit(colA, delayUnitTagA, delaySyncToggleA, delaySubdivisionComboA, delayKnobA, feedbackKnobA);
        layoutDelayUnit(colB, delayUnitTagB, delaySyncToggleB, delaySubdivisionComboB, delayKnobB, feedbackKnobB);
    }

    // ---- DRIFT / OUTPUT: Drift, Mix, Out stacked, per A/B column (compact knob size). ----
    {
        auto doInner = driftOutputColumn;
        doInner.removeFromTop(sectionPaddingTop);
        doInner.removeFromLeft(43);
        doInner.removeFromRight(43);

        const auto halfWidth = doInner.getWidth() / 2;
        auto colA = doInner.removeFromLeft(halfWidth);
        auto colB = doInner;

        auto layoutDriftOutputUnit = [this](juce::Rectangle<int> col, juce::Label& tag,
                                             GradientRotaryKnob& drift, GradientRotaryKnob& mix, GradientRotaryKnob& out)
        {
            auto tagArea = col.removeFromTop(unitTagHeight).withSizeKeepingCentre(unitTagWidth, unitTagHeight);
            tag.setBounds(tagArea);
            col.removeFromTop(6);

            auto driftCell = col.removeFromTop(compactKnobSize + knobBlockExtra).withSizeKeepingCentre(compactKnobSize, compactKnobSize + knobBlockExtra);
            positionKnob(driftCell.withHeight(compactKnobSize), compactKnobSize, drift);

            col.removeFromTop(7);
            auto mixCell = col.removeFromTop(compactKnobSize + knobBlockExtra).withSizeKeepingCentre(compactKnobSize, compactKnobSize + knobBlockExtra);
            positionKnob(mixCell.withHeight(compactKnobSize), compactKnobSize, mix);

            col.removeFromTop(7);
            auto outCell = col.withSizeKeepingCentre(compactKnobSize, compactKnobSize + knobBlockExtra);
            positionKnob(outCell.withHeight(compactKnobSize), compactKnobSize, out);
        };

        layoutDriftOutputUnit(colA, driftOutputUnitTagA, driftKnobA, mixKnobA, outputKnobA);
        layoutDriftOutputUnit(colB, driftOutputUnitTagB, driftKnobB, mixKnobB, outputKnobB);
    }

    rebuildChassisTexture();
}
