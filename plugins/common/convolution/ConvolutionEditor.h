#pragma once

#include "ConvolutionProcessor.h"
#include "IRWaveformDisplay.h"

#include "../LookAndFeel/HardwarePanelLookAndFeel.h"
#include "../UI/ResizableZoom.h"

#include <array>
#include <memory>

// Hardware-panel UI for the shared convolution reverb, built from the approved mockup
// (plugins/convolution-base/mockups/convolution-mockup-v1.html) via the juce-hardware-panel-ui
// skill. rebuildChassisTexture(), drawHardwareSection(), and the chassis/panel/header/footer chrome
// in paint() are COPY-VERBATIM from plugins/aura-reverb/Source/PluginEditor.cpp (itself verbatim
// from Caverns, the skill's canonical reference) - none of it references per-plugin content.
//
// Unlike every other plugin's editor this one is NOT per-plugin: it is shared by every convolution
// variant, and reads the two things that differ - the product name and the accent colours - from
// variantConfig() and variantTheme(). There is no per-variant editor or LookAndFeel subclass.
namespace wildjag::conv
{

class ConvolutionEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit ConvolutionEditorContent(ConvolutionProcessor& processorToUse);
    ~ConvolutionEditorContent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Pulls the latest waveform snapshot and IR metadata from the processor. Normally driven by
    // this component's own timer; public so the offline renderer (Source/Tools/RenderUI.cpp) can
    // bring the display up to date deterministically instead of waiting on a timer to fire.
    void refreshFromProcessor();

private:
    void timerCallback() override;
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& label);
    void setupRotarySlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText);
    void setupVerticalSlider(juce::Slider& slider, juce::Label& label, const juce::String& labelText);

    // Pre-Delay, Length, Attack in SHAPE; Low Cut, High Cut in FILTER. No output gain: Wet reaches
    // 200%, so a master volume would only be a third place to lose level.
    enum Knob { preDelay, length, attack, lowCut, highCut, numKnobs };

    struct KnobControl
    {
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct FaderControl
    {
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ConvolutionProcessor& processorRef;
    wildjag::HardwarePanelLookAndFeel lookAndFeel;

    juce::Label titleLabel, tagLabel;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::ComboBox irSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> irSelectorAttachment;

    // "2.41 s - stereo - 44.1 kHz". Says whether the selected IR is being resampled, which is
    // otherwise invisible once IRLibrary has done its work.
    juce::Label irMetaLabel;

    IRWaveformDisplay waveform;

    std::array<KnobControl, numKnobs> knobs;
    FaderControl dryFader, wetFader;

    juce::Image chassisTexture;
    juce::Rectangle<float> impulseSectionBounds, shapeSectionBounds, filterSectionBounds, mixSectionBounds;

    // Guards the 30 Hz repaint: the waveform only changes when the worker publishes a new snapshot.
    std::shared_ptr<const WaveformSnapshot> lastSnapshot;

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
