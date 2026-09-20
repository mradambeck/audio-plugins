// Hardware-panel UI, built from the approved mockup (plugins/inhalt-nonlin/mockups/inhalt-mockup-v1.html,
// 2026-09-19) via the juce-hardware-panel-ui skill. rebuildChassisTexture(), drawHardwareSection(),
// and the chassis/panel/header/footer chrome in paint() are COPY-VERBATIM from
// plugins/caverns-delay/Source/PluginEditor.cpp (the skill's canonical reference) - they don't
// reference any per-plugin content. Everything else (constructor's control set/labels/window
// size, resized()'s column layout, drawHardwareSection() call sites) is Inhalt-specific, matching
// the mockup's TIMING/TONE/MIX three-section layout.

#include "PluginEditor.h"
#include "BinaryData.h"

// Lives here (not PluginProcessor.cpp) so PluginProcessor.cpp has no GUI dependency - InhaltTests
// links only PluginProcessor.cpp against juce_audio_processors/juce_dsp, no editor/LookAndFeel/fonts.
juce::AudioProcessorEditor* InhaltAudioProcessor::createEditor()
{
    return new InhaltAudioProcessorEditor(*this);
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

    // Content-driven, matching the mockup's own flex:0 0 auto (content-hugging) sections - see
    // Aura's own PluginEditor.cpp comment for why these are hand-picked constants rather than
    // proportional flex-grow columns. TIMING and TONE here have the exact same two-row shape
    // (regular knob row, then hero knob row) as Aura's own Timing/Tone sections, so their widths
    // start from Aura's own already-screenshot-verified values (150/250) rather than re-deriving
    // from scratch - confirmed against the real build below, not assumed.
    constexpr int timingColumnWidth = 150;
    constexpr int toneColumnWidth = 250;

    constexpr int sectionPaddingTop = 40;    // clearance for the badge straddling the top border
    constexpr int sectionPaddingSide = 10;   // matches the mockup's tightened hw-section padding
    constexpr int sectionPaddingBottom = 20;

    constexpr int defaultKnobSize = 88;
    constexpr int heroKnobSize = 112;         // Time/High read larger, per the approved mockup
    constexpr int knobRowVerticalGap = 16;    // vertical gap between a section's two knob rows
    constexpr int knobNameHeight = 28;        // gap between the knob and its value textbox, occupied
                                               // by the name label -- mockup DOM order is knob, name,
                                               // value (not name-above-knob like a typical plugin)
    constexpr int knobTextBoxHeight = 20;

    // Time's own extra third line (see PluginEditor.h's gateLengthLabel comment) - not part of
    // any other knob in this catalog, so no shared constant exists for it yet.
    constexpr int gateLengthLabelHeight = 14;

    constexpr int fieldNameHeight = 20;      // headroom for a fader's name label, which IS above
                                              // (Wet/Dry match the more usual above-fader
                                              // convention, opposite of the knob-name-below rule)
    constexpr int faderGap = 14;             // matches the mockup's tightened fader-row gap
}

void InhaltEditorContent::setupRotarySlider(juce::Slider& slider, juce::Label& label,
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

void InhaltEditorContent::setupVerticalSlider(juce::Slider& slider, juce::Label& label,
                                               const juce::String& labelText)
{
    slider.setLookAndFeel(&lookAndFeel);   // see setupRotarySlider() for why this is necessary
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, knobTextBoxHeight);
    addAndMakeVisible(slider);

    // Not attachToComponent() here, deliberately: the fader track itself is only ~13px wide, and
    // a Label attached that way is forced to match its owner's width. Positioned manually in
    // resized() instead, wide enough to fit the text, matching the mockup's fader-cell (label
    // centred over the whole cell, not the thin track).
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

InhaltEditorContent::InhaltEditorContent(InhaltAudioProcessor& p)
    : processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("INHALT", juce::dontSendNotification);
    // Bounds are set precisely to each font's own ascent in resized() for baseline alignment
    // against tagLabel, so topLeft here (not centred) is what makes that positioning land right.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffDB51DB));
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("NonLin Reverb").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8a7089));
    addAndMakeVisible(tagLabel);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, InhaltAudioProcessor::bypassParamID, bypassButton);

    setupRotarySlider(preDelaySlider, preDelayLabel, "Pre-Delay");
    setupRotarySlider(timeKnobSlider, timeKnobLabel, "Time");
    setupRotarySlider(lowCutSlider, lowCutLabel, "Low Cut");
    setupRotarySlider(widthSlider, widthLabel, "Width");
    setupRotarySlider(highSlider, highLabel, "High");
    setupVerticalSlider(wetSlider, wetLabel, "Wet");
    setupVerticalSlider(drySlider, dryLabel, "Dry");

    // Time's own secondary readout - see PluginEditor.h's gateLengthLabel comment. Smaller/dimmer
    // than the knob's own built-in value textbox, matching the mockup's .knob-subvalue.
    gateLengthLabel.setJustificationType(juce::Justification::centred);
    gateLengthLabel.setFont(lookAndFeel.getDisplayFont(8.5f).withExtraKerningFactor(0.02f));
    gateLengthLabel.setColour(juce::Label::textColourId, juce::Colour(0xff7f938f).withAlpha(0.62f));
    addAndMakeVisible(gateLengthLabel);
    updateGateLengthLabel();
    timeKnobSlider.onValueChange = [this] { updateGateLengthLabel(); };

    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::preDelayMsParamID, preDelaySlider);
    timeKnobAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::timeKnobParamID, timeKnobSlider);
    lowCutAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::lowCutHzParamID, lowCutSlider);
    widthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::widthParamID, widthSlider);
    highAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::highParamID, highSlider);
    wetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::wetParamID, wetSlider);
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, InhaltAudioProcessor::dryParamID, drySlider);

    setSize(684, 529);
}

