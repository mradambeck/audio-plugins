#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace wildjag
{
    // One factory preset: a display name plus the raw parameter values setValueNotifyingHost()
    // takes after normalising (i.e. exactly the units a plugin's own createParameterLayout() range
    // uses - Hz, %, x, etc - not a 0-1 normalised value). Every Wild Jag plugin that ships built-in
    // presets decodes these directly from .aupreset files the developer saved via a host's native
    // preset UI (see each plugin's own getFactoryPresets() for its source presets and paramIDs),
    // rather than hand-tuning them - keep that convention when adding more.
    struct FactoryPreset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;
    };

    // getNumPrograms()/getCurrentProgram()/setCurrentProgram()/getProgramName() boilerplate for a
    // plugin's built-in factory presets - previously duplicated near-verbatim in every plugin's own
    // PluginProcessor.h/.cpp. A plugin's AudioProcessor now just owns one of these (constructed from
    // its own, necessarily plugin-specific, getFactoryPresets() table) and forwards its four
    // AudioProcessor::getNumPrograms() etc overrides straight through to it:
    //
    //   int XAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
    //   int XAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
    //   void XAudioProcessor::setCurrentProgram(int i) { factoryPresets.setCurrentProgram(i, apvts); }
    //   const juce::String XAudioProcessor::getProgramName(int i) { return factoryPresets.getProgramName(i); }
    //
    // Binds a reference to the table rather than copying it - every plugin's getFactoryPresets()
    // returns a function-local `static const` vector, so the reference is valid for the processor's
    // entire lifetime (indeed the whole process's), same as binding to any other static data.
    class FactoryPresetList
    {
    public:
        explicit FactoryPresetList(const std::vector<FactoryPreset>& presetsIn) : presets(presetsIn) {}

        int getNumPrograms() const { return (int) presets.size(); }
        int getCurrentProgram() const { return currentProgramIndex; }

        void setCurrentProgram(int index, juce::AudioProcessorValueTreeState& apvts)
        {
            if (! juce::isPositiveAndBelow(index, (int) presets.size()))
                return;

            currentProgramIndex = index;

            for (auto& [paramID, value] : presets[(size_t) index].values)
                if (auto* param = apvts.getParameter(paramID))
                    param->setValueNotifyingHost(param->convertTo0to1(value));
        }

        juce::String getProgramName(int index) const
        {
            return juce::isPositiveAndBelow(index, (int) presets.size()) ? presets[(size_t) index].name
                                                                           : juce::String();
        }

    private:
        const std::vector<FactoryPreset>& presets;

        // Matches every plugin's own prior currentProgramIndex convention: starts at 0 without
        // actually applying preset 0's values (the processor boots with its own APVTS defaults, not
        // any preset's) - it's the editor's presetCombo that's left visibly unselected (see
        // setupPresetCombo() below), so picking any preset, including the first, is always a real
        // selection change.
        int currentProgramIndex = 0;
    };

    // Sets up a hardware-panel preset combo box against an AudioProcessor's own
    // getNumPrograms()/getProgramName()/setCurrentProgram() - the setup boilerplate (LookAndFeel,
    // text colour, placeholder text, item population, parenting, onChange wiring) that was
    // otherwise duplicated near-verbatim in every plugin's editor constructor. Deliberately does
    // NOT set bounds - callers still position it themselves in resized(), since header layout
    // varies per plugin. Left unselected on startup (rather than showing the first preset's name):
    // JUCE's ComboBox doesn't fire onChange when you choose the item that's already showing, which
    // would otherwise make the first preset unreachable from this menu once the plugin loads with
    // its own default parameter values rather than the preset's. The text colour is set directly on
    // this instance rather than only via the shared LookAndFeel: ComboBox's internal text Label only
    // picks up ComboBox::textColourId inside ComboBox::colourChanged(), which fires from a direct
    // setColour() call on the instance, not from setting the colour on the LookAndFeel it shares
    // with every other component.
    inline void setupPresetCombo(juce::ComboBox& combo, juce::LookAndFeel& lookAndFeel,
                                  juce::Component& parent, juce::AudioProcessor& processor)
    {
        combo.setLookAndFeel(&lookAndFeel);
        combo.setColour(juce::ComboBox::textColourId, juce::Colour(0xffcfe3e0));
        combo.setTextWhenNothingSelected("Preset");
        for (int i = 0; i < processor.getNumPrograms(); ++i)
            combo.addItem(processor.getProgramName(i), i + 1);
        parent.addAndMakeVisible(combo);
        combo.onChange = [&combo, &processor]
        {
            processor.setCurrentProgram(combo.getSelectedItemIndex());
        };
    }
}
