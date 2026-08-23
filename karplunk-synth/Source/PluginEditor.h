#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "KarplunkBuildNumber.h"
#include "KarplunkLookAndFeel.h"
#include "PluginProcessor.h"

// Minimal functional editor - plain rotary sliders bound to APVTS, no hardware-panel chassis/
// mockup pass yet (explicitly out of scope for this base scaffold; see README.md). Still applies
// KarplunkLookAndFeel so the sliders pick up the plugin's accent colour theme "for free" without
// custom paint() panel work.
class KarplunkAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit KarplunkAudioProcessorEditor(KarplunkAudioProcessor&);
    ~KarplunkAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupSlider(juce::Slider&, juce::Label&, const juce::String& labelText);

    KarplunkAudioProcessor& processorRef;
    KarplunkLookAndFeel lookAndFeel;

    juce::Label titleLabel;
    juce::Label buildNumberLabel;

    juce::Slider dampingSlider;
    juce::Label dampingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;

    juce::Slider outputLevelSlider;
    juce::Label outputLevelLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputLevelAttachment;

    juce::Slider brightnessSlider;
    juce::Label brightnessLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> brightnessAttachment;

    juce::Slider bowAmountSlider;
    juce::Label bowAmountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bowAmountAttachment;

    juce::Slider structureSlider;
    juce::Label structureLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> structureAttachment;

    juce::Slider positionSlider;
    juce::Label positionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> positionAttachment;

    juce::ToggleButton monoButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttachment;

    juce::Slider glideTimeSlider;
    juce::Label glideTimeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> glideTimeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarplunkAudioProcessorEditor)
};
