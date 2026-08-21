#include "PluginEditor.h"

namespace
{
    constexpr int editorWidth = 420;
    constexpr int editorHeight = 260;
    constexpr int sliderSize = 100;
    constexpr int labelHeight = 20;
}

KarplunkAudioProcessorEditor::KarplunkAudioProcessorEditor(KarplunkAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("KARPLUNK", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    setupSlider(dampingSlider, dampingLabel, "Decay");
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::dampingParamID, dampingSlider);

    setupSlider(outputLevelSlider, outputLevelLabel, "Output Level");
    outputLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::outputLevelParamID, outputLevelSlider);

    setupSlider(brightnessSlider, brightnessLabel, "Pluck Brightness");
    brightnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, KarplunkAudioProcessor::brightnessParamID, brightnessSlider);

    setSize(editorWidth, editorHeight);
}

KarplunkAudioProcessorEditor::~KarplunkAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void KarplunkAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                                const juce::String& labelText)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, sliderSize, labelHeight);
    addAndMakeVisible(slider);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void KarplunkAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour{0xff1a1a1a});
}

void KarplunkAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(16);

    titleLabel.setBounds(bounds.removeFromTop(32));
    bounds.removeFromTop(12);

    const auto sliderCount = 3;
    const auto columnWidth = bounds.getWidth() / sliderCount;

    auto layoutColumn = [&](juce::Rectangle<int> columnBounds, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds(columnBounds.removeFromTop(labelHeight));
        slider.setBounds(columnBounds.withSizeKeepingCentre(sliderSize, sliderSize + labelHeight));
    };

    layoutColumn(bounds.removeFromLeft(columnWidth), dampingLabel, dampingSlider);
    layoutColumn(bounds.removeFromLeft(columnWidth), outputLevelLabel, outputLevelSlider);
    layoutColumn(bounds, brightnessLabel, brightnessSlider);
}
