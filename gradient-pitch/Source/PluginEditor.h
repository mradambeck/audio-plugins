#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "GradientLookAndFeel.h"

// Bundles a rotary knob with its own name label and parameter attachment - a small abstraction
// (not present in Caverns, which only has ~9 controls total) justified here by Gradient's much
// larger control count (~19 knobs across dual A/B units): without it, PluginEditor.h would need
// three separate member declarations per knob.
struct GradientRotaryKnob
{
    juce::Slider slider;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct GradientToggle
{
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

struct GradientCombo
{
    juce::ComboBox combo;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class GradientAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GradientAudioProcessorEditor(GradientAudioProcessor&);
    ~GradientAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupRotaryKnob(GradientRotaryKnob&, const juce::String& labelText, const juce::String& paramID);
    void setupToggle(GradientToggle&, const juce::String& labelText, const juce::String& paramID);
    void setupSpliceCombo(GradientCombo&, const juce::String& paramID);
    void setupSubdivisionCombo(GradientCombo&, const juce::String& paramID);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value).
    void positionKnob(juce::Rectangle<int> cell, int knobSize, GradientRotaryKnob&);

    GradientAudioProcessor& processorRef;

    GradientLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> modeSectionBounds, spliceSectionBounds;
    juce::Rectangle<float> pitchSectionBounds, delaySectionBounds, driftOutputSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    juce::ComboBox presetCombo;
    GradientToggle bypassToggle;

    // ---- MODE section (global, not per-unit) ----
    GradientToggle dualModeToggle, linkToggle, crossFeedbackToggle;
    GradientRotaryKnob widthKnob, linkPitchKnob, linkDelayKnob;

    // ---- SPLICE section ----
    GradientCombo spliceModeComboA, spliceModeComboB;
    juce::Label spliceUnitTagA, spliceUnitTagB;
    GradientRotaryKnob xfadeKnobA, xfadeKnobB;

    // ---- PITCH section ----
    juce::Label pitchUnitTagA, pitchUnitTagB;
    GradientRotaryKnob pitchKnobA, fineKnobA, pitchKnobB, fineKnobB;

    // ---- DELAY / REGEN section ----
    juce::Label delayUnitTagA, delayUnitTagB;
    GradientToggle delaySyncToggleA, delaySyncToggleB;
    GradientCombo delaySubdivisionComboA, delaySubdivisionComboB;
    GradientRotaryKnob delayKnobA, feedbackKnobA, delayKnobB, feedbackKnobB;

    // ---- DRIFT / OUTPUT section ----
    juce::Label driftOutputUnitTagA, driftOutputUnitTagB;
    GradientRotaryKnob driftKnobA, mixKnobA, outputKnobA, driftKnobB, mixKnobB, outputKnobB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GradientAudioProcessorEditor)
};
