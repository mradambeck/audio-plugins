#include "GradientLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    const wildjag::HardwarePanelTheme gradientTheme
    {
        .accentMuted = juce::Colour{0xffCE7954},
        .accentBrightHi = juce::Colour{0xffF0A177},
        .accentBrightLo = juce::Colour{0xffC2653A},
        .badgeInkColour = juce::Colour{0xff241209},
        .sliderTextBoxTextColour = juce::Colour{0xffc9a68c},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

GradientLookAndFeel::GradientLookAndFeel() : HardwarePanelLookAndFeel(gradientTheme) {}
