#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// Placeholder editor - plain JUCE sliders, NOT the hardware-panel treatment. Real UI work follows
// the juce-hardware-panel-ui skill's mockup-first + screenshot-diff process (a separate step);
// this exists only so the plugin target builds and is playable/automatable in a DAW meanwhile.
class AuraAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AuraAudioProcessorEditor(AuraAudioProcessor&);
    ~AuraAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AuraAudioProcessor& processorRef;

    juce::Slider timeSlider, lowSlider, highSlider, preDelaySlider, bitDepthSlider, mixSlider, inputGainSlider, outputGainSlider;
    juce::Label timeLabel, lowLabel, highLabel, preDelayLabel, bitDepthLabel, mixLabel, inputGainLabel, outputGainLabel;
    juce::ToggleButton bypassButton { "Bypass" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment, lowAttachment,
        highAttachment, preDelayAttachment, bitDepthAttachment, mixAttachment, inputGainAttachment, outputGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraAudioProcessorEditor)
};
