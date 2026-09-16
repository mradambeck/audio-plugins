#include "ConvolutionEditor.h"
#include "ConvolutionVariantTheme.h"

#include <cmath>

namespace wildjag::conv
{

namespace
{
    constexpr int chassisMargin = 15;
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 30;

    constexpr int contentPadding = 18;
    constexpr int columnGap = 26;
    constexpr int rowGap = 18;      // between IMPULSE and the SHAPE/FILTER row below it

    constexpr int sectionPaddingTop = 40;    // clearance for the badge inside the top border
    constexpr int sectionPaddingSide = 10;
    constexpr int sectionPaddingBottom = 20;

    constexpr int knobSize = 88;
    constexpr int knobGap = 26;
    // Reserved below each knob for its name label, then its built-in value textbox. The mockup's
    // CSS approximates this stack at ~32px; JUCE's real labels need more, which is why the section
    // heights below are computed from these rather than from the mockup's measured pixels.
    constexpr int knobNameHeight = 28;
    constexpr int knobTextBoxHeight = 20;
    constexpr int knobCellHeight = knobSize + knobNameHeight + knobTextBoxHeight;

    constexpr int comboHeight = 30;
    constexpr int irSelectorWidth = 330;
    constexpr int irMetaGap = 14;
    constexpr int waveformGap = 14;
    constexpr int waveformHeight = 112;

    constexpr int fieldNameHeight = 20;   // a fader's name label sits ABOVE it, unlike a knob's
    constexpr int faderGap = 14;

    // Section widths, content-driven exactly as the mockup's own content-hugging sections are.
    constexpr int shapeSectionWidth = 3 * knobSize + 2 * knobGap + 2 * sectionPaddingSide;   // 336
    constexpr int filterSectionWidth = 2 * knobSize + knobGap + 2 * sectionPaddingSide;      // 222
    constexpr int leftColumnWidth = shapeSectionWidth + columnGap + filterSectionWidth;      // 584

    // Wider than the mockup's measured 110px, deliberately - the same correction Aura's Mix column
    // needed. The mockup's example fader values never exercised Wet's real 0-200% range, and
    // "200.0%" needs meaningfully more textbox width than "40.0%" suggested.
    constexpr int mixSectionWidth = 130;

    constexpr int impulseSectionHeight = sectionPaddingTop + comboHeight + waveformGap
                                         + waveformHeight + sectionPaddingBottom;            // 216
    constexpr int knobSectionHeight = sectionPaddingTop + knobCellHeight + sectionPaddingBottom; // 196

    constexpr int panelContentHeight = impulseSectionHeight + rowGap + knobSectionHeight;     // 430

    constexpr int editorWidth = chassisMargin * 2 + contentPadding * 2
                                + leftColumnWidth + columnGap + mixSectionWidth;
    constexpr int editorHeight = chassisMargin * 2 + headerHeight + footerHeight
                                 + contentPadding * 2 + panelContentHeight;

    constexpr int uiRefreshHz = 30;

    struct KnobSpec { const char* parameterID; const char* caption; };

    const std::array<KnobSpec, 5> knobSpecs {{
        { ConvolutionProcessor::preDelayMsParamID,    "Pre-Delay" },
        { ConvolutionProcessor::lengthPercentParamID, "Length" },
        { ConvolutionProcessor::attackMsParamID,      "Attack" },
        { ConvolutionProcessor::lowCutHzParamID,      "Low Cut" },
        { ConvolutionProcessor::highCutHzParamID,     "High Cut" },
    }};
}

void ConvolutionEditorContent::setupRotarySlider(juce::Slider& slider, juce::Label& label,
                                                  const juce::String& labelText)
{
    // Slider is a member, so it default-constructs (and builds its internal value-textbox Label)
    // before this editor's setLookAndFeel() runs - at which point getLookAndFeel() still resolves
    // to JUCE's global default. ComboBox recovers on reparent; Slider never does, so it keeps the
    // default styling unless told explicitly here.
    slider.setLookAndFeel(&lookAndFeel);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, knobTextBoxHeight);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible(slider);

