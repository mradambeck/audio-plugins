// ============================================================================
// Adapted from the juce-hardware-panel-ui skill's canonical reference
// (caverns-delay/Source/PluginEditor.cpp). rebuildChassisTexture() and
// drawHardwareSection() are copied verbatim from that file, as is the
// chassis/panel/header/footer chrome inside paint(). The constructor,
// resized(), section names, and control layout are specific to Alloy's own
// Analog/Sub/FM/Arp/Mix parameter set, matching the approved mockup.
// ============================================================================

#include "PluginEditor.h"

#include "../../common/Presets/FactoryPreset.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - AlloyTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* AlloyAudioProcessor::createEditor()
{
    return new AlloyAudioProcessorEditor(*this);
}

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 62;
    constexpr int footerHeight = 36;

    constexpr int contentPaddingTop = 14;
    constexpr int contentPaddingSide = 22;
    constexpr int contentPaddingBottom = 6;
    constexpr int columnGap = 14;

    // Clearance for the badge straddling the top border - tightened from the mockup's 62px
    // (which left visible dead space in the Analog section once stretched to match FM's taller
    // natural height) so the whole window comfortably fits displays around 1080px tall; the
    // approved layout's grouping/proportions are otherwise unchanged, only the whitespace is.
    constexpr int sectionPaddingTop = 44;
    constexpr int sectionPaddingSide = 14;
    constexpr int sectionPaddingBottom = 10;
    constexpr int sectionContentGap = 8;    // gap between a section's direct top-level children

    // ---- Row 1 (Analog, FM) ----
    constexpr int analogGridColWidth = 100;
    constexpr int analogGridColGap = 24;
    constexpr int analogGridRowGap = 8;
    constexpr int analogTwoColGap = 28;
    constexpr int analogRightColWidth = 260;
    constexpr int analogRightColGap = 6;

    constexpr int fmRowWidth = 280; // wide enough for stack + Volume + a mini Vel> knob on one row

    constexpr int analogSectionWidth = analogGridColWidth * 2 + analogGridColGap + analogTwoColGap + analogRightColWidth + sectionPaddingSide * 2;
    constexpr int fmSectionWidth = fmRowWidth + sectionPaddingSide * 2;

    // ---- Row 2 (Sub, Arp, Mix) - widths chosen so the row totals match Row 1's exactly (980
    // window matches the approved mockup's device width; the mockup used flex-grow to stretch
    // these three sections to fill the same row width as Analog+FM, so fixed widths do the same
    // job here, in the ratio the mockup's own flex-grow values implied). ----
    constexpr int subSectionWidth = 130;
    constexpr int arpSectionWidth = 376;
    constexpr int mixSectionWidth = analogSectionWidth + columnGap + fmSectionWidth - subSectionWidth - arpSectionWidth - columnGap * 2;

    // The window's fixed content width (both pages share it - Row 2's section widths above were
    // deliberately chosen to sum to the same total as Row 1's). Content heights differ per page
    // instead: only one page is ever shown at once (see setShowingPageOne()), and the window is
    // resized to fit whichever one is active, rather than stacking both rows in one tall window
    // that didn't fit on smaller displays.
    constexpr int windowContentWidth = analogSectionWidth + columnGap + fmSectionWidth;
    constexpr int page1ContentHeight = 604; // Analog, FM (FM's Modulator has one extra row: Brightness)
    constexpr int page2ContentHeight = 314; // Sub, Arp, Mix

    constexpr int knobSize = 74;
    constexpr int miniKnobSize = 56;
    constexpr int knobNameHeight = 14;
    constexpr int knobTextBoxHeight = 14;
    constexpr int miniKnobNameHeight = 12;
    constexpr int miniKnobTextBoxHeight = 12;
    constexpr int knobCellHeight = knobSize + knobNameHeight + knobTextBoxHeight;
    constexpr int miniKnobCellHeight = miniKnobSize + miniKnobNameHeight + miniKnobTextBoxHeight;

    constexpr int comboHeight = 28;
    constexpr int comboWidth = 100;
    constexpr int comboLabelHeight = 14;
    constexpr int comboLabelGap = 6;
    constexpr int comboCellHeight = comboLabelHeight + comboLabelGap + comboHeight;

    constexpr int buttonWidth = 72;
    constexpr int buttonHeight = 28;
    constexpr int buttonGap = 12;

    constexpr int subLabelHeight = 18;

    constexpr int ctrlRowGap = 20;
    constexpr int ctrlRowGapTight = 12;

    constexpr int miniStackGap = 10;
}

