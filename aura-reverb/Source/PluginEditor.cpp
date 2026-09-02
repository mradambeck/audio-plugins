#include "PluginEditor.h"

namespace
{
    void setupRotary(juce::Slider& slider, juce::Label& label, const juce::String& text, juce::Component& parent)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
        parent.addAndMakeVisible(slider);

        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        parent.addAndMakeVisible(label);
    }
}

AuraAudioProcessorEditor::AuraAudioProcessorEditor(AuraAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setupRotary(timeSlider, timeLabel, "Time", *this);
    setupRotary(lowSlider, lowLabel, "Low", *this);
    setupRotary(highSlider, highLabel, "High", *this);
    setupRotary(preDelaySlider, preDelayLabel, "Pre-Delay", *this);
    setupRotary(mixSlider, mixLabel, "Blend", *this);
    setupRotary(inputGainSlider, inputGainLabel, "Gain", *this);
    setupRotary(outputGainSlider, outputGainLabel, "Volume", *this);

    addAndMakeVisible(bypassButton);

    auto& apvts = processorRef.apvts;
    timeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::timeSecondsParamID, timeSlider);
    lowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::lowDbParamID, lowSlider);
    highAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::highDbParamID, highSlider);
    preDelayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::preDelayMsParamID, preDelaySlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::mixPercentParamID, mixSlider);
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::inputGainDbParamID, inputGainSlider);
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, AuraAudioProcessor::outputGainDbParamID, outputGainSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, AuraAudioProcessor::bypassParamID, bypassButton);

    setSize(560, 260);
}

AuraAudioProcessorEditor::~AuraAudioProcessorEditor() = default;

void AuraAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour{0xff2a2a2e});
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawFittedText("Aura - placeholder UI (see juce-hardware-panel-ui skill for the real one)",
        getLocalBounds().removeFromTop(30), juce::Justification::centred, 1);
}

void AuraAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().withTrimmedTop(30).reduced(10);
    const auto knobWidth = area.getWidth() / 7;

    auto layoutOne = [&](juce::Slider& slider, juce::Label& label)
    {
        auto column = area.removeFromLeft(knobWidth);
        label.setBounds(column.removeFromTop(20));
        slider.setBounds(column.reduced(4));
    };

    layoutOne(timeSlider, timeLabel);
    layoutOne(lowSlider, lowLabel);
    layoutOne(highSlider, highLabel);
    layoutOne(preDelaySlider, preDelayLabel);
    layoutOne(mixSlider, mixLabel);
    layoutOne(inputGainSlider, inputGainLabel);
    layoutOne(outputGainSlider, outputGainLabel);

    bypassButton.setBounds(getLocalBounds().removeFromBottom(30).removeFromRight(100).reduced(4));
}

juce::AudioProcessorEditor* AuraAudioProcessor::createEditor()
{
    return new AuraAudioProcessorEditor(*this);
}
