#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AlloyLookAndFeel.h"

class AlloyAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AlloyAudioProcessorEditor(AlloyAudioProcessor&);
    ~AlloyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // A slider + its name label + its APVTS attachment - bundled since Alloy has ~35 of these
    // (individually-named members, as the rest of the catalog uses, would be an enormous amount
    // of near-identical boilerplate at this control count).
    struct KnobControl
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

        void setVisible(bool shouldBeVisible) { slider.setVisible(shouldBeVisible); label.setVisible(shouldBeVisible); }
    };

    // A combo box + its caption label + its APVTS attachment.
    struct ComboControl
    {
        juce::ComboBox combo;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

        void setVisible(bool shouldBeVisible) { combo.setVisible(shouldBeVisible); label.setVisible(shouldBeVisible); }
    };

    void setupKnob(KnobControl&, const juce::String& paramID, const juce::String& labelText, bool mini);
    void setupCombo(ComboControl&, const juce::String& paramID, const juce::String& labelText,
                     const juce::StringArray& choices);
    void setupLedButton(juce::ToggleButton&, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&,
                         const juce::String& paramID, const juce::String& text);
    void setupADSR(std::array<KnobControl, 4>&, const juce::String paramIDs[4], bool mini);

    void setupSubLabel(juce::Label&, const juce::String& text);

    void rebuildChassisTexture();
    void drawHardwareSection(juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& label);

    // Lays out a knob + its name label (below the knob) + its built-in value textbox (below
    // that), matching the mockup's DOM order.
    void positionKnob(juce::Rectangle<int> cell, int knobSize, KnobControl&);
    void positionCombo(juce::Rectangle<int> cell, int comboWidth, ComboControl&);

    // Two toggleable pages (Analog+FM, then Sub+Arp+Mix) rather than stacking both rows in one
    // tall window - the combined layout didn't fit on smaller (e.g. laptop) displays. Both pages'
    // controls are always real children of the editor; only one page's controls are ever visible
    // (and only that page's bounds get laid out in resized()) at a time, and the window itself is
    // resized to fit whichever page is currently showing.
    void setShowingPageOne(bool showPageOne);
    void updatePageVisibility();
    int contentHeightForPage(bool pageOne) const;

    AlloyAudioProcessor& processorRef;
    AlloyLookAndFeel lookAndFeel;

    juce::Image chassisTexture;
    juce::Rectangle<float> analogSectionBounds, fmSectionBounds, subSectionBounds, arpSectionBounds, mixSectionBounds;

    juce::Label titleLabel, tagLabel;

    juce::TextButton panicButton;
    juce::TextButton pageButton;
    bool showingPageOne = true;

    // ---- Analog ----
    ComboControl analogWaveform, analogOctave, analogUnison;
    KnobControl analogDetune, analogFilterCutoff, analogFilterResonance, analogFilterEnvAmount, analogVelToFilter;
    juce::Label filterEnvLabel, ampEnvLabel;
    std::array<KnobControl, 4> analogFilterEnvKnobs;
    std::array<KnobControl, 4> analogAmpEnvKnobs;
    KnobControl analogGlide, analogVolume;

    // ---- Sub ----
    juce::ToggleButton subEnabledButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> subEnabledAttachment;
    ComboControl subWaveform, subOctave;
    KnobControl subVolume;

    // ---- FM ----
    juce::Label fmCarrierLabel, fmModulatorLabel;
    ComboControl fmCarrierWaveform, fmCarrierOctave;
    KnobControl fmCarrierVolume, fmVelocityToCarrier;
    std::array<KnobControl, 4> fmCarrierEnvKnobs;
    ComboControl fmModulatorWaveform, fmModulatorOctave;
    KnobControl fmModulatorVolume, fmVelocityToBrightness, fmModulatorBrightness;
    std::array<KnobControl, 4> fmModulatorEnvKnobs;

    // ---- Arp ----
    juce::ToggleButton arpEnabledButton, arpSyncButton, arpHoldButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> arpEnabledAttachment, arpSyncAttachment, arpHoldAttachment;
    ComboControl arpDivision, arpPattern, arpOctaveRange;
    KnobControl arpRate, arpGate;

    // ---- Mix ----
    KnobControl mixDrive, mixTone, mixOutput, mixAge;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlloyAudioProcessorEditor)
};