void AlloyAudioProcessorEditor::setupKnob(KnobControl& kc, const juce::String& paramID,
                                           const juce::String& labelText, bool mini)
{
    // Slider is a member variable, so it's default-constructed (and builds its internal value
    // textbox Label via lookAndFeelChanged()) before the editor's own setLookAndFeel() call runs
    // in the constructor body - explicit here so it doesn't default to JUCE's global LookAndFeel.
    kc.slider.setLookAndFeel(&lookAndFeel);
    kc.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    kc.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                               mini ? miniKnobSize : knobSize, mini ? miniKnobTextBoxHeight : knobTextBoxHeight);
    kc.slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(kc.slider);

    kc.label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    kc.label.setJustificationType(juce::Justification::centred);
    kc.label.setFont(lookAndFeel.getDisplayFont(mini ? 8.5f : 10.0f).withExtraKerningFactor(0.09f));
    kc.label.setColour(juce::Label::textColourId, juce::Colour(0xffdcece9));
    addAndMakeVisible(kc.label);

    kc.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, paramID, kc.slider);
}

void AlloyAudioProcessorEditor::setupCombo(ComboControl& cc, const juce::String& paramID,
                                            const juce::String& labelText, const juce::StringArray& choices)
{
    cc.combo.setLookAndFeel(&lookAndFeel);
    cc.combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
    cc.combo.addItemList(choices, 1);
    addAndMakeVisible(cc.combo);

    cc.label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    cc.label.setJustificationType(juce::Justification::centred);
    cc.label.setFont(lookAndFeel.getDisplayFont(9.5f).withExtraKerningFactor(0.12f));
    cc.label.setColour(juce::Label::textColourId, juce::Colour(0xff8fa3a8));
    addAndMakeVisible(cc.label);

    cc.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, paramID, cc.combo);
}

void AlloyAudioProcessorEditor::setupLedButton(juce::ToggleButton& button,
                                                std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& attachment,
                                                const juce::String& paramID, const juce::String& text)
{
    button.setLookAndFeel(&lookAndFeel);
    button.setButtonText(text);
    addAndMakeVisible(button);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, paramID, button);
}

void AlloyAudioProcessorEditor::setupADSR(std::array<KnobControl, 4>& knobs, const juce::String paramIDs[4], bool mini)
{
    static const char* names[4] = { "Attack", "Decay", "Sustain", "Release" };
    for (int i = 0; i < 4; ++i)
        setupKnob(knobs[(size_t) i], paramIDs[i], names[i], mini);
}

void AlloyAudioProcessorEditor::setupSubLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.18f));
    label.setColour(juce::Label::textColourId, juce::Colour(0xff8fa3a8));
    addAndMakeVisible(label);
}

