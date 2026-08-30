#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "KarplunkLookAndFeel.h"
#include "PluginProcessor.h"

// Bundles a rotary knob with its own name label and parameter attachment, matching Gradient's
// GradientRotaryKnob precedent - justified here by Karplunk's own large control count (18 knobs).
struct KarplunkRotaryKnob
{
    juce::Slider slider;
    juce::Label nameLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct KarplunkToggle
{
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// No name label - every combo in Karplunk's hardware-panel design (Noise Color, Waveshaper Type)
// is either self-evident from its own text or paired directly beneath a knob that already has a
// name label, per the approved mockup (mockups/karplunk-mockup.html).
struct KarplunkCombo
{
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Hardware-panel chassis/section editor, matching the approved mockup at
// mockups/karplunk-mockup.html pixel-for-pixel (see mockups/karplunk-mockup-reference.png) -
// replaces the plain-grid base-scaffold editor. Layout/structure follows Gradient's own reference
// implementation (Source/PluginEditor.h/.cpp) - see that file's header comment for the
// COPY-VERBATIM/PLUGIN-SPECIFIC split this file follows.
class KarplunkAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit KarplunkAudioProcessorEditor(KarplunkAudioProcessor&);
    ~KarplunkAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setupRotaryKnob(KarplunkRotaryKnob&, const juce::String& labelText, const juce::String& paramID);
    void setupToggle(KarplunkToggle&, const juce::String& buttonText, const juce::String& paramID);
    void setupCombo(KarplunkCombo&, const juce::StringArray& items, const juce::String& paramID);
    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    // Positions a rotary knob + its name label together, matching the mockup's .knob-cell DOM
    // order (knob, then name, then value).
    void positionKnob(juce::Rectangle<int> cell, int knobSize, KarplunkRotaryKnob&);

    // Sizes and positions a toggle button from its own button text, matching drawToggleButton's
    // content layout (LED + gap + text) plus outer padding - so a button's width always matches
    // what it actually draws rather than a hand-guessed constant.
    void positionToggle(juce::Rectangle<int> cell, KarplunkToggle&);

    KarplunkAudioProcessor& processorRef;
    KarplunkLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> strumSectionBounds, colorSectionBounds, outputSectionBounds;
    juce::Rectangle<float> ringModSectionBounds, crossCoupleSectionBounds, filterSectionBounds;

    juce::Label titleLabel;
    juce::Label tagLabel;

    KarplunkToggle monoToggle;

    // ---- STRUM section ----
    KarplunkRotaryKnob decayKnob, brightnessKnob, bowAmountKnob, bowForceKnob;
    KarplunkCombo noiseColorCombo;

    // ---- COLOR section ----
    KarplunkToggle postFilterToggle;
    KarplunkRotaryKnob structureKnob, stringPositionKnob, waveshapeKnob;
    KarplunkCombo waveshaperTypeCombo;

    // ---- OUTPUT section ----
    KarplunkRotaryKnob volumeKnob;

    // ---- RING MOD section ----
    KarplunkRotaryKnob ringModAmountKnob, ringModFrequencyKnob;

    // ---- CROSS COUPLE section ----
    KarplunkToggle crossCoupleOnToggle;
    KarplunkRotaryKnob crossCoupleAmountKnob, coupleDelayKnob, detuneKnob;

    // ---- FILTER section ----
    KarplunkToggle filterOnToggle;
    KarplunkRotaryKnob filterCutoffKnob, resonanceKnob, filterAttackKnob, filterEnvAmountKnob, filterDecayKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarplunkAudioProcessorEditor)
};
