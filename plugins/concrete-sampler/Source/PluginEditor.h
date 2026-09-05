#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ConcreteLookAndFeel.h"
#include "PluginProcessor.h"

// Phase 0 placeholder editor - just enough for the AU/VST3/Standalone formats to build and open a
// window. Phase 1 adds a file loader/waveform/on-screen keyboard; Phase 8 replaces all of it with
// the real hardware-panel UI once the DSP phases have settled what controls exist. See
// concrete-sampler-plugin-plan.md.
class ConcreteAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ConcreteAudioProcessorEditor(ConcreteAudioProcessor&);
    ~ConcreteAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ConcreteAudioProcessor& processor;
    ConcreteLookAndFeel lookAndFeel;
};