AlloyAudioProcessorEditor::AlloyAudioProcessorEditor(AlloyAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("ALLOY", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6ea8c9));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Stacked Mono Synth").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7f9096));
    addAndMakeVisible(tagLabel);

    // See common/Presets/FactoryPreset.h's setupPresetCombo() for why it's left unselected on
    // startup rather than showing the first preset's name.
    wildjag::setupPresetCombo(presetCombo, lookAndFeel, *this, processorRef);

    panicButton.setLookAndFeel(&lookAndFeel);
    panicButton.setButtonText("Panic");
    addAndMakeVisible(panicButton);
    panicButton.onClick = [this] { processorRef.requestPanic(); };

    // Shows the name of the page a click will switch TO, not the one currently showing.
    pageButton.setLookAndFeel(&lookAndFeel);
    pageButton.setButtonText("Sub / Arp / Mix");
    addAndMakeVisible(pageButton);
    pageButton.onClick = [this] { setShowingPageOne(!showingPageOne); };

    // ---- Analog ----
    setupCombo(analogWaveform, AlloyAudioProcessor::analogWaveformParamID, "Waveform", AlloyAudioProcessor::getWaveformChoices());
    setupCombo(analogOctave, AlloyAudioProcessor::analogOctaveParamID, "Octave", AlloyAudioProcessor::getOctaveChoices());
    setupCombo(analogUnison, AlloyAudioProcessor::analogUnisonParamID, "Unison", AlloyAudioProcessor::getUnisonChoices());
    setupKnob(analogDetune, AlloyAudioProcessor::analogDetuneParamID, "Detune", false);
    setupKnob(analogFilterCutoff, AlloyAudioProcessor::analogFilterCutoffParamID, "Cutoff", false);
    setupKnob(analogFilterResonance, AlloyAudioProcessor::analogFilterResonanceParamID, "Resonance", false);
    setupKnob(analogFilterEnvAmount, AlloyAudioProcessor::analogFilterEnvAmountParamID, "Env Amount", false);
    setupKnob(analogVelToFilter, AlloyAudioProcessor::analogVelocityToFilterParamID, "Vel>Filter", false);

    setupSubLabel(filterEnvLabel, "Filter Envelope");
    {
        const juce::String ids[4] = { AlloyAudioProcessor::analogFilterAttackParamID, AlloyAudioProcessor::analogFilterDecayParamID,
                                       AlloyAudioProcessor::analogFilterSustainParamID, AlloyAudioProcessor::analogFilterReleaseParamID };
        setupADSR(analogFilterEnvKnobs, ids, true);
    }
    setupSubLabel(ampEnvLabel, "Amp Envelope");
    {
        const juce::String ids[4] = { AlloyAudioProcessor::analogAmpAttackParamID, AlloyAudioProcessor::analogAmpDecayParamID,
                                       AlloyAudioProcessor::analogAmpSustainParamID, AlloyAudioProcessor::analogAmpReleaseParamID };
        setupADSR(analogAmpEnvKnobs, ids, true);
    }
    setupKnob(analogGlide, AlloyAudioProcessor::analogGlideTimeParamID, "Glide", false);
    setupKnob(analogVolume, AlloyAudioProcessor::analogVolumeParamID, "Volume", false);

    // ---- Sub ----
    setupLedButton(subEnabledButton, subEnabledAttachment, AlloyAudioProcessor::subEnabledParamID, "ON");
    setupCombo(subWaveform, AlloyAudioProcessor::subWaveformParamID, "Waveform", AlloyAudioProcessor::getSubWaveformChoices());
    setupCombo(subOctave, AlloyAudioProcessor::subOctaveParamID, "Octave", AlloyAudioProcessor::getSubOctaveChoices());
    setupKnob(subVolume, AlloyAudioProcessor::subVolumeParamID, "Volume", false);

    // ---- FM ----
    setupSubLabel(fmCarrierLabel, "Carrier");
    setupCombo(fmCarrierWaveform, AlloyAudioProcessor::fmCarrierWaveformParamID, "Waveform", AlloyAudioProcessor::getFmWaveformChoices());
    setupCombo(fmCarrierOctave, AlloyAudioProcessor::fmCarrierOctaveParamID, "Octave", AlloyAudioProcessor::getOctaveChoices());
    setupKnob(fmCarrierVolume, AlloyAudioProcessor::fmCarrierVolumeParamID, "Volume", false);
    setupKnob(fmVelocityToCarrier, AlloyAudioProcessor::fmVelocityToCarrierParamID, "Vel>Carrier", true);
    {
        const juce::String ids[4] = { AlloyAudioProcessor::fmCarrierAttackParamID, AlloyAudioProcessor::fmCarrierDecayParamID,
                                       AlloyAudioProcessor::fmCarrierSustainParamID, AlloyAudioProcessor::fmCarrierReleaseParamID };
        setupADSR(fmCarrierEnvKnobs, ids, true);
    }
    setupSubLabel(fmModulatorLabel, "Modulator");
    setupCombo(fmModulatorWaveform, AlloyAudioProcessor::fmModulatorWaveformParamID, "Waveform", AlloyAudioProcessor::getFmWaveformChoices());
    setupCombo(fmModulatorOctave, AlloyAudioProcessor::fmModulatorOctaveParamID, "Octave", AlloyAudioProcessor::getOctaveChoices());
    setupKnob(fmModulatorVolume, AlloyAudioProcessor::fmModulatorVolumeParamID, "Volume", false);
    setupKnob(fmVelocityToBrightness, AlloyAudioProcessor::fmVelocityToBrightnessParamID, "Vel>Bright", true);
    setupKnob(fmModulatorBrightness, AlloyAudioProcessor::fmModulatorBrightnessParamID, "Brightness", false);
    {
        const juce::String ids[4] = { AlloyAudioProcessor::fmModulatorAttackParamID, AlloyAudioProcessor::fmModulatorDecayParamID,
                                       AlloyAudioProcessor::fmModulatorSustainParamID, AlloyAudioProcessor::fmModulatorReleaseParamID };
        setupADSR(fmModulatorEnvKnobs, ids, true);
    }

    // ---- Arp ----
    setupLedButton(arpEnabledButton, arpEnabledAttachment, AlloyAudioProcessor::arpEnabledParamID, "ON");
    setupLedButton(arpSyncButton, arpSyncAttachment, AlloyAudioProcessor::arpSyncParamID, "SYNC");
    setupLedButton(arpHoldButton, arpHoldAttachment, AlloyAudioProcessor::arpHoldParamID, "HOLD");
    setupCombo(arpDivision, AlloyAudioProcessor::arpDivisionParamID, "Division", AlloyAudioProcessor::getArpDivisionChoices());
    setupCombo(arpPattern, AlloyAudioProcessor::arpPatternParamID, "Pattern", AlloyAudioProcessor::getArpPatternChoices());
    setupCombo(arpOctaveRange, AlloyAudioProcessor::arpOctaveRangeParamID, "Oct. Range", AlloyAudioProcessor::getArpOctaveRangeChoices());
    setupKnob(arpRate, AlloyAudioProcessor::arpRateParamID, "Rate", false);
    setupKnob(arpGate, AlloyAudioProcessor::arpGateParamID, "Gate", false);

    // ---- Mix ----
    setupKnob(mixDrive, AlloyAudioProcessor::mixDriveParamID, "Drive", false);
    setupKnob(mixTone, AlloyAudioProcessor::mixToneParamID, "Tone", false);
    setupKnob(mixOutput, AlloyAudioProcessor::mixOutputParamID, "Output", false);
    setupKnob(mixAge, AlloyAudioProcessor::mixAgeParamID, "Age", false);

    updatePageVisibility();
    setSize(windowContentWidth + contentPaddingSide * 2 + chassisMargin * 2, contentHeightForPage(showingPageOne));

    // Sets the Rate knob's initial enabled/dimmed state immediately (Arp Sync defaults to on),
    // rather than waiting for the first timer tick.
    timerCallback();
    startTimerHz(30);
}

