#include "../PluginProcessor.h"

// The real createEditor() lives in PluginEditor.cpp, which this test target deliberately never
// compiles (that's what keeps PluginProcessor.cpp - and this target - decoupled from the GUI/
// LookAndFeel/font code). This stub exists only to satisfy the vtable's reference to the virtual
// override; no test ever calls it.
juce::AudioProcessorEditor* IntruderAudioProcessor::createEditor() { return nullptr; }