    // Deliberately not attachToComponent(): the mockup's .knob-cell DOM order is knob, then name,
    // then value - the name sits BELOW the knob, which attachToComponent cannot express. Placed by
    // hand in resized(), in the gap drawRotarySlider leaves for it.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void ConvolutionEditorContent::setupVerticalSlider(juce::Slider& slider, juce::Label& label,
                                                    const juce::String& labelText)
{
    slider.setLookAndFeel(&lookAndFeel);   // see setupRotarySlider() for why
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, knobTextBoxHeight);
    addAndMakeVisible(slider);

    // Not attachToComponent() either: the fader track is only ~13px wide and an attached label is
    // forced to its owner's width, which would ellipsize the text. Centred over the whole cell in
    // resized() instead, matching the mockup's .fader-cell.
    label.setText(labelText.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

ConvolutionEditorContent::ConvolutionEditorContent(ConvolutionProcessor& processorToUse)
    : processorRef(processorToUse), lookAndFeel(variantTheme())
{
    setLookAndFeel(&lookAndFeel);

    const auto productName = processorRef.getVariant().displayName != nullptr
                               ? juce::String(processorRef.getVariant().displayName)
                               : juce::String("Convolution");

    titleLabel.setText(productName.toUpperCase(), juce::dontSendNotification);
    // topLeft, not centred: resized() positions this from the font's own ascent so it shares a
    // baseline with tagLabel, which a centred justification would undo.
    titleLabel.setJustificationType(juce::Justification::topLeft);
    titleLabel.setFont(lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f));
    titleLabel.setColour(juce::Label::textColourId, variantTheme().accentBrightHi);
    addAndMakeVisible(titleLabel);

    tagLabel.setText(juce::String("Convolution Reverb").toUpperCase(), juce::dontSendNotification);
    tagLabel.setJustificationType(juce::Justification::topLeft);
    tagLabel.setFont(lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f));
    tagLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6f8280));
    addAndMakeVisible(tagLabel);

    bypassButton.setLookAndFeel(&lookAndFeel);
    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.apvts, ConvolutionProcessor::bypassParamID, bypassButton);

    // The dropdown's contents are the one thing that differs between two builds of this editor.
    irSelector.setLookAndFeel(&lookAndFeel);
    const auto& irs = processorRef.getVariant().irs;
    for (int i = 0; i < (int) irs.size(); ++i)
        irSelector.addItem(irs[(size_t) i].displayName != nullptr ? irs[(size_t) i].displayName : "IR", i + 1);
    addAndMakeVisible(irSelector);
    irSelectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processorRef.apvts, ConvolutionProcessor::irIndexParamID, irSelector);

    irMetaLabel.setJustificationType(juce::Justification::centredLeft);
    irMetaLabel.setFont(lookAndFeel.getDisplayFont(10.5f).withExtraKerningFactor(0.03f));
    irMetaLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8fa19e));
    addAndMakeVisible(irMetaLabel);

    waveform.setColour(IRWaveformDisplay::backgroundColourId, juce::Colour(0xff161b1d));
    waveform.setColour(IRWaveformDisplay::sourceWaveformColourId, juce::Colour(0xff2f3a44));
    waveform.setColour(IRWaveformDisplay::shapedWaveformColourId, variantTheme().accentBrightHi);
    waveform.setColour(IRWaveformDisplay::preDelayMarkerColourId, juce::Colour(0xff8fa19e));
    waveform.setColour(IRWaveformDisplay::gridColourId, juce::Colour(0xff39434b));
    waveform.setColour(IRWaveformDisplay::outlineColourId, juce::Colour(0xff3a4245));
    addAndMakeVisible(waveform);

    for (int i = 0; i < numKnobs; ++i)
    {
        setupRotarySlider(knobs[(size_t) i].slider, knobs[(size_t) i].name, knobSpecs[(size_t) i].caption);
        knobs[(size_t) i].attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processorRef.apvts, knobSpecs[(size_t) i].parameterID, knobs[(size_t) i].slider);
    }

    setupVerticalSlider(dryFader.slider, dryFader.name, "Dry");
    setupVerticalSlider(wetFader.slider, wetFader.name, "Wet");
    dryFader.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ConvolutionProcessor::dryParamID, dryFader.slider);
    wetFader.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, ConvolutionProcessor::wetParamID, wetFader.slider);

    setSize(editorWidth, editorHeight);
    startTimerHz(uiRefreshHz);
}

ConvolutionEditorContent::~ConvolutionEditorContent()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void ConvolutionEditorContent::timerCallback()
{
    refreshFromProcessor();
}

