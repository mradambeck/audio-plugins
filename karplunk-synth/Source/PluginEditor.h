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

    juce::Slider bowForceSlider;
    juce::Label bowForceLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bowForceAttachment;

    juce::ComboBox noiseColorBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noiseColorAttachment;

    juce::Slider structureSlider;
    juce::Label structureLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> structureAttachment;

    juce::Slider positionSlider;
    juce::Label positionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> positionAttachment;

    juce::ToggleButton monoButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttachment;

    juce::Slider waveshapeSlider;
    juce::Label waveshapeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> waveshapeAttachment;

    juce::ComboBox waveshaperTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveshaperTypeAttachment;

    juce::Slider ringModAmountSlider;
    juce::Label ringModAmountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ringModAmountAttachment;

    juce::Slider ringModFrequencySlider;
    juce::Label ringModFrequencyLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ringModFrequencyAttachment;

    juce::ComboBox topologyBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> topologyAttachment;

    juce::Slider crossCoupleSlider;
    juce::Label crossCoupleLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> crossCoupleAttachment;

    juce::Slider coupleDelaySlider;
    juce::Label coupleDelayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> coupleDelayAttachment;

    juce::Slider detuneSlider;
    juce::Label detuneLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> detuneAttachment;

    juce::ComboBox loopFilterTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loopFilterTypeAttachment;

    juce::Slider resonanceSlider;
    juce::Label resonanceLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resonanceAttachment;

    juce::Slider filterCutoffSlider;
    juce::Label filterCutoffLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterCutoffAttachment;

    juce::Slider filterEnvAmountSlider;
    juce::Label filterEnvAmountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterEnvAmountAttachment;

    juce::Slider filterAttackSlider;
    juce::Label filterAttackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterAttackAttachment;

    juce::Slider filterDecaySlider;
    juce::Label filterDecayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> filterDecayAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarplunkAudioProcessorEditor)
};
