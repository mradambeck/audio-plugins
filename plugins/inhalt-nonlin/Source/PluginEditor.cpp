#include "PluginEditor.h"
#include "InhaltParameterMap.h"

namespace
{
    constexpr int nativeWidth = 420;
    constexpr int nativeHeight = 360;
}

void InhaltEditorContent::setUpKnobRow(KnobRow& row, const juce::String& paramID, const juce::String& labelText)
{
    row.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    row.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    addAndMakeVisible(row.slider);

    row.label.setText(labelText, juce::dontSendNotification);
    row.label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(row.label);

    row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, row.slider);
}

InhaltEditorContent::InhaltEditorContent(InhaltAudioProcessor& processorToUse)
    : processor(processorToUse)
{
    setUpKnobRow(timeKnobRow, InhaltAudioProcessor::timeKnobParamID, "Time");
    setUpKnobRow(highRow, InhaltAudioProcessor::highParamID, "High Frequency");
    setUpKnobRow(preDelayRow, InhaltAudioProcessor::preDelayMsParamID, "Pre-Delay");
    setUpKnobRow(lowCutRow, InhaltAudioProcessor::lowCutHzParamID, "Low Cut");
    setUpKnobRow(widthRow, InhaltAudioProcessor::widthParamID, "Width");
    setUpKnobRow(dryRow, InhaltAudioProcessor::dryParamID, "Dry");
    setUpKnobRow(wetRow, InhaltAudioProcessor::wetParamID, "Wet");

    converterBox.addItem("Vintage", 1);
    converterBox.addItem("Modern", 2);
    addAndMakeVisible(converterBox);
    converterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, InhaltAudioProcessor::converterParamID, converterBox);

    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, InhaltAudioProcessor::bypassParamID, bypassButton);

    gateLengthLabel.setJustificationType(juce::Justification::centred);
    gateLengthLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(gateLengthLabel);
    updateGateLengthLabel();
    timeKnobRow.slider.onValueChange = [this] { updateGateLengthLabel(); };

    setSize(nativeWidth, nativeHeight);
}

void InhaltEditorContent::updateGateLengthLabel()
{
    const auto timeKnob = (float) timeKnobRow.slider.getValue();
    const auto gateLengthMs = InhaltParameterMap::gateLengthMsForDisplay(timeKnob);
    gateLengthLabel.setText(juce::String(gateLengthMs, 0) + " ms gate", juce::dontSendNotification);
}

void InhaltEditorContent::resized()
{
    auto bounds = getLocalBounds().reduced(12);

    auto topRow = bounds.removeFromTop(140);
    const auto knobWidth = topRow.getWidth() / 2;
    auto timeArea = topRow.removeFromLeft(knobWidth);
    timeKnobRow.slider.setBounds(timeArea.removeFromTop(90));
    timeKnobRow.label.setBounds(timeArea.removeFromTop(18));
    gateLengthLabel.setBounds(timeArea);
    auto highArea = topRow;
    highRow.slider.setBounds(highArea.removeFromTop(90));
    highRow.label.setBounds(highArea);

    bounds.removeFromTop(8);
    auto midRow = bounds.removeFromTop(110);
    const auto midWidth = midRow.getWidth() / 3;
    for (auto* row : { &preDelayRow, &lowCutRow, &widthRow })
    {
        auto area = midRow.removeFromLeft(midWidth);
        row->slider.setBounds(area.removeFromTop(80));
        row->label.setBounds(area);
    }

    bounds.removeFromTop(8);
    auto mixRow = bounds.removeFromTop(110);
    const auto mixWidth = mixRow.getWidth() / 2;
    for (auto* row : { &dryRow, &wetRow })
    {
        auto area = mixRow.removeFromLeft(mixWidth);
        row->slider.setBounds(area.removeFromTop(80));
        row->label.setBounds(area);
    }

    auto bottomRow = bounds.removeFromTop(28);
    converterBox.setBounds(bottomRow.removeFromLeft(120));
    bypassButton.setBounds(bottomRow.removeFromRight(90));
}

InhaltAudioProcessorEditor::InhaltAudioProcessorEditor(InhaltAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), content(p),
      zoomHandler(*this, content, { nativeWidth, nativeHeight })
{
    addAndMakeVisible(content);
    setSize(nativeWidth, nativeHeight);
}

void InhaltAudioProcessorEditor::resized()
{
    content.setTransform(juce::AffineTransform::scale(
        (float) getWidth() / (float) nativeWidth, (float) getHeight() / (float) nativeHeight));
}

juce::AudioProcessorEditor* InhaltAudioProcessor::createEditor()
{
    return new InhaltAudioProcessorEditor(*this);
}
