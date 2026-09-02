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

    juce::Slider timeSlider, lowCutSlider, highSlider, preDelaySlider, bitDepthSlider, inputGainSlider, drySlider, wetSlider;
    juce::Label timeLabel, lowCutLabel, highLabel, preDelayLabel, bitDepthLabel, inputGainLabel, dryLabel, wetLabel;
    juce::ToggleButton bypassButton { "Bypass" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeAttachment, lowCutAttachment,
        highAttachment, preDelayAttachment, bitDepthAttachment, inputGainAttachment, dryAttachment, wetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraAudioProcessorEditor)
};
