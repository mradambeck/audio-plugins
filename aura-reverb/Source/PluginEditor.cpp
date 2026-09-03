// Hardware-panel UI, built from the approved mockup (aura-reverb/mockups/aura-mockup-v1.html,
// 2026-09-02) via the juce-hardware-panel-ui skill. rebuildChassisTexture(), drawHardwareSection(),
// and the chassis/panel/header/footer chrome in paint() are COPY-VERBATIM from
// caverns-delay/Source/PluginEditor.cpp (the skill's canonical reference) - they don't reference
// any per-plugin content. Everything else (constructor's control set/labels/window size,
// resized()'s column layout, drawHardwareSection() call sites) is Aura-specific, matching the
// mockup's TONE/TIMING/MIX three-section layout.

#include "PluginEditor.h"
#include "BinaryData.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - AuraTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* AuraAudioProcessor::createEditor()
{
    return new AuraAudioProcessorEditor(*this);
}

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 30;

    constexpr int contentPaddingTop = 22;
    constexpr int contentPaddingSide = 18;
    constexpr int contentPaddingBottom = 11;
    constexpr int columnGap = 26;

    // Content-driven, matching the mockup's own flex:0 0 auto (content-hugging) sections exactly
    // - see the mockup's own comment on why proportional flex-grow sections were abandoned there.
    // TONE: max(2 regular knobs + gap, 1 hero knob) + side padding. TIMING: 1 hero knob (its own
    // widest row) + side padding. MIX (not given its own constant - sized as the remainder of
    // setSize()'s total width, same convention as Caverns' own mixColumn): 2 fader cells + 1 gap
    // + side padding, ~166px (was ~246px/3 lanes before Pre-Gain was removed, 2026-09-03 - see
    // setSize()'s own comment).
    // Widened from the mockup's own first-pass estimate (222/132) after the mandatory mockup-vs-
    // real-app screenshot comparison (see the juce-hardware-panel-ui skill's Verification
    // methodology) showed Tone/Timing reading visibly more cramped in the real app than in the
    // mockup - real JUCE knob cells reserve knobNameHeight+knobTextBoxHeight (48px) for the name+
    // value stack below each knob, taller than the mockup's own compact ~32px CSS approximation,
    // which changes the section's natural proportions. Mix's fader-cell width deliberately did
    // NOT shrink to compensate (see mixColumn's own comment) - Wet's real 0-200% range needs
    // "200.0%"-length value text the mockup's shorter example values didn't exercise.
    constexpr int toneColumnWidth = 250;
    constexpr int timingColumnWidth = 150;

    constexpr int sectionPaddingTop = 40;    // clearance for the badge straddling the top border
    constexpr int sectionPaddingSide = 10;   // matches the mockup's tightened hw-section padding
    constexpr int sectionPaddingBottom = 20;

    constexpr int defaultKnobSize = 88;
    constexpr int heroKnobSize = 112;        // Color/Decay read larger, per the approved mockup
    constexpr int knobRowVerticalGap = 16;   // vertical gap between a section's two knob rows
    constexpr int knobNameHeight = 28;       // gap between the knob and its value textbox, occupied
                                              // by the name label -- mockup DOM order is knob, name,
                                              // value (not name-above-knob like a typical plugin)
    constexpr int knobTextBoxHeight = 20;

    constexpr int fieldNameHeight = 20;      // headroom for a fader's name label, which IS above
                                              // (Wet/Dry match the more usual above-fader
                                              // convention, opposite of the knob-name-below rule)
    constexpr int faderGap = 14;             // matches the mockup's tightened fader-row gap

    // Converter: a small 3-position slide switch next to Low Cut (see PluginEditor.h's
    // ConverterSwitch for the hardware reference photo this is modelled on), plus its own name
    // label below (matching Low Cut's/Color's own knob-name style).
    constexpr int converterSwitchWidth = 82;
    constexpr int converterTickRowHeight = 14;   // the "8  16  24BIT" row painted above the track
    constexpr int converterTrackHeight = 16;
    constexpr int converterSwitchHeight = converterTickRowHeight + converterTrackHeight;
    constexpr int converterSwitchLabelGap = 6;
}