AlloyAudioProcessorEditor::~AlloyAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void AlloyAudioProcessorEditor::timerCallback()
{
    // Rate is only read by the DSP while Sync is off (see processBlock) - disabling/dimming it
    // while Sync is on makes that inertness visible rather than leaving a live-looking knob that
    // silently does nothing. Polled rather than driven off the button's onClick so it also
    // reflects changes from automation, undo, or preset loads.
    const auto syncOn = arpSyncButton.getToggleState();
    arpRate.slider.setEnabled(!syncOn);
    arpRate.slider.setAlpha(syncOn ? 0.5f : 1.0f);
}

int AlloyAudioProcessorEditor::contentHeightForPage(bool pageOne) const
{
    return chassisMargin * 2 + headerHeight + contentPaddingTop
           + (pageOne ? page1ContentHeight : page2ContentHeight)
           + contentPaddingBottom + footerHeight;
}

void AlloyAudioProcessorEditor::updatePageVisibility()
{
    const auto page1 = showingPageOne;
    const auto page2 = !showingPageOne;

    analogWaveform.setVisible(page1);
    analogOctave.setVisible(page1);
    analogUnison.setVisible(page1);
    analogDetune.setVisible(page1);
    analogFilterCutoff.setVisible(page1);
    analogFilterResonance.setVisible(page1);
    analogFilterEnvAmount.setVisible(page1);
    analogVelToFilter.setVisible(page1);
    filterEnvLabel.setVisible(page1);
    ampEnvLabel.setVisible(page1);
    for (auto& k : analogFilterEnvKnobs) k.setVisible(page1);
    for (auto& k : analogAmpEnvKnobs) k.setVisible(page1);
    analogGlide.setVisible(page1);
    analogVolume.setVisible(page1);

    fmCarrierLabel.setVisible(page1);
    fmModulatorLabel.setVisible(page1);
    fmCarrierWaveform.setVisible(page1);
    fmCarrierOctave.setVisible(page1);
    fmCarrierVolume.setVisible(page1);
    fmVelocityToCarrier.setVisible(page1);
    for (auto& k : fmCarrierEnvKnobs) k.setVisible(page1);
    fmModulatorWaveform.setVisible(page1);
    fmModulatorOctave.setVisible(page1);
    fmModulatorVolume.setVisible(page1);
    fmVelocityToBrightness.setVisible(page1);
    fmModulatorBrightness.setVisible(page1);
    for (auto& k : fmModulatorEnvKnobs) k.setVisible(page1);

    subEnabledButton.setVisible(page2);
    subWaveform.setVisible(page2);
    subOctave.setVisible(page2);
    subVolume.setVisible(page2);

    arpEnabledButton.setVisible(page2);
    arpSyncButton.setVisible(page2);
    arpHoldButton.setVisible(page2);
    arpDivision.setVisible(page2);
    arpPattern.setVisible(page2);
    arpOctaveRange.setVisible(page2);
    arpRate.setVisible(page2);
    arpGate.setVisible(page2);

    mixDrive.setVisible(page2);
    mixTone.setVisible(page2);
    mixOutput.setVisible(page2);
    mixAge.setVisible(page2);

    pageButton.setButtonText(showingPageOne ? "Sub / Arp / Mix" : "Analog / FM");
}

