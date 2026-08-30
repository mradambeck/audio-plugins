#include "KarplunkLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Accent pair derived from the given highlight colour #469C6A (HSL ~145deg, 38%, 44%), the same
    // way every other plugin's theme derives its pair from one highlight colour - accentBrightLo is
    // the highlight colour itself; accentBrightHi is the same hue lightened (~59% L, +5% S);
    // accentMuted is the same hue darkened/desaturated (~31% L, -5% S); badgeInkColour is a near-
    // black shade of the same hue for text-on-badge contrast. Matches the approved mockup at
    // mockups/karplunk-mockup.html (see mockups/karplunk-mockup-reference.png for the reference
    // screenshot) - originally a warm tan/brown (#A57B69) accent, replaced with this green during
    // mockup review.
    const wildjag::HardwarePanelTheme karplunkTheme
    {
        .accentMuted = juce::Colour{0xff366a4c},
        .accentBrightHi = juce::Colour{0xff6bc490},
        .accentBrightLo = juce::Colour{0xff469c6a},
        .badgeInkColour = juce::Colour{0xff0f2418},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

KarplunkLookAndFeel::KarplunkLookAndFeel() : HardwarePanelLookAndFeel(karplunkTheme) {}
