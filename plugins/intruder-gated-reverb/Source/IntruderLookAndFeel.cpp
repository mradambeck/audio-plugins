#include "IntruderLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    const wildjag::HardwarePanelTheme intruderTheme
    {
        .accentMuted = juce::Colour{0xffa1a45c},
        .accentBrightHi = juce::Colour{0xffd3d95f},
        .accentBrightLo = juce::Colour{0xffbbc32a},
        .badgeInkColour = juce::Colour{0xff282815},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

IntruderLookAndFeel::IntruderLookAndFeel() : HardwarePanelLookAndFeel(intruderTheme) {}