void ConvolutionEditorContent::refreshFromProcessor()
{
    auto snapshot = processorRef.getIRLoadWorker().getWaveformSnapshot();

    waveform.update(snapshot,
                    processorRef.apvts.getRawParameterValue(ConvolutionProcessor::preDelayMsParamID)->load());

    if (snapshot == lastSnapshot)
        return;

    lastSnapshot = snapshot;

    if (snapshot == nullptr)
    {
        irMetaLabel.setText({}, juce::dontSendNotification);
        return;
    }

    const auto channels = snapshot->sourceChannels > 1 ? "stereo" : "mono";
    const auto rateKHz = juce::String(snapshot->nativeSampleRate / 1000.0, 1);
    irMetaLabel.setText(juce::String(snapshot->sourceSeconds, 2) + " s  "
                            + juce::String::fromUTF8("\xC2\xB7") + "  " + channels + "  "
                            + juce::String::fromUTF8("\xC2\xB7") + "  " + rateKHz + " kHz",
                        juce::dontSendNotification);
}

// COPY-VERBATIM (see file banner): procedural chassis grain, no image assets, no per-plugin
// parameters.
void ConvolutionEditorContent::rebuildChassisTexture()
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
// inside the border rather than straddling it.
void ConvolutionEditorContent::drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds,
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

void ConvolutionEditorContent::paint(juce::Graphics& g)
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
    g.drawLine(deviceBounds.getX() + 14.0f, deviceBounds.getY() + 1.5f,
               deviceBounds.getRight() - 14.0f, deviceBounds.getY() + 1.5f, 1.5f);

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
        g.drawLine(fullPanelBounds.getX(), fullPanelBounds.getY() + 0.5f,
                   fullPanelBounds.getRight(), fullPanelBounds.getY() + 0.5f, 1.0f);

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
    drawHardwareSection(g, impulseSectionBounds, "Impulse");
    drawHardwareSection(g, shapeSectionBounds, "Shape");
    drawHardwareSection(g, filterSectionBounds, "Filter");
    drawHardwareSection(g, mixSectionBounds, "Mix");
    // --- END PLUGIN-SPECIFIC ---

    auto footerBoundsCopy = fullPanelBounds;
    auto footerArea = footerBoundsCopy.removeFromBottom((float) footerHeight).reduced(20.0f, 0.0f);

    const auto productName = processorRef.getVariant().displayName != nullptr
                               ? juce::String(processorRef.getVariant().displayName)
                               : juce::String("Convolution");

    g.setFont(lookAndFeel.getSmallPrintFont(9.5f).withExtraKerningFactor(0.14f));
    g.setColour(juce::Colour(0xff586566));
    g.drawText(productName.toUpperCase() + juce::String::fromUTF8(" \xC2\xB7 v") + JucePlugin_VersionString,
               footerArea.removeFromLeft(220.0f), juce::Justification::topLeft);

    g.setColour(juce::Colour(0xff3a4547));
    g.drawText(juce::String("Wild Jag").toUpperCase(), footerArea, juce::Justification::topRight);
}

