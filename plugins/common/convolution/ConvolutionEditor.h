#pragma once

#include "ConvolutionProcessor.h"
#include "IRWaveformDisplay.h"

#include "../LookAndFeel/HardwarePanelLookAndFeel.h"
#include "../UI/ResizableZoom.h"

#include <array>
#include <memory>

// The shared editor. Like the processor, it is not subclassed per variant: the name it paints and
// the colours it paints in both come from the variant contract (variantConfig() / variantTheme()),
// so rebranding changes no code here.
//
// NOTE: the layout below is functional scaffolding, not the finished panel. The hardware-panel
// chrome (chassis texture, section badges, wordmark) comes from the juce-hardware-panel-ui skill's
// mockup-first process - HTML mockup, user approval, then C++, then a pixel diff. Until that has
// happened this is a plain arrangement of real, correctly-wired controls.
namespace wildjag::conv
{

class ConvolutionEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit ConvolutionEditorContent(ConvolutionProcessor& processorToUse);
    ~ConvolutionEditorContent() override;

    // The panel's native size, set once here and never changed - ResizableZoomHandler scales it.
    static constexpr int nativeWidth = 780;
    static constexpr int nativeHeight = 470;

    // Pre-Delay, Length, Attack, Low Cut, High Cut, Dry, Wet. No output gain: Wet reaches 200%, so
    // a master volume would only be a third place to lose level. Public because the knob table in
    // ConvolutionEditor.cpp is sized from it at file scope.
    static constexpr int numKnobs = 7;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void configureKnob(Knob& knob, const juce::String& parameterID, const juce::String& caption);

    ConvolutionProcessor& processor;
    wildjag::HardwarePanelLookAndFeel lookAndFeel;

    juce::ComboBox irSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> irSelectorAttachment;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    IRWaveformDisplay waveform;
    std::array<Knob, numKnobs> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionEditorContent)
};

class ConvolutionAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ConvolutionAudioProcessorEditor(ConvolutionProcessor& processorToUse);

    void resized() override {}

private:
    ConvolutionEditorContent content;
    wildjag::ResizableZoomHandler zoom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionAudioProcessorEditor)
};

} // namespace wildjag::conv