void AuraEditorContent::setupRotarySlider(juce::Slider& slider, juce::Label& label,
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
    // in resized() instead, in the gap HardwarePanelLookAndFeel::drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void AuraEditorContent::setupVerticalSlider(juce::Slider& slider, juce::Label& label,
                                             const juce::String& labelText)
{
    slider.setLookAndFeel(&lookAndFeel);   // see setupRotarySlider() for why this is necessary
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, knobTextBoxHeight);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the fader track itself is only ~13px wide, and
    // a Label attached that way is forced to match its owner's width - "PRE-GAIN" would get
    // silently ellipsized. Positioned manually in resized() instead, wide enough to fit the text,
    // matching the mockup's fader-cell (label centred over the whole cell, not the thin track).
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void AuraEditorContent::setupConverterSwitch()
{
    converterLabel.setText(juce::String("Converter").toUpperCase(), juce::dontSendNotification);
    converterLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(converterLabel);

    addAndMakeVisible(converterSwitch);
    converterSwitch.onValueChanged = [this](int newIndex)
    {
        auto* param = processorRef.apvts.getParameter(AuraAudioProcessor::bitDepthParamID);
        if (param == nullptr)
            return;
        param->setValueNotifyingHost(param->convertTo0to1((float) newIndex));
    };
}

void AuraEditorContent::ConverterSwitch::setValue(int newValue, juce::NotificationType notify)
{
    const auto clamped = juce::jlimit(0, 2, newValue);
    if (clamped == value)
        return;
    value = clamped;
    repaint();
    if (notify == juce::sendNotification && onValueChanged != nullptr)
        onValueChanged(value);
}

void AuraEditorContent::ConverterSwitch::mouseDown(const juce::MouseEvent& e)
{
    const auto segment = juce::jlimit(0, 2, (int) (e.position.x / (float) getWidth() * 3.0f));
    setValue(segment, juce::sendNotification);
}

