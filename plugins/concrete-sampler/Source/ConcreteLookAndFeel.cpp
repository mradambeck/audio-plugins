#include "ConcreteLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Provisional "signal blue" accent - picked in concrete-sampler-plugin-plan.md (Identity /
    // Decisions) as the only clearly unoccupied hue against the rest of the catalog. NOT yet
    // reviewed against a real mockup; revisit during the Phase 8 mockup pass. Changing it is a
    // three-line edit here - nothing else in the codebase repeats these hex values, PluginEditor
    // reads them back via getAccentColour()/getBadgeInkColour().
    //
    // Derived the same way Strike's theme comment documents: accentBrightLo is the base highlight
    // colour itself (#7fa5f5, ~HSL 221deg/86%/73%); accentBrightHi is the same hue lightened
    // (~82% L); accentMuted (#5a72b0) was the paired "muted" value chosen alongside it; badgeInk
    // is a near-black shade of the same hue.
    const wildjag::HardwarePanelTheme concreteTheme
    {
        .accentMuted = juce::Colour{0xff5a72b0},
        .accentBrightHi = juce::Colour{0xffa5c0f8},
        .accentBrightLo = juce::Colour{0xff7fa5f5},
        .badgeInkColour = juce::Colour{0xff141a26},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

ConcreteLookAndFeel::ConcreteLookAndFeel() : HardwarePanelLookAndFeel(concreteTheme) {}
