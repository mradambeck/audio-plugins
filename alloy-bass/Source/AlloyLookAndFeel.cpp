#include "AlloyLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // No fader in Alloy's UI (approved via HTML mockup iteration), so accentBrightLo is never
    // actually exercised -- left unset (matches the original file, which never defined it either).
    const wildjag::HardwarePanelTheme alloyTheme
    {
        .accentMuted = juce::Colour{0xff4e7691},
        .accentBrightHi = juce::Colour{0xff6ea8c9},
        .badgeInkColour = juce::Colour{0xff0a1519},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

AlloyLookAndFeel::AlloyLookAndFeel() : HardwarePanelLookAndFeel(alloyTheme) {}