// Modelled on the cropped Roland RE-501 LEVEL-switch reference photo (see PluginEditor.h's
// ConverterSwitch comment): a tick-value row ("8   16   24BIT", spaced over the track's own 3
// segment centres, unit suffix only on the last one - matching the reference's own "-50dB") above
// a track matching the Wet/Dry faders' own unfilled-track colours, with a black, ridged thumb
// block at the current position (Adam, 2026-09-03 - matches the reference photo's own black
// switch, not the cream/tan a pushbutton LED cap would use).
void AuraEditorContent::ConverterSwitch::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto tickRow = bounds.removeFromTop(bounds.getHeight() - (float) converterTrackHeight);
    auto trackBounds = bounds;

    static const juce::StringArray tickLabels { "8", "16", "24BIT" };
    const auto tickFont = lookAndFeel.getDisplayFont(9.0f).withExtraKerningFactor(0.02f);
    g.setFont(tickFont);
    const auto segmentWidth = tickRow.getWidth() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto isSelected = (i == value);
        g.setColour(juce::Colour(0xffdcece9).withAlpha(isSelected ? 1.0f : 0.55f));
        const juce::Rectangle<float> tickCell(tickRow.getX() + segmentWidth * (float) i, tickRow.getY(),
                                               segmentWidth, tickRow.getHeight());
        g.drawText(tickLabels[i], tickCell, juce::Justification::centred);
    }

    // The track ("the larger unselected box") uses the exact same fill/outline colours as the
    // Wet/Dry faders' own unfilled track (HardwarePanelLookAndFeel::drawLinearSlider's
    // faderTrackTop/faderTrackBottom + comboOutline) - not this control's own invented colours,
    // per Adam's 2026-09-03 request to match the sliders directly.
    constexpr float trackCornerSize = 2.0f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff14191a), trackBounds.getX(), trackBounds.getY(),
                                            juce::Colour(0xff1c2224), trackBounds.getX(), trackBounds.getBottom(), false));
    g.fillRoundedRectangle(trackBounds, trackCornerSize);

    // Outline only on the bottom/right edges, not a uniform stroke - reads as a 3D inset bevel
    // with light raking in from the top-left (matching the panel's own established key-light
    // direction), rather than a flat uniform border.
    g.setColour(juce::Colour(0xff3a4245));
    g.drawLine(trackBounds.getX(), trackBounds.getBottom() - 0.5f, trackBounds.getRight(), trackBounds.getBottom() - 0.5f, 1.0f);
    g.drawLine(trackBounds.getRight() - 0.5f, trackBounds.getY(), trackBounds.getRight() - 0.5f, trackBounds.getBottom(), 1.0f);

    // 2 notch lines dividing the track into 3 equal segments.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    for (int i = 1; i < 3; ++i)
    {
        const auto x = trackBounds.getX() + trackBounds.getWidth() / 3.0f * (float) i;
        g.drawLine(x, trackBounds.getY() + 2.0f, x, trackBounds.getBottom() - 2.0f, 1.0f);
    }

    // The switch/thumb itself: black, matching the reference photo (not the cream/tan colour a
    // pushbutton LED cap would use) - a few short ridge lines for grip, same knurled-cap idea as
    // before, just recoloured.
    const auto thumbBounds = trackBounds
        .withWidth(trackBounds.getWidth() / 3.0f)
        .withX(trackBounds.getX() + trackBounds.getWidth() / 3.0f * (float) value)
        .reduced(1.5f, 1.5f);

    juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 3, {0, 1}).drawForRectangle(g, thumbBounds.getSmallestIntegerContainer());
    g.setColour(juce::Colour(0xff0c0e0f));
    g.fillRoundedRectangle(thumbBounds, 1.5f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(thumbBounds, 1.5f, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.15f));
    const auto ridgeCount = 3;
    for (int i = 1; i <= ridgeCount; ++i)
    {
        const auto x = thumbBounds.getX() + thumbBounds.getWidth() / (float) (ridgeCount + 1) * (float) i;
        g.drawLine(x, thumbBounds.getY() + 2.0f, x, thumbBounds.getBottom() - 2.0f, 1.0f);
    }
}

AuraEditorContent::AuraEditorContent(AuraAudioProcessor& p)
    : processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("AURA", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffDCAC52));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Ambience Reverb").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, AuraAudioProcessor::bypassParamID, bypassButton);

    setupRotarySlider(lowCutSlider, lowCutLabel, "Low Cut");
    setupConverterSwitch();
    setupRotarySlider(colorSlider, colorLabel, "Color");
    setupRotarySlider(preDelaySlider, preDelayLabel, "Pre-Delay");
    setupRotarySlider(decaySlider, decayLabel, "Decay");
    setupVerticalSlider(wetSlider, wetLabel, "Wet");
    setupVerticalSlider(drySlider, dryLabel, "Dry");

    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, AuraAudioProcessor::lowCutHzParamID, lowCutSlider);
    colorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, AuraAudioProcessor::highDbParamID, colorSlider);
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, AuraAudioProcessor::preDelayMsParamID, preDelaySlider);
    decayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, AuraAudioProcessor::timeSecondsParamID, decaySlider);
    wetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, AuraAudioProcessor::wetParamID, wetSlider);
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, AuraAudioProcessor::dryParamID, drySlider);

    // Width reduced from 764 to 684 (2026-09-03) when Pre-Gain was removed - Mix went from 3 fader
    // lanes to 2, shrinking mixColumn's remainder width by exactly one fader cell + one gap
    // (~80px) so the two remaining lanes keep the same per-lane width as before, rather than
    // stretching wider to fill the old 3-lane space. Height back to the original 529 (2026-09-03)
    // now that Converter is a compact switch next to Low Cut again, not its own extra Tone row.
    setSize(684, 529);

    // Drives the Converter switch's resync-on-change (see timerCallback()) - it needs to reflect
    // the current choice regardless of source (switch click, host automation, preset load), not
    // just clicks handled locally.
    startTimerHz(30);
}

