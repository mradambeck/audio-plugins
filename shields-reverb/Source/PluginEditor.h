#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "ShieldsLookAndFeel.h"

// Hardware-panel UI (see .claude/skills/juce-hardware-panel-ui and
// common/LookAndFeel/MOCKUP_GROUND_TRUTH.md) built directly from the approved mockup at
// mockups/shields-mockup-v1.html. No factory presets exist yet (ShieldsAudioProcessor::getNumPrograms()
// returns 1), so unlike Caverns/Gradient there is no preset combo in the header.
class ShieldsAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ShieldsAudioProcessorEditor(ShieldsAudioProcessor&);
    ~ShieldsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    ShieldsAudioProcessor& processorRef;

    ShieldsLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> diffusionSectionBounds, decaySectionBounds, toneSectionBounds,
        motionSectionBounds, mixSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::Slider diffusionSlider;
    juce::Label diffusionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAttachment;

    juce::Slider sizeSlider;
    juce::Label sizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;

    juce::Slider feedbackSlider;
    juce::Label feedbackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackAttachment;

    juce::Slider dampingSlider;
    juce::Label dampingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;

    juce::Slider bandwidthSlider;
    juce::Label bandwidthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bandwidthAttachment;

    juce::Slider bitDepthSlider;
    juce::Label bitDepthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bitDepthAttachment;

    // Optional, off-by-default delay-line modulation (see ShieldsFDNEngine::setWobble()'s comment) -
    // its own single-knob section, positioned between Tone and Mix per the original build order's
    // final ("optional modulation") step.
    juce::Slider wobbleSlider;
    juce::Label wobbleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wobbleAttachment;

    // Independent Dry/Wet knobs (small, side by side - a fader pair was tried first to match
    // Caverns exactly, but read poorly at this section's height; small knobs matching the other
    // three sections' style read better here).
    juce::Slider drySlider;
    juce::Label dryLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;

    juce::Slider wetSlider;
    juce::Label wetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShieldsAudioProcessorEditor)
};
