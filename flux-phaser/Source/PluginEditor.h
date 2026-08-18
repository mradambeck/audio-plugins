#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "FluxLookAndFeel.h"

class FluxAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FluxAudioProcessorEditor(FluxAudioProcessor&);
    ~FluxAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void commitRawRateParam(float hz);
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupShiftButton();
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);
    void drawStageList(juce::Graphics&, juce::Rectangle<float> bounds);

    FluxAudioProcessor& processorRef;

    FluxLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> lfoSectionBounds, stagesSectionBounds, colorSectionBounds, mixSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // ---- LFO: Rate (+ Sync/Division), Depth, Shape ----
    juce::ToggleButton syncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

    juce::ComboBox divisionCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;

    juce::Slider rateSlider;
    juce::Label rateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment;

    juce::Slider depthSlider;
    juce::Label depthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;

    juce::Slider shapeSlider;
    juce::Label shapeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shapeAttachment;

    // ---- Stages: a Shift button that cycles through stage counts, and a read-only list below
    // (hand-painted in drawStageList(), not one Component per row) showing each count with an
    // LED that lights up next to whichever is currently selected. ----
    static constexpr int numStageChoices = 7;
    juce::TextButton shiftButton;
    juce::Rectangle<int> stageListBounds;
    // Repaint the (small) stage list only when the selection actually changes, rather than on
    // every 30Hz timer tick regardless - checked in timerCallback() against the live parameter.
    int lastPaintedStageIndex = -1;

    // ---- Color: Offset, Feedback, Brightness, Grit ----
    juce::Slider offsetSlider;
    juce::Label offsetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> offsetAttachment;

    juce::Slider feedbackSlider;
    juce::Label feedbackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackAttachment;

    juce::Slider brightnessSlider;
    juce::Label brightnessLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> brightnessAttachment;

    juce::Slider gritSlider;
    juce::Label gritLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gritAttachment;

    // ---- Mix: Blend (hero-sized rotary crossfade, left = 100% dry, right = 100% wet) ----
    juce::Slider blendSlider;
    juce::Label blendLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blendAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluxAudioProcessorEditor)
};
