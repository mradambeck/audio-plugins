#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

// Placeholder editor: a stock GenericAudioProcessorEditor (auto-built sliders for every APVTS
// parameter). Deliberately not styled yet - per the build order, the hardware-panel UI comes only
// after the core algorithm is validated against the reference Midiverb IRs (see reference-irs/ and
// tools/compare_irs.py). Swap this out for a proper BloomLookAndFeel-based editor at that point.
class BloomAudioProcessorEditor : public juce::GenericAudioProcessorEditor
{
public:
    explicit BloomAudioProcessorEditor(BloomAudioProcessor&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BloomAudioProcessorEditor)
};
