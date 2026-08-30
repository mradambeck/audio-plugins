#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "StrikeLookAndFeel.h"
#include "PluginProcessor.h"

// Bundles a rotary knob with its own name label and parameter attachment, matching Gradient's
// GradientRotaryKnob precedent - justified here by Strike's own large control count (18 knobs).
struct StrikeRotaryKnob
{
    juce::Slider slider;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct StrikeToggle
{
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// No name label - every combo in Strike's hardware-panel design (Noise Color, Waveshaper Type)
// is either self-evident from its own text or paired directly beneath a knob that already has a
// name label, per the approved mockup (mockups/strike-mockup.html).
struct StrikeCombo
{
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Hardware-panel chassis/section editor, matching the approved mockup at
// mockups/strike-mockup.html pixel-for-pixel (see mockups/strike-mockup-reference.png) -
// replaces the plain-grid base-scaffold editor. Layout/structure follows Gradient's own reference
// implementation (Source/PluginEditor.h/.cpp) - see that file's header comment for the
// COPY-VERBATIM/PLUGIN-SPECIFIC split this file follows.
class StrikeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit StrikeAudioProcessorEditor(StrikeAudioProcessor&);
    ~StrikeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupRotaryKnob(StrikeRotaryKnob&, const juce::String& labelText, const juce::String& paramID);
    void setupToggle(StrikeToggle&, const juce::String& buttonText, const juce::String& paramID);
    void setupCombo(StrikeCombo&, const juce::StringArray& items, const juce::String& paramID);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value).
    void positionKnob(juce::Rectangle<int> cell, int knobSize, StrikeRotaryKnob&);

    // Sizes and positions a toggle button from its own button text, matching drawToggleButton's
    // content layout (LED + gap + text) plus outer padding - so a button's width always matches
    // what it actually draws rather than a hand-guessed constant.
    void positionToggle(juce::Rectangle<int> cell, StrikeToggle&);

    StrikeAudioProcessor& processorRef;
    StrikeLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> strumSectionBounds, colorSectionBounds, outputSectionBounds;
    juce::Rectangle<float> ringModSectionBounds, crossCoupleSectionBounds, filterSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    StrikeToggle monoToggle;

    // ---- STRUM section ----
    StrikeRotaryKnob decayKnob, brightnessKnob, bowAmountKnob, bowForceKnob;
    StrikeCombo noiseColorCombo;

    // ---- COLOR section ----
    StrikeToggle postFilterToggle;
    StrikeRotaryKnob structureKnob, stringPositionKnob, waveshapeKnob;
    StrikeCombo waveshaperTypeCombo;

    // ---- OUTPUT section ----
    StrikeRotaryKnob volumeKnob;

    // ---- RING MOD section ----
    StrikeRotaryKnob ringModAmountKnob, ringModFrequencyKnob;

    // ---- CROSS COUPLE section ----
    StrikeToggle crossCoupleOnToggle;
    StrikeRotaryKnob crossCoupleAmountKnob, coupleDelayKnob, detuneKnob;

    // ---- FILTER section ----
    StrikeToggle filterOnToggle;
    StrikeRotaryKnob filterCutoffKnob, resonanceKnob, filterAttackKnob, filterEnvAmountKnob, filterDecayKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrikeAudioProcessorEditor)
};