void ConvolutionEditorContent::resized()
{
    auto panelArea = getLocalBounds().reduced(chassisMargin);

    auto header = panelArea.removeFromTop(headerHeight).reduced(22, 0);

    // Content-based width (LED + gap + text + padding), matching the mockup's .hw-toggle.
    const auto bypassFont = lookAndFeel.getDisplayFont(11.0f).withExtraKerningFactor(0.06f);
    const auto bypassTextWidth = juce::GlyphArrangement::getStringWidth(bypassFont, "BYPASS");
    const auto bypassWidth = (int) std::ceil(9.0f + 8.0f + bypassTextWidth + 24.0f);
    bypassButton.setBounds(header.removeFromRight(bypassWidth).withSizeKeepingCentre(bypassWidth, 28)
                                .expanded((int) wildjag::HardwarePanelLookAndFeel::buttonShadowMargin));

    // Baseline-align the wordmark and the tag line (mockup: .brand{align-items:baseline}).
    const auto titleFont = lookAndFeel.getDisplayFont(27.0f).withExtraKerningFactor(0.035f);
    const auto tagFont = lookAndFeel.getSmallPrintFont(11.0f).withExtraKerningFactor(0.26f);
    const auto titleWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, titleLabel.getText()) + 8;
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

    auto content = panelArea.reduced(contentPadding);

    // Two columns, not two rows: MIX is a sibling of the whole left column, so it spans the full
    // body height and its faders are full-length. IMPULSE therefore ends flush with FILTER's right
    // edge rather than running the panel's whole width.
    auto mixColumn = content.removeFromRight(mixSectionWidth);
    content.removeFromRight(columnGap);
    auto leftColumn = content;

    auto impulseSection = leftColumn.removeFromTop(impulseSectionHeight);
    leftColumn.removeFromTop(rowGap);
    auto shapeSection = leftColumn.removeFromLeft(shapeSectionWidth);
    leftColumn.removeFromLeft(columnGap);
    auto filterSection = leftColumn;

    impulseSectionBounds = impulseSection.toFloat();
    shapeSectionBounds = shapeSection.toFloat();
    filterSectionBounds = filterSection.toFloat();
    mixSectionBounds = mixColumn.toFloat();

    // ---- IMPULSE: the IR dropdown and its metadata, over the waveform. ----
    auto impulseInner = impulseSection;
    impulseInner.removeFromTop(sectionPaddingTop);
    impulseInner.removeFromBottom(sectionPaddingBottom);
    impulseInner = impulseInner.withTrimmedLeft(sectionPaddingSide).withTrimmedRight(sectionPaddingSide);

    auto comboRow = impulseInner.removeFromTop(comboHeight);
    irSelector.setBounds(comboRow.removeFromLeft(irSelectorWidth));
    comboRow.removeFromLeft(irMetaGap);
    irMetaLabel.setBounds(comboRow);

    impulseInner.removeFromTop(waveformGap);
    waveform.setBounds(impulseInner.removeFromTop(waveformHeight));

    // Positions a rotary knob and its name label together, matching the mockup's .knob-cell DOM
    // order: the slider's bounds span knob + name gap + its built-in value textbox, and
    // drawRotarySlider flush-tops the circle within that, leaving the gap for the name label.
    auto positionKnob = [](juce::Rectangle<int> cell, juce::Slider& slider, juce::Label& nameLabel)
    {
        auto knobBounds = cell.withSizeKeepingCentre(knobSize, knobCellHeight);
        slider.setBounds(knobBounds);
        nameLabel.setBounds(knobBounds.getX(), knobBounds.getY() + knobSize, knobSize, knobNameHeight);
    };

    auto layOutKnobRow = [&](juce::Rectangle<int> section, int firstKnob, int count)
    {
        auto inner = section;
        inner.removeFromTop(sectionPaddingTop);
        inner.removeFromBottom(sectionPaddingBottom);
        inner = inner.withTrimmedLeft(sectionPaddingSide).withTrimmedRight(sectionPaddingSide);

        for (int i = 0; i < count; ++i)
        {
            if (i > 0)
                inner.removeFromLeft(knobGap);

            auto& knob = knobs[(size_t) (firstKnob + i)];
            positionKnob(inner.removeFromLeft(knobSize), knob.slider, knob.name);
        }
    };

    layOutKnobRow(shapeSection, preDelay, 3);
    layOutKnobRow(filterSection, lowCut, 2);

    // ---- MIX: Dry and Wet faders, full height. ----
    auto mixInner = mixColumn;
    mixInner.removeFromTop(sectionPaddingTop);
    mixInner.removeFromBottom(sectionPaddingBottom);
    mixInner = mixInner.withTrimmedLeft(sectionPaddingSide).withTrimmedRight(sectionPaddingSide);

    auto positionFader = [](juce::Rectangle<int> cell, juce::Slider& slider, juce::Label& nameLabel)
    {
        // The fader's name sits ABOVE it, opposite the knob-name-below rule - matching the mockup's
        // .fader-cell, and the same split every other plugin in this catalog uses.
        nameLabel.setBounds(cell.removeFromTop(fieldNameHeight));
        slider.setBounds(cell);
    };

    const auto faderCellWidth = (mixInner.getWidth() - faderGap) / 2;
    positionFader(mixInner.removeFromLeft(faderCellWidth), dryFader.slider, dryFader.name);
    mixInner.removeFromLeft(faderGap);
    positionFader(mixInner, wetFader.slider, wetFader.name);

    rebuildChassisTexture();
}

ConvolutionAudioProcessorEditor::ConvolutionAudioProcessorEditor(ConvolutionProcessor& processorToUse)
    : AudioProcessorEditor(&processorToUse),
      content(processorToUse),
      zoom(*this, content, { content.getWidth(), content.getHeight() })
{
    addAndMakeVisible(content);
}

} // namespace wildjag::conv

// Defined here rather than in ConvolutionProcessor.cpp on purpose: it is the only thing tying the
// processor to the editor, so keeping it in this translation unit lets headless targets link the
// real processor against a stub of this one function. See Source/Tests/TestCreateEditorStub.cpp.
juce::AudioProcessorEditor* wildjag::conv::ConvolutionProcessor::createEditor()
{
    return new wildjag::conv::ConvolutionAudioProcessorEditor(*this);
}
