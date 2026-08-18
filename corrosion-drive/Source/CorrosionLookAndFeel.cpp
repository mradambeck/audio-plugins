#include "CorrosionLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    const wildjag::HardwarePanelTheme corrosionTheme
    {
        .accentMuted = juce::Colour{0xffaeaa62},
        .accentBrightHi = juce::Colour{0xffe6df5c},
        .accentBrightLo = juce::Colour{0xffa7a016},
        .badgeInkColour = juce::Colour{0xff241f0c},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

CorrosionLookAndFeel::CorrosionLookAndFeel() : HardwarePanelLookAndFeel(corrosionTheme) {}
