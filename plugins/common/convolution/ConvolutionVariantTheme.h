#pragma once

#include "../LookAndFeel/HardwarePanelTheme.h"

// The GUI half of the per-variant contract, kept apart from ConvolutionVariant.h so that headless
// targets - the tests, the render harness - can use the IR table without linking juce_gui_basics or
// a variant's BinaryData. Only the editor translation unit includes this.
namespace wildjag::conv
{
    // Supplied by each variant, in its own VariantTheme.cpp. Rebranding a variant is, visually,
    // editing the colours returned here.
    const wildjag::HardwarePanelTheme& variantTheme();
}
