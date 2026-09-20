#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "InhaltLookAndFeel.h"
#include "InhaltParameterMap.h"

#include "../../common/UI/ResizableZoom.h"

// All real painting/layout lives here, at a fixed native size (see the setSize() call in the
// constructor) that never changes again - see InhaltAudioProcessorEditor below for why, and
// common/UI/ResizableZoom.h for the resizable/zoom mechanism this split exists to support.
// Matches caverns-delay's own EditorContent/Editor split - see the juce-hardware-panel-ui skill.
class InhaltEditorContent : public juce::Component
{
public:
    explicit InhaltEditorContent(InhaltAudioProcessor&);
    ~InhaltEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupVerticalSlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);
    void updateGateLengthLabel();

    InhaltAudioProcessor& processorRef;

    InhaltLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> timingSectionBounds, toneSectionBounds, mixSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // TIMING section: Pre-Delay (regular, top row), Time (hero-sized, bottom row).
    juce::Slider preDelaySlider;
    juce::Label preDelayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;

    juce::Slider timeKnobSlider;
    juce::Label timeKnobLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> timeKnobAttachment;

    // Time's own secondary readout, below its built-in value textbox (the hardware's 0.1-9.8
    // label is not seconds - see PluginProcessor.h's timeKnobParamID comment). Re-synced from
    // timeKnobSlider's own onValueChange, not a timer - unlike Aura's ConverterSwitch this has no
    // other write path (host automation/preset load both still drive the slider itself, which
    // already fires onValueChange).
    juce::Label gateLengthLabel;

    // TONE section: Low Cut + Width (regular knobs, top row), High Frequency (hero-sized, own
    // row below).
    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;

    juce::Slider widthSlider;
    juce::Label widthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;

    juce::Slider highSlider;
    juce::Label highLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highAttachment;

    // MIX section: two independent vertical faders (not knobs), name above each track.
    juce::Slider wetSlider;
    juce::Label wetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;

    juce::Slider drySlider;
    juce::Label dryLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InhaltEditorContent)
};

// Thin shell around InhaltEditorContent: owns the plugin window's actual (resizable/zoomable)
// size. wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively
// resizable within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it.
// Always reopens at 100% (native size) - the resized size is deliberately not persisted.
class InhaltAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit InhaltAudioProcessorEditor(InhaltAudioProcessor&);

private:
    InhaltEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InhaltAudioProcessorEditor)
};
