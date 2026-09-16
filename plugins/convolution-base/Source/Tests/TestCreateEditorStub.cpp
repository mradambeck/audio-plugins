#include "ConvolutionProcessor.h"

// The real createEditor() lives in ../../common/convolution/ConvolutionEditor.cpp, which this test
// target deliberately never compiles - that is what keeps the shared processor, and this target,
// decoupled from the GUI/LookAndFeel/font code. This stub exists only to satisfy the vtable's
// reference to the virtual override; no test calls it.
juce::AudioProcessorEditor* wildjag::conv::ConvolutionProcessor::createEditor() { return nullptr; }
