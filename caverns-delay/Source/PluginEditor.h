#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CavernsLookAndFeel.h"

#include "../../common/UI/ResizableZoom.h"

// All real painting/layout lives here, at a fixed native size (see the setSize() call in the
// constructor) that never changes again - see CavernsAudioProcessorEditor below for why, and
// common/UI/ResizableZoom.h for the resizable/zoom mechanism this split exists to support.
class CavernsEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit CavernsEditorContent(CavernsAudioProcessor&);
    ~CavernsEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void commitRawTimeParam(const juce::String& paramID, float ms);
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupVerticalSlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    CavernsAudioProcessor& processorRef;

    CavernsLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> timingSectionBounds, characterSectionBounds, modulationSectionBounds, mixSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ComboBox presetCombo;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    juce::ToggleButton syncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

    juce::ToggleButton linkButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttachment;

    juce::Label leftDivisionLabel;
    juce::ComboBox leftDivisionCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> leftDivisionAttachment;

    juce::Label rightDivisionLabel;
    juce::ComboBox rightDivisionCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rightDivisionAttachment;

    juce::Slider leftTimeSlider;
    juce::Label leftTimeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> leftTimeAttachment;

    juce::Slider rightTimeSlider;
    juce::Label rightTimeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rightTimeAttachment;

    juce::Slider feedbackSlider;
    juce::Label feedbackLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackAttachment;

    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;

    juce::Slider highCutSlider;
    juce::Label highCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highCutAttachment;

    juce::Slider degradeSlider;
    juce::Label degradeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> degradeAttachment;

    juce::Slider modSpeedSlider;
    juce::Label modSpeedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modSpeedAttachment;

    juce::Slider modDepthSlider;
    juce::Label modDepthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> modDepthAttachment;

    juce::Slider drySlider;
    juce::Label dryLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;

    juce::Slider wetSlider;
    juce::Label wetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CavernsEditorContent)
};

// Thin shell around CavernsEditorContent: owns the plugin window's actual (resizable/zoomable) size.
// wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively resizable
// within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it - corner-grip /
// window-edge drag, with no custom zoom UI drawn by the plugin itself. Always reopens at 100% (native
// size) - the resized size is deliberately not persisted.
class CavernsAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit CavernsAudioProcessorEditor(CavernsAudioProcessor&);

private:
    CavernsEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CavernsAudioProcessorEditor)
};
