#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "IntruderLookAndFeel.h"

#include "../../common/UI/ResizableZoom.h"

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
//
// All real painting/layout lives here, at a fixed native size (see the setSize() call in the
// constructor) that never changes again - see IntruderAudioProcessorEditor below for why, and
// common/UI/ResizableZoom.h for the resizable/zoom mechanism this split exists to support.
class IntruderEditorContent : public juce::Component
{
public:
    explicit IntruderEditorContent(IntruderAudioProcessor&);
    ~IntruderEditorContent() override;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntruderEditorContent)
};

// Thin shell around IntruderEditorContent: owns the plugin window's actual (resizable/zoomable)
// size. wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively
// resizable within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it -
// corner-grip / window-edge drag, with no custom zoom UI drawn by the plugin itself. Always reopens
// at 100% (native size) - the resized size is deliberately not persisted.
class IntruderAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit IntruderAudioProcessorEditor(IntruderAudioProcessor&);

private:
    IntruderEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntruderAudioProcessorEditor)
};
