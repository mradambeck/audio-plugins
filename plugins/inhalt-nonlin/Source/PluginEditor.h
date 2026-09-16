#pragma once

#include "../../common/UI/ResizableZoom.h"
#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

// PLAIN-JUCE PLACEHOLDER, not the final hardware-panel UI - matches Aura's own Phase C precedent
// (see that plugin's git history: "PluginEditor is a plain-JUCE placeholder - the real
// hardware-panel UI is separate follow-up work via the juce-hardware-panel-ui skill's
// mockup-first process, not something to skip/fake"). Same reasoning applies here: an HTML mockup
// needs to exist and be approved before any real chassis/knob styling work happens. This class
// still follows the catalog's usual EditorContent + AudioProcessorEditor-shell split so that swap
// doesn't need a structural rewrite, just new painting/layout inside EditorContent.
class InhaltEditorContent : public juce::Component
{
public:
    explicit InhaltEditorContent(InhaltAudioProcessor& processorToUse);

    void resized() override;

private:
    struct KnobRow
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // Configures `row` in place rather than returning one by value - KnobRow holds a juce::Slider
    // (non-copyable/non-movable, like every juce::Component), so it can't be constructed as a
    // temporary and moved/copied into a member.
    void setUpKnobRow(KnobRow& row, const juce::String& paramID, const juce::String& labelText);

    InhaltAudioProcessor& processor;

    KnobRow timeKnobRow, highRow, preDelayRow, lowCutRow, widthRow, dryRow, wetRow;
    juce::ComboBox converterBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> converterAttachment;
    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Shows InhaltParameterMap::gateLengthMsForDisplay(timeKnob) underneath the Time knob, since
    // the knob's own label (0.1-9.8) is not seconds - see PluginProcessor.h's timeKnobParamID
    // comment.
    juce::Label gateLengthLabel;
    void updateGateLengthLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InhaltEditorContent)
};

class InhaltAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit InhaltAudioProcessorEditor(InhaltAudioProcessor&);

    void resized() override;

private:
    InhaltEditorContent content;
    wildjag::ResizableZoomHandler zoomHandler;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InhaltAudioProcessorEditor)
};
