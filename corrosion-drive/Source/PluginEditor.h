#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CorrosionLookAndFeel.h"

class CorrosionAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit CorrosionAudioProcessorEditor(CorrosionAudioProcessor&);
    ~CorrosionAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    CorrosionAudioProcessor& processorRef;

    CorrosionLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> toneSectionBounds, rectSectionBounds, outputSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ComboBox presetCombo;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::Slider driveSlider;
    juce::Label driveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;

    juce::Slider toneSlider;
    juce::Label toneLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> toneAttachment;

    juce::Slider biasSlider;
    juce::Label biasLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> biasAttachment;

    juce::Slider characterSlider;
    juce::Label characterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> characterAttachment;

    juce::Slider rectBlendSlider;
    juce::Label rectBlendLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rectBlendAttachment;

    juce::Slider rectMixSlider;
    juce::Label rectMixLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rectMixAttachment;

    juce::Slider drySlider;
    juce::Label dryLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;

    juce::Slider compSlider;
    juce::Label compLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compAttachment;

    juce::Slider outputSlider;
    juce::Label outputLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CorrosionAudioProcessorEditor)
};