InhaltEditorContent::~InhaltEditorContent()
{
    setLookAndFeel(nullptr);
}

void InhaltEditorContent::updateGateLengthLabel()
{
    const auto timeKnob = (float) timeKnobSlider.getValue();
    const auto gateLengthMs = InhaltParameterMap::gateLengthMsForDisplay(timeKnob);
    // juce::String(float, 0) does NOT mean "0 decimal places" (per that overload's own docs,
    // numberOfDecimalPlaces <= 0 means "use the default format", which still shows decimals,
    // e.g. "149.3") - juce::roundToInt() is what actually gives the mockup's clean "149ms gate".
    gateLengthLabel.setText(juce::String(juce::roundToInt(gateLengthMs)) + "ms gate", juce::dontSendNotification);
}

// COPY-VERBATIM (see file banner): procedural chassis grain, no image assets, no per-plugin
// parameters.
void InhaltEditorContent::rebuildChassisTexture()
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
void InhaltEditorContent::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void InhaltEditorContent::paint(juce::Graphics& g)
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
    drawHardwareSection(g, timingSectionBounds, "Timing");
    drawHardwareSection(g, toneSectionBounds, "Tone");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(juce::String::fromUTF8("INHALT \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(180.0f), juce::Justification::topLeft);

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void InhaltEditorContent::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    // Content-based width (LED + gap + text + padding), matching the mockup's .hw-toggle.
    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) InhaltLookAndFeel::buttonShadowMargin));

    // Baseline-align "INHALT" and the tag line (mockup: .brand{align-items:baseline}).
    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "INHALT") + 8;
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
    auto toneColumn = content.removeFromLeft(toneColumnWidth);
    content.removeFromLeft(columnGap);
    // Remainder, not a fixed constant (same convention as Aura's own mixColumn).
    auto mixColumn = content;

    timingSectionBounds = timingColumn.toFloat();
    toneSectionBounds = toneColumn.toFloat();
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

    // ---- Timing: Pre-Delay (regular, top row), Time (hero-sized, bottom row). Time's row
    // reserves extra height at the bottom for gateLengthLabel, below the knob's own built-in
    // value textbox. ----
    auto timingInner = timingColumn;
    timingInner.removeFromTop(sectionPaddingTop);
    timingInner.removeFromLeft(sectionPaddingSide);
    timingInner.removeFromRight(sectionPaddingSide);
    timingInner.removeFromBottom(sectionPaddingBottom);

    auto timingRow1 = timingInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(timingRow1, defaultKnobSize, preDelaySlider, preDelayLabel);

    timingInner.removeFromTop(knobRowVerticalGap);
    auto timingRow2 = timingInner.removeFromTop(heroKnobSize + knobNameHeight + knobTextBoxHeight + gateLengthLabelHeight);
    auto timeArea = timingRow2.removeFromTop(heroKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(timeArea, heroKnobSize, timeKnobSlider, timeKnobLabel);
    gateLengthLabel.setBounds(timeKnobSlider.getX(), timeArea.getBottom(), heroKnobSize, gateLengthLabelHeight);

    // ---- Tone: Low Cut + Width (regular knobs, top row), High (hero-sized, own row below). ----
    auto toneInner = toneColumn;
    toneInner.removeFromTop(sectionPaddingTop);
    toneInner.removeFromLeft(sectionPaddingSide);
    toneInner.removeFromRight(sectionPaddingSide);
    toneInner.removeFromBottom(sectionPaddingBottom);

    auto toneRow1 = toneInner.removeFromTop(defaultKnobSize + knobNameHeight + knobTextBoxHeight);
    const auto toneHalfWidth = toneRow1.getWidth() / 2;
    positionKnob(toneRow1.removeFromLeft(toneHalfWidth), defaultKnobSize, lowCutSlider, lowCutLabel);
    positionKnob(toneRow1, defaultKnobSize, widthSlider, widthLabel);

    toneInner.removeFromTop(knobRowVerticalGap);
    auto toneRow2 = toneInner.removeFromTop(heroKnobSize + knobNameHeight + knobTextBoxHeight);
    positionKnob(toneRow2, heroKnobSize, highSlider, highLabel);

    // ---- Mix: two independent vertical faders. Unlike the rotary knobs above, the mockup's
    // fader-cell DOM order is name, THEN track, then value - name above is correct here. Faders
    // fill the section's full remaining height. ----
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
    // fuller width or "150.0%"-style values get silently ellipsized to "...".
    wetSlider.setBounds(mixInner.removeFromLeft(faderCellWidth).reduced(1, 4));
    mixInner.removeFromLeft(faderGap);
    drySlider.setBounds(mixInner.reduced(1, 4));

    rebuildChassisTexture();
}

InhaltAudioProcessorEditor::InhaltAudioProcessorEditor(InhaltAudioProcessor& p)
    : AudioProcessorEditor(&p), content(p), zoomHandler(*this, content, {684, 529})
{
    addAndMakeVisible(content);
}
