#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AuraLookAndFeel.h"

#include "../../common/UI/ResizableZoom.h"

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
    // A small 3-position slide switch, modelled directly on a cropped reference photo of the
    // Roland RE-501 Chorus Echo's own "LEVEL" switch (Adam, 2026-09-03) - this whole panel
    // language's hardware reference - rather than continuing to invent a Converter treatment from
    // scratch. Purely local to Aura, not folded into the shared HardwarePanelLookAndFeel since no
    // other plugin in the catalog uses this control shape yet. Paints its own tick-value row
    // ("8   16   24BIT") directly above its own track, since those need to align exactly over the
    // track's own 3 segment centres - not something a separately-positioned Label could do
    // reliably. The name label ("CONVERTER") below is a normal external juce::Label instead
    // (AuraEditorContent::converterLabel), matching how every other control's name sits outside
    // its own paint().
    class ConverterSwitch : public juce::Component
    {
    public:
        explicit ConverterSwitch(AuraLookAndFeel& lookAndFeelIn) : lookAndFeel(lookAndFeelIn) {}

        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;

        void setValue(int newValue, juce::NotificationType notify);
        int getValue() const noexcept { return value; }

        // Fires on a user click, not on setValue()'s own programmatic (dontSendNotification) path
        // - mirrors juce::Slider's onValueChange/setValue(..., dontSendNotification) contract, so
        // the owner can safely re-sync from the live parameter without feedback looping.
        std::function<void(int)> onValueChanged;

    private:
        AuraLookAndFeel& lookAndFeel;
        int value = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConverterSwitch)
    };

    void timerCallback() override;
    void setupRotarySlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupVerticalSlider(juce::Slider&, juce::Label&, const juce::String& labelText);
    void setupConverterSwitch();
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

    // TONE section: Low Cut + Converter (regular knob + switch, top row), Color (hero-sized, own
    // row below).
    juce::Slider lowCutSlider;
    juce::Label lowCutLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowCutAttachment;

    // Converter: a 3-position ConverterSwitch (see above) plus its own name label below - there's
    // no Slider/ButtonAttachment shape that fits a hand-painted 3-position switch, so this is
    // wired manually: ConverterSwitch::onValueChanged pushes the clicked position straight into
    // the "bitDepth" parameter, and timerCallback() re-syncs the switch's own value from the live
    // parameter so host automation/preset loads are reflected too, not just clicks.
    ConverterSwitch converterSwitch { lookAndFeel };
    juce::Label converterLabel;
    int lastSyncedConverterIndex = -1;

    juce::Slider colorSlider;
    juce::Label colorLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> colorAttachment;

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