void AlloyAudioProcessorEditor::setShowingPageOne(bool showPageOne)
{
    if (showingPageOne == showPageOne)
        return;

    showingPageOne = showPageOne;
    updatePageVisibility();
    setSize(windowContentWidth + contentPaddingSide * 2 + chassisMargin * 2, contentHeightForPage(showingPageOne));
    repaint();
}

// COPY-VERBATIM (see banner at top of file): procedural chassis grain, no image assets, no
// per-plugin parameters.
void AlloyAudioProcessorEditor::rebuildChassisTexture()
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
void AlloyAudioProcessorEditor::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void AlloyAudioProcessorEditor::paint(juce::Graphics& g)
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

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to Alloy's own parameters. Only the
    // active page's sections are drawn, matching which controls are currently visible. ---
    if (showingPageOne)
    {
        drawHardwareSection(g, analogSectionBounds, "Analog");
        drawHardwareSection(g, fmSectionBounds, "FM");
    }
    else
    {
        drawHardwareSection(g, subSectionBounds, "Sub");
        drawHardwareSection(g, arpSectionBounds, "Arp");
        drawHardwareSection(g, mixSectionBounds, "Mix");
    }
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("ALLOY \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void AlloyAudioProcessorEditor::positionKnob(juce::Rectangle<int> cell, int size, KnobControl& kc)
{
    const bool mini = (size != knobSize);
    const auto nameH = mini ? miniKnobNameHeight : knobNameHeight;
    const auto textH = mini ? miniKnobTextBoxHeight : knobTextBoxHeight;

    juce::Rectangle<int> unit(cell.getCentreX() - size / 2, cell.getY(), size, size + nameH + textH);
    kc.slider.setBounds(unit);
    kc.label.setBounds(unit.getX(), unit.getY() + size, size, nameH);
}

void AlloyAudioProcessorEditor::positionCombo(juce::Rectangle<int> cell, int width, ComboControl& cc)
{
    juce::Rectangle<int> unit(cell.getCentreX() - width / 2, cell.getY(), width, comboCellHeight);
    cc.label.setBounds(unit.removeFromTop(comboLabelHeight));
    unit.removeFromTop(comboLabelGap);
    cc.combo.setBounds(unit);
}

void AlloyAudioProcessorEditor::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    const auto panicFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto panicTextWidth = juce::GlyphArrangement::getStringWidth(panicFont, "PANIC");
    const auto panicWidth = (int) std::ceil(panicTextWidth + 32.0f);
    panicButton.setBounds(header.removeFromRight(panicWidth).withSizeKeepingCentre(panicWidth, buttonHeight)
                               .expanded((int) AlloyLookAndFeel::buttonShadowMargin));

    header.removeFromRight(buttonGap);
    const auto pageButtonTextWidth = juce::GlyphArrangement::getStringWidth(panicFont, pageButton.getButtonText().toUpperCase());
    const auto pageButtonWidth = (int) std::ceil(pageButtonTextWidth + 32.0f);
    pageButton.setBounds(header.removeFromRight(pageButtonWidth).withSizeKeepingCentre(pageButtonWidth, buttonHeight)
                              .expanded((int) AlloyLookAndFeel::buttonShadowMargin));

    header.removeFromRight(buttonGap);
    presetCombo.setBounds(header.removeFromRight(128).withSizeKeepingCentre(128, 28));

    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "ALLOY") + 8;
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

    // Only the active page's row is laid out - the window is already sized to fit exactly that
    // row (see contentHeightForPage()), and the other page's controls stay wherever they were
    // last positioned since they're invisible.
    if (showingPageOne)
    {
    // ---- Row 1: Analog, FM ----
    auto row1 = content.removeFromTop(page1ContentHeight);
    auto analogColumn = row1.removeFromLeft(analogSectionWidth);
    row1.removeFromLeft(columnGap);
    auto fmColumn = row1;

    analogSectionBounds = analogColumn.toFloat();
    fmSectionBounds = fmColumn.toFloat();

    // ==== Analog: two-col (left = 2-col grid of selectors/knobs, right = envelopes + Glide/Vol) ====
    {
        auto inner = analogColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromLeft(sectionPaddingSide);
        inner.removeFromRight(sectionPaddingSide);
        inner.removeFromBottom(sectionPaddingBottom);

        auto leftCol = inner.removeFromLeft(analogGridColWidth * 2 + analogGridColGap);
        inner.removeFromLeft(analogTwoColGap);
        auto rightCol = inner;

        // Left: 4 grid rows of 2 columns each.
        auto gridRow1 = leftCol.removeFromTop(comboCellHeight);
        leftCol.removeFromTop(analogGridRowGap);
        auto gridRow2 = leftCol.removeFromTop(knobCellHeight);
        leftCol.removeFromTop(analogGridRowGap);
        auto gridRow3 = leftCol.removeFromTop(knobCellHeight);
        leftCol.removeFromTop(analogGridRowGap);
        auto gridRow4 = leftCol.removeFromTop(knobCellHeight);

        auto col1Of = [](juce::Rectangle<int> row) { return row.removeFromLeft(analogGridColWidth); };
        auto col2Of = [](juce::Rectangle<int> row) { row.removeFromLeft(analogGridColWidth + analogGridColGap); return row; };

        positionCombo(col1Of(gridRow1), analogGridColWidth, analogWaveform);
        positionCombo(col2Of(gridRow1), analogGridColWidth, analogOctave);

        positionCombo(col1Of(gridRow2), analogGridColWidth, analogUnison);
        positionKnob(col2Of(gridRow2), knobSize, analogDetune);

        positionKnob(col1Of(gridRow3), knobSize, analogFilterCutoff);
        positionKnob(col2Of(gridRow3), knobSize, analogFilterResonance);

        positionKnob(col1Of(gridRow4), knobSize, analogFilterEnvAmount);
        positionKnob(col2Of(gridRow4), knobSize, analogVelToFilter);

        // Right: Filter Envelope (4 mini), Amp Envelope (4 mini), Glide/Volume.
        filterEnvLabel.setBounds(rightCol.removeFromTop(subLabelHeight));
        rightCol.removeFromTop(analogRightColGap);

        auto filterEnvRow = rightCol.removeFromTop(miniKnobCellHeight);
        const auto miniQuadWidth = filterEnvRow.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
            positionKnob(filterEnvRow.removeFromLeft(miniQuadWidth), miniKnobSize, analogFilterEnvKnobs[(size_t) i]);
        rightCol.removeFromTop(analogRightColGap);

        ampEnvLabel.setBounds(rightCol.removeFromTop(subLabelHeight));
        rightCol.removeFromTop(analogRightColGap);

        auto ampEnvRow = rightCol.removeFromTop(miniKnobCellHeight);
        for (int i = 0; i < 4; ++i)
            positionKnob(ampEnvRow.removeFromLeft(miniQuadWidth), miniKnobSize, analogAmpEnvKnobs[(size_t) i]);
        rightCol.removeFromTop(analogRightColGap);

        auto glideVolRow = rightCol.removeFromTop(knobCellHeight);
        const auto glideVolHalf = glideVolRow.getWidth() / 2;
        positionKnob(glideVolRow.removeFromLeft(glideVolHalf), knobSize, analogGlide);
        positionKnob(glideVolRow, knobSize, analogVolume);
    }

    // ==== FM: Carrier (selector stack + Volume, then 4 mini ADSR), Modulator (same) ====
    {
        auto inner = fmColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromLeft(sectionPaddingSide);
        inner.removeFromRight(sectionPaddingSide);
        inner.removeFromBottom(sectionPaddingBottom);

        // extraKnob is only non-null for the Modulator (Brightness) - Carrier has no equivalent,
        // so it gets its own row between the stack/Volume/Vel row and the envelope row only when
        // present, rather than every operator needing the same fixed row count.
        auto layoutOperator = [this, &inner](juce::Label& opLabel, ComboControl& waveform, ComboControl& octave,
                                              KnobControl& volume, KnobControl& velKnob, std::array<KnobControl, 4>& envKnobs,
                                              KnobControl* extraKnob)
        {
            opLabel.setBounds(inner.removeFromTop(subLabelHeight));
            inner.removeFromTop(sectionContentGap);

            auto stackRow = inner.removeFromTop(knobCellHeight);
            auto stackCell = stackRow.removeFromLeft(comboWidth);
            positionCombo(stackCell.removeFromTop(comboCellHeight), comboWidth, waveform);
            stackCell.removeFromTop(miniStackGap);
            positionCombo(stackCell, comboWidth, octave);

            stackRow.removeFromLeft(ctrlRowGap);
            auto volVelGroup = stackRow.withSizeKeepingCentre(knobSize + ctrlRowGap + miniKnobSize, stackRow.getHeight());
            positionKnob(volVelGroup.removeFromLeft(knobSize), knobSize, volume);
            volVelGroup.removeFromLeft(ctrlRowGap);
            positionKnob(volVelGroup, miniKnobSize, velKnob);
            inner.removeFromTop(sectionContentGap);

            if (extraKnob != nullptr)
            {
                auto extraRow = inner.removeFromTop(knobCellHeight);
                positionKnob(extraRow.withSizeKeepingCentre(knobSize, extraRow.getHeight()), knobSize, *extraKnob);
                inner.removeFromTop(sectionContentGap);
            }

            auto envRow = inner.removeFromTop(miniKnobCellHeight);
            const auto quadWidth = envRow.getWidth() / 4;
            for (int i = 0; i < 4; ++i)
                positionKnob(envRow.removeFromLeft(quadWidth), miniKnobSize, envKnobs[(size_t) i]);
            inner.removeFromTop(sectionContentGap);
        };

        layoutOperator(fmCarrierLabel, fmCarrierWaveform, fmCarrierOctave, fmCarrierVolume, fmVelocityToCarrier, fmCarrierEnvKnobs, nullptr);
        layoutOperator(fmModulatorLabel, fmModulatorWaveform, fmModulatorOctave, fmModulatorVolume, fmVelocityToBrightness, fmModulatorEnvKnobs, &fmModulatorBrightness);
    }
    }
    else
    {
    // ---- Row 2: Sub, Arp, Mix ----
    auto row2 = content.removeFromTop(page2ContentHeight);
    auto subColumn = row2.removeFromLeft(subSectionWidth);
    row2.removeFromLeft(columnGap);
    auto arpColumn = row2.removeFromLeft(arpSectionWidth);
    row2.removeFromLeft(columnGap);
    auto mixColumn = row2;

    subSectionBounds = subColumn.toFloat();
    arpSectionBounds = arpColumn.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // ==== Sub: ON button, Waveform, Octave, Volume - stacked single-column rows ====
    {
        auto inner = subColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromLeft(sectionPaddingSide);
        inner.removeFromRight(sectionPaddingSide);
        inner.removeFromBottom(sectionPaddingBottom);

        auto buttonRow = inner.removeFromTop(buttonHeight);
        subEnabledButton.setBounds(buttonRow.withSizeKeepingCentre(buttonWidth, buttonHeight)
                                        .expanded((int) AlloyLookAndFeel::buttonShadowMargin));
        inner.removeFromTop(sectionContentGap);

        positionCombo(inner.removeFromTop(comboCellHeight), comboWidth, subWaveform);
        inner.removeFromTop(sectionContentGap);

        positionCombo(inner.removeFromTop(comboCellHeight), comboWidth, subOctave);
        inner.removeFromTop(sectionContentGap);

        positionKnob(inner.removeFromTop(knobCellHeight), knobSize, subVolume);
    }

    // ==== Arp: buttons row, selectors row, knobs row ====
    {
        auto inner = arpColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromLeft(sectionPaddingSide);
        inner.removeFromRight(sectionPaddingSide);
        inner.removeFromBottom(sectionPaddingBottom);

        auto buttonRow = inner.removeFromTop(buttonHeight)
                              .withSizeKeepingCentre(buttonWidth * 3 + buttonGap * 2, buttonHeight);
        arpEnabledButton.setBounds(buttonRow.removeFromLeft(buttonWidth).expanded((int) AlloyLookAndFeel::buttonShadowMargin));
        buttonRow.removeFromLeft(buttonGap);
        arpSyncButton.setBounds(buttonRow.removeFromLeft(buttonWidth).expanded((int) AlloyLookAndFeel::buttonShadowMargin));
        buttonRow.removeFromLeft(buttonGap);
        arpHoldButton.setBounds(buttonRow.expanded((int) AlloyLookAndFeel::buttonShadowMargin));
        inner.removeFromTop(sectionContentGap);

        auto selectorRow = inner.removeFromTop(comboCellHeight);
        const auto selectorThird = selectorRow.getWidth() / 3;
        positionCombo(selectorRow.removeFromLeft(selectorThird), comboWidth, arpDivision);
        positionCombo(selectorRow.removeFromLeft(selectorThird), comboWidth, arpPattern);
        positionCombo(selectorRow, comboWidth, arpOctaveRange);
        inner.removeFromTop(sectionContentGap);

        auto knobRow = inner.removeFromTop(knobCellHeight);
        const auto knobHalf = knobRow.getWidth() / 2;
        positionKnob(knobRow.removeFromLeft(knobHalf), knobSize, arpRate);
        positionKnob(knobRow, knobSize, arpGate);
    }

    // ==== Mix: 2x2 grid - Drive/Tone, then Output/Age ====
    {
        auto inner = mixColumn;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromLeft(sectionPaddingSide);
        inner.removeFromRight(sectionPaddingSide);
        inner.removeFromBottom(sectionPaddingBottom);

        auto knobRow1 = inner.removeFromTop(knobCellHeight);
        const auto knobHalf1 = knobRow1.getWidth() / 2;
        positionKnob(knobRow1.removeFromLeft(knobHalf1), knobSize, mixDrive);
        positionKnob(knobRow1, knobSize, mixTone);
        inner.removeFromTop(sectionContentGap);

        auto knobRow2 = inner.removeFromTop(knobCellHeight);
        const auto knobHalf2 = knobRow2.getWidth() / 2;
        positionKnob(knobRow2.removeFromLeft(knobHalf2), knobSize, mixOutput);
        positionKnob(knobRow2, knobSize, mixAge);
    }
    }

    rebuildChassisTexture();
}
