#include "FluxLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    const wildjag::HardwarePanelTheme fluxTheme
    {
        .accentMuted = juce::Colour{0xff8070b8},
        .accentBrightHi = juce::Colour{0xffb47bf0},
        .accentBrightLo = juce::Colour{0xff8340c9},
        .badgeInkColour = juce::Colour{0xff1c1526},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

FluxLookAndFeel::FluxLookAndFeel() : HardwarePanelLookAndFeel(fluxTheme) {}
