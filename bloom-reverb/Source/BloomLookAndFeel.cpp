#include "BloomLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Accent pair derived from the given highlight colour #D74377 (see mockups/bloom-mockup-v1.html
    // for the HSL derivation) - accentBrightLo is the highlight colour itself.
    const wildjag::HardwarePanelTheme bloomTheme
    {
        .accentMuted = juce::Colour{0xffA74467},
        .accentBrightHi = juce::Colour{0xffEC6594},
        .accentBrightLo = juce::Colour{0xffD74377},
        .badgeInkColour = juce::Colour{0xff2F0E1A},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

BloomLookAndFeel::BloomLookAndFeel() : HardwarePanelLookAndFeel(bloomTheme) {}
