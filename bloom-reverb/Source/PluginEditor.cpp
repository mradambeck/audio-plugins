#include "PluginEditor.h"

BloomAudioProcessorEditor::BloomAudioProcessorEditor(BloomAudioProcessor& p)
    : juce::GenericAudioProcessorEditor(p)
{
    setSize(400, 500);
}
