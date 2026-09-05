#include "PluginEditor.h"

ConcreteAudioProcessorEditor::ConcreteAudioProcessorEditor(ConcreteAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);
    setSize(600, 400);
}

ConcreteAudioProcessorEditor::~ConcreteAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void ConcreteAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1f20));
}

void ConcreteAudioProcessorEditor::resized() {}

juce::AudioProcessorEditor* ConcreteAudioProcessor::createEditor()
{
    return new ConcreteAudioProcessorEditor(*this);
}
