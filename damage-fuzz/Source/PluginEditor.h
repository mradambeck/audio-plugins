#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "DamageLookAndFeel.h"

#include "../../common/UI/ResizableZoom.h"

// All real painting/layout lives here, at a fixed native size (see the setSize() call in the
// constructor) that never changes again - see DamageAudioProcessorEditor below for why, and
// common/UI/ResizableZoom.h for the resizable/zoom mechanism this split exists to support.
class DamageEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit DamageEditorContent(DamageAudioProcessor&);
    ~DamageEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupVerticalSlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    DamageAudioProcessor& processorRef;

    DamageLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> gainSectionBounds, characterSectionBounds, mixSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ComboBox presetCombo;

    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // ---- Gain: Drive (+ Boost), Gate (+ Slow) ----
    juce::Slider driveSlider;
    juce::Label driveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;

    juce::ToggleButton boostButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> boostAttachment;

    LevelMeterSlider gateSlider;
    juce::Label gateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateAttachment;

    juce::ToggleButton slowButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> slowAttachment;

    // ---- Character: Hi Pass + Lo Pass on row 1, Pulse Width + FM Freq on row 2 (+ On) ----
    juce::Slider hiPassSlider;
    juce::Label hiPassLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hiPassAttachment;

    juce::Slider loPassSlider;
    juce::Label loPassLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loPassAttachment;

    juce::Slider widthSlider;
    juce::Label widthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment;

    juce::Slider oscFreqSlider;
    juce::Label oscFreqLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> oscFreqAttachment;

    juce::ToggleButton onButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    // ---- Mix: Dry, Wet (vertical faders) ----
    juce::Slider drySlider;
    juce::Label dryLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryAttachment;

    juce::Slider wetSlider;
    juce::Label wetLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamageEditorContent)
};

// Thin shell around DamageEditorContent: owns the plugin window's actual (resizable/zoomable) size.
// wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively resizable
// within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it - corner-grip /
// window-edge drag, with no custom zoom UI drawn by the plugin itself. Always reopens at 100% (native
// size) - the resized size is deliberately not persisted.
class DamageAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DamageAudioProcessorEditor(DamageAudioProcessor&);

private:
    DamageEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamageAudioProcessorEditor)
};
