#include "ConvolutionProcessor.h"
#include "ConvolutionVariant.h"

// The plugin entry point, shared rather than duplicated into every variant. Together with
// variantConfig() and variantTheme() this is why a variant folder contains no C++ beyond its own
// IR table and accent colours - there is no per-variant processor or editor subclass to write.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wildjag::conv::ConvolutionProcessor(wildjag::conv::variantConfig());
}
