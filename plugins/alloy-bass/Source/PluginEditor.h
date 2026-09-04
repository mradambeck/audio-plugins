#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "AlloyLookAndFeel.h"

#include "../../common/UI/ResizableZoom.h"

// All real painting/layout lives here. Unlike every other plugin in this catalog, this content's
// own native size is NOT fixed forever after construction - see setShowingPageOne() below, and
// onNativeSizeChanged's comment for how that's reconciled with the resizable/zoom mechanism in
// common/UI/ResizableZoom.h that AlloyAudioProcessorEditor (below) sets up around this class.
class AlloyEditorContent : public juce::Component, private juce::Timer
{
public:
    explicit AlloyEditorContent(AlloyAudioProcessor&);
    ~AlloyEditorContent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Fired whenever setShowingPageOne() changes this component's own native size (i.e. right after
    // its own setSize() call) - NOT fired at construction. wildjag::ResizableZoomHandler has no other
    // way to learn that its "native size" baseline moved, since page-switching (unlike a corner-grip
    // drag) originates from this content component, not from the host resizing the plugin window.
    std::function<void(juce::Point<int>)> onNativeSizeChanged;

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

    juce::ComboBox presetCombo;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlloyEditorContent)
};

// Thin shell around AlloyEditorContent: owns the plugin window's actual (resizable/zoomable) size.
// wildjag::ResizableZoomHandler (see common/UI/ResizableZoom.h) makes this editor natively resizable
// within a fixed aspect ratio and keeps content scaled via AffineTransform to fill it - corner-grip /
// window-edge drag, with no custom zoom UI drawn by the plugin itself. Always reopens at 100% (native
// size) - the resized size is deliberately not persisted. Also re-anchors the "native size" itself
// (via content.onNativeSizeChanged -> zoomHandler.setNativeSize()) whenever the page switch changes
// content's own native height, preserving whatever zoom level was already in effect.
class AlloyAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AlloyAudioProcessorEditor(AlloyAudioProcessor&);

private:
    AlloyEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlloyAudioProcessorEditor)
};