AuraEditorContent::~AuraEditorContent()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

// Re-syncs the Converter switch's value from the live "bitDepth" parameter only when the
// selection actually changed, rather than unconditionally on every 30Hz tick.
// ConverterSwitch::setValue's dontSendNotification path is required here - sendNotification would
// re-fire onValueChanged and risk a feedback loop with the parameter write that caused this sync.
void AuraEditorContent::timerCallback()
{
    const auto currentIndex = (int) processorRef.apvts.getRawParameterValue(AuraAudioProcessor::bitDepthParamID)->load();
    if (currentIndex != lastSyncedConverterIndex)
    {
        lastSyncedConverterIndex = currentIndex;
        converterSwitch.setValue(currentIndex, juce::dontSendNotification);
    }
}

// COPY-VERBATIM (see file banner): procedural chassis grain, no image assets, no per-plugin
// parameters.
void AuraEditorContent::rebuildChassisTexture()
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

// COPY-VERBATIM (see file banner): unbroken border + centred badge that hugs its text, sitting
// inside the border rather than straddling it. Badge/ink colours are read from the LookAndFeel
// rather than held as separate literals here.
void AuraEditorContent::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void AuraEditorContent::paint(juce::Graphics& g)
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

    // --- PLUGIN-SPECIFIC: section names/grouping, matched to the approved mockup. ---
    drawHardwareSection(g, toneSectionBounds, "Tone");
    drawHardwareSection(g, timingSectionBounds, "Timing");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("AURA \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void AuraEditorContent::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    // Content-based width (LED + gap + text + padding), matching the mockup's .hw-toggle.
    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) AuraLookAndFeel::buttonShadowMargin));

    // Baseline-align "AURA" and the tag line (mockup: .brand{align-items:baseline}).
    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "AURA") + 8;
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
    auto timingColumn = content.removeFromLeft(timingColumnWidth);
    content.removeFromLeft(columnGap);
    // Remainder, not a fixed constant (same convention as Caverns' own mixColumn) - deliberately
    // left generous rather than shrunk to match the mockup's own narrower Mix proportion: the
    // mockup's example fader values ("0.0%") never exercised Wet's real 0-200% range, and
    // "200.0%" needs meaningfully more textbox width than the mockup's assumption did.
    auto mixColumn = content;

    toneSectionBounds = toneColumn.toFloat();
    timingSectionBounds = timingColumn.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value): the slider's own bounds span knob + name-gap +
    // built-in value textbox, and HardwarePanelLookAndFeel::drawRotarySlider flush-tops the
    // circle within that, leaving the name-gap blank for this label to occupy.
    auto positionKnob = [](juce::Rectangle<int> cell, int knobSize, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobSize + knobNameHeight + knobTextBoxHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    // ---- Tone: Low Cut + Converter (regular knob + small switch, top row), Color (hero-sized,
    // own row below) - back to the original 2-row shape (Adam, 2026-09-03): Color stays hero-sized
    // (a shared row with Low Cut previously forced it down to regular size, which read as
    // awkward), and Converter moved from its own dedicated row into a compact switch beside Low
    // Cut instead, closer in footprint to a hardware panel's own small utility switches. ----
    auto toneInner = toneColumn;
    toneInner.removeFromTop(sectionPaddingTop);
    toneInner.removeFromLeft(sectionPaddingSide);
    toneInner.removeFromRight(sectionPaddingSide);
    toneInner.removeFromBottom(sectionPaddingBottom);

    auto toneRow1 = toneInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto toneHalfWidth = toneRow1.getWidth() / 2;
    positionKnob(toneRow1.removeFromLeft(toneHalfWidth), defaultKnobSize, lowCutSlider, lowCutLabel);

    // Converter: the switch + its name label below. The label's Y is taken directly from
    // lowCutLabel's own Y (set by positionKnob() just above), rather than centring this whole
    // group independently within the row - Adam's 2026-09-03 request was specifically that
    // "CONVERTER" line up with "LOW CUT", and reusing the real value here guarantees that exactly
    // regardless of either control's own vertical-centring math. Cell width comes from whichever
    // of the switch/label is wider (the "CONVERTER" text, measured rather than guessed, same
    // discipline as bypassButton's own content-driven width).
    const auto converterLabelFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.09f);
    const auto converterLabelWidth = (int) std::ceil(
        juce::GlyphArrangement::getStringWidth(converterLabelFont, converterLabel.getText())) + 4;
    const auto converterCellWidth = juce::jmax(converterSwitchWidth, converterLabelWidth);

    auto converterCell = toneRow1.withSizeKeepingCentre(converterCellWidth, toneRow1.getHeight());
    // Height matches knobNameHeight (lowCutLabel's own height), not a separately-invented value -
    // both labels use Justification::centred, so matching bounds height (not just Y) is what
    // actually keeps their text vertically centred on the same line; a shorter height here would
    // centre "CONVERTER" a few px higher than "LOW CUT" despite sharing the same top Y.
    converterLabel.setBounds(converterCell.getX(), lowCutLabel.getY(), converterCellWidth, knobNameHeight);
    converterSwitch.setBounds(converterCell.getX() + (converterCellWidth - converterSwitchWidth) / 2,
                               converterLabel.getY() - converterSwitchLabelGap - converterSwitchHeight,
                               converterSwitchWidth, converterSwitchHeight);

    toneInner.removeFromTop(knobRowVerticalGap);
    auto toneRow2 = toneInner.removeFromTop(heroKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(toneRow2, heroKnobSize, colorSlider, colorLabel);

    // ---- Timing: Pre-Delay (regular, top row), Decay (hero-sized, bottom row) - same
    // two-row shape as Tone, one knob per row instead of two. ----
    auto timingInner = timingColumn;
    timingInner.removeFromTop(sectionPaddingTop);
    timingInner.removeFromLeft(sectionPaddingSide);
    timingInner.removeFromRight(sectionPaddingSide);
    timingInner.removeFromBottom(sectionPaddingBottom);

    auto timingRow1 = timingInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(timingRow1, defaultKnobSize, preDelaySlider, preDelayLabel);

    timingInner.removeFromTop(knobRowVerticalGap);
    auto timingRow2 = timingInner.removeFromTop(heroKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(timingRow2, heroKnobSize, decaySlider, decayLabel);

    // ---- Mix: two independent vertical faders. Unlike the rotary knobs above, the mockup's
    // fader-cell DOM order is name, THEN track, then value - name above is correct here. Faders
    // fill the section's FULL remaining height (approved mockup revision), not a fixed stub. ----
    auto mixInner = mixColumn;
    mixInner.removeFromTop(sectionPaddingTop);
    mixInner.removeFromLeft(sectionPaddingSide);
    mixInner.removeFromRight(sectionPaddingSide);
    mixInner.removeFromBottom(sectionPaddingBottom);

    auto mixLabelRow = mixInner.removeFromTop(fieldNameHeight);
    const auto faderCellWidth = (mixInner.getWidth() - faderGap) / 2;
    wetLabel.setBounds(mixLabelRow.removeFromLeft(faderCellWidth));
    mixLabelRow.removeFromLeft(faderGap);
    dryLabel.setBounds(mixLabelRow);

    // Reduced only a little horizontally - the visual track itself stays slim (drawLinearSlider
    // caps it at ~12px regardless of component width), but the built-in value textbox needs the
    // fuller width or "200.0%"-style values get silently ellipsized to "...".
    wetSlider.setBounds(mixInner.removeFromLeft(faderCellWidth).reduced(1, 4));
    mixInner.removeFromLeft(faderGap);
    drySlider.setBounds(mixInner.reduced(1, 4));

    rebuildChassisTexture();
}

AuraAudioProcessorEditor::AuraAudioProcessorEditor(AuraAudioProcessor& p)
    : AudioProcessorEditor(&p), content(p), zoomHandler(*this, content, {684, 529})
{
    addAndMakeVisible(content);
}
