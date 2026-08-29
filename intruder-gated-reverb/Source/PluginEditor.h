#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "IntruderLookAndFeel.h"

// Hardware-panel styled editor, following the juce-hardware-panel-ui skill / Caverns' canonical
// PluginEditor.cpp pattern. Three sections (Levels' 3-across small row is a later addition, not in
// the original approved mockup at mockups/intruder-mockup-v1.html - see Threshold's comment):
// Timing (Decay, Pre-Delay, stacked), Character (Low/High, Smoothing, stacked), Levels (Gain,
// Volume, Threshold as a small trio, Blend as a larger "hero" knob below). No preset combo -
// matches shields-reverb's precedent (getNumPrograms() == 1 here too).
//
// "Smoothing" is the UI label for what's still internally the diffusionSlider/tighterParamID
// (renamed from "Diffusion" 2026-08-29 - Adam's naming call, not a re-scope) - member/variable
// names weren't renamed to match, same precedent as diffusionSlider already not matching
// tighterParamID underneath it.
class IntruderAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit IntruderAudioProcessorEditor(IntruderAudioProcessor&);
    ~IntruderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    IntruderAudioProcessor& processorRef;

    IntruderLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> timingSectionBounds, characterSectionBounds, levelsSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::Slider decaySlider;
    juce::Label decayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;

    juce::Slider preDelaySlider;
    juce::Label preDelayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;

    juce::Slider lowHighSlider;
    juce::Label lowHighLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowHighAttachment;

    juce::Slider diffusionSlider;
    juce::Label diffusionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAttachment;

    juce::Slider gainSlider;
    juce::Label gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    juce::Slider volumeSlider;
    juce::Label volumeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;

    juce::Slider thresholdSlider;
    juce::Label thresholdLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;

    juce::Slider blendSlider;
    juce::Label blendLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntruderAudioProcessorEditor)
};
