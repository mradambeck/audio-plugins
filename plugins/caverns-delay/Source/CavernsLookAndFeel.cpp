#include "CavernsLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    const wildjag::HardwarePanelTheme cavernsTheme
    {
        .accentMuted = juce::Colour{0xff579e92},
        .accentBrightHi = juce::Colour{0xff5be6cc},
        .accentBrightLo = juce::Colour{0xff16a68f},
        .badgeInkColour = juce::Colour{0xff0c211d},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

CavernsLookAndFeel::CavernsLookAndFeel() : HardwarePanelLookAndFeel(cavernsTheme) {}
