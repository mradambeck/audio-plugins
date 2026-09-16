#include "ConvolutionEditor.h"
#include "ConvolutionVariantTheme.h"

namespace wildjag::conv
{

namespace
{
    constexpr int uiRefreshHz = 30;

    struct KnobSpec
    {
        const char* parameterID;
        const char* caption;
    };

    const std::array<KnobSpec, ConvolutionEditorContent::numKnobs> knobSpecs {{
        { ConvolutionProcessor::preDelayMsParamID,   "PRE-DELAY" },
        { ConvolutionProcessor::lengthPercentParamID, "LENGTH" },
        { ConvolutionProcessor::attackMsParamID,      "ATTACK" },
        { ConvolutionProcessor::lowCutHzParamID,      "LOW CUT" },
        { ConvolutionProcessor::highCutHzParamID,     "HIGH CUT" },
        { ConvolutionProcessor::dryParamID,           "DRY" },
        { ConvolutionProcessor::wetParamID,           "WET" },
    }};
}

ConvolutionEditorContent::ConvolutionEditorContent(ConvolutionProcessor& processorToUse)
    : processor(processorToUse), lookAndFeel(variantTheme())
{
    setLookAndFeel(&lookAndFeel);

    // Populated from the variant's IR table, in its declared order, so the dropdown's contents are
    // the one thing that differs between two builds of this same editor.
    const auto& irs = processor.getVariant().irs;
    for (int i = 0; i < (int) irs.size(); ++i)
        irSelector.addItem(irs[(size_t) i].displayName != nullptr ? irs[(size_t) i].displayName : "IR", i + 1);

    addAndMakeVisible(irSelector);
    irSelectorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, ConvolutionProcessor::irIndexParamID, irSelector);

    bypassButton.setButtonText("BYPASS");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, ConvolutionProcessor::bypassParamID, bypassButton);

    waveform.setColour(IRWaveformDisplay::backgroundColourId, juce::Colour { 0xff11161a });
    waveform.setColour(IRWaveformDisplay::sourceWaveformColourId, juce::Colour { 0xff2c3a42 });
    waveform.setColour(IRWaveformDisplay::shapedWaveformColourId, lookAndFeel.getAccentColour());
    waveform.setColour(IRWaveformDisplay::preDelayMarkerColourId, juce::Colour { 0xff7f938f });
    waveform.setColour(IRWaveformDisplay::gridColourId, juce::Colour { 0xff1e262c });
    addAndMakeVisible(waveform);

    for (int i = 0; i < numKnobs; ++i)
        configureKnob(knobs[(size_t) i], knobSpecs[(size_t) i].parameterID, knobSpecs[(size_t) i].caption);

    setSize(nativeWidth, nativeHeight);
    startTimerHz(uiRefreshHz);
}

ConvolutionEditorContent::~ConvolutionEditorContent()
{
    setLookAndFeel(nullptr);
}

void ConvolutionEditorContent::configureKnob(Knob& knob, const juce::String& parameterID, const juce::String& caption)
{
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 16);
    addAndMakeVisible(knob.slider);

    knob.caption.setText(caption, juce::dontSendNotification);
    knob.caption.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(knob.caption);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, parameterID, knob.slider);
}

void ConvolutionEditorContent::timerCallback()
{
    waveform.update(processor.getIRLoadWorker().getWaveformSnapshot(),
                    processor.apvts.getRawParameterValue(ConvolutionProcessor::preDelayMsParamID)->load());
}

void ConvolutionEditorContent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour { 0xff1b2226 });

    g.setColour(lookAndFeel.getAccentColour());
    g.setFont(lookAndFeel.getDisplayFont(22.0f));
    g.drawText(processor.getVariant().displayName != nullptr ? processor.getVariant().displayName : "Convolution",
               24, 16, 320, 28, juce::Justification::centredLeft);
}

void ConvolutionEditorContent::resized()
{
    auto bounds = getLocalBounds().reduced(24);

    auto header = bounds.removeFromTop(56);
    header.removeFromLeft(320); // the wordmark painted above
    bypassButton.setBounds(header.removeFromRight(110).reduced(0, 12));
    header.removeFromRight(16);
    irSelector.setBounds(header.removeFromRight(260).reduced(0, 14));

    bounds.removeFromTop(8);
    waveform.setBounds(bounds.removeFromTop(160));
    bounds.removeFromTop(20);

    const auto knobWidth = bounds.getWidth() / numKnobs;
    for (int i = 0; i < numKnobs; ++i)
    {
        auto column = bounds.removeFromLeft(knobWidth);
        knobs[(size_t) i].caption.setBounds(column.removeFromTop(18));
        knobs[(size_t) i].slider.setBounds(column.reduced(6, 0).removeFromTop(112));
    }
}

ConvolutionAudioProcessorEditor::ConvolutionAudioProcessorEditor(ConvolutionProcessor& processorToUse)
    : AudioProcessorEditor(&processorToUse),
      content(processorToUse),
      zoom(*this, content,
           { ConvolutionEditorContent::nativeWidth, ConvolutionEditorContent::nativeHeight })
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
