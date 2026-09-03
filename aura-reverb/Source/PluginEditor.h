#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AuraLookAndFeel.h"

#include "../../common/UI/ResizableZoom.h"

#include <array>

// All real painting/layout lives here, at a fixed native size (see the setSize() call in the
// constructor) that never changes again - see AuraAudioProcessorEditor below for why, and
// common/UI/ResizableZoom.h for the resizable/zoom mechanism this split exists to support.
// Matches caverns-delay's own EditorContent/Editor split - see the juce-hardware-panel-ui skill.
class AuraEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit AuraEditorContent(AuraAudioProcessor&);
    ~AuraEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupVerticalSlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupConverterButtons();
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    AuraAudioProcessor& processorRef;

    AuraLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> toneSectionBounds, timingSectionBounds, mixSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // TONE section: Low Cut (regular, own row), Color (hero-sized, own row), Converter (own row -
    // a name label matching Low Cut's/Color's own, plus 3 mutually-exclusive LED buttons).
    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;

    juce::Slider colorSlider;
    juce::Label colorLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> colorAttachment;

    // Converter: one real juce::ToggleButton per choice (8/16/24 bit) - same flat-pushbutton +
    // LED chrome as bypassButton (HardwarePanelLookAndFeel::drawToggleButton), radio-grouped so
    // clicking one turns the other two off, matching the "only one can be selected at a time"
    // requirement directly rather than hand-painting a selection indicator. There's no
    // ButtonAttachment for "N buttons standing for one AudioParameterChoice", so this is wired
    // manually: each button's onClick pushes its own index straight into the bitDepth parameter,
    // and timerCallback() re-syncs all three buttons' toggle states from the live parameter value
    // so host automation/preset loads are reflected too, not just clicks.
    juce::Label converterLabel;
    static constexpr int numConverterChoices = 3;
    static constexpr int converterRadioGroupId = 1;
    std::array<juce::ToggleButton, numConverterChoices> converterButtons;
    int lastSyncedConverterIndex = -1;

    // TIMING section: Pre-Delay (regular, top row), Decay (hero-sized, bottom row).
    juce::Slider preDelaySlider;
    juce::Label preDelayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> preDelayAttachment;

    juce::Slider decaySlider;
    juce::Label decayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> decayAttachment;

    // MIX section: two independent vertical faders (not knobs), name above each track.
    juce::Slider wetSlider;
    juce::Label wetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;

    juce::Slider drySlider;
    juce::Label dryLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraEditorContent)
};

// Thin shell around AuraEditorContent: owns the plugin window's actual (resizable/zoomable) size.
// wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively resizable
// within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it. Always
// reopens at 100% (native size) - the resized size is deliberately not persisted.
class AuraAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AuraAudioProcessorEditor(AuraAudioProcessor&);

private:
    AuraEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraAudioProcessorEditor)
};
