#include "AuraLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Gold, H=39 - approved via HTML mockup iteration (plugins/aura-reverb/mockups/aura-mockup-v1.html,
    // 2026-09-02) after three earlier picks each landed too close to an existing plugin's hue
    // (indigo #6D85E6 replaced outright; green #62BA88 was 18deg from Strike; teal #56B399 was
    // 7deg from Caverns; amber #DC9952 was 10deg from Gradient) - H=39 sits in the actual open
    // gap between Gradient's orange (H21) and Corrosion's yellow-olive (H57), ~18deg clear of
    // each. accentBrightHi IS the wordmark literal verbatim (checked against every other
    // plugin's own titleLabel.setColour() call - the wordmark equals accentBrightHi exactly in
    // every plugin except Caverns, the oldest, a historical exception not the rule). The other
    // three are new variations at the same H=39, matching the catalog's usual S/L shape.
    const wildjag::HardwarePanelTheme auraTheme
    {
        .accentMuted = juce::Colour{0xff9B8150},
        .accentBrightHi = juce::Colour{0xffDCAC52},
        .accentBrightLo = juce::Colour{0xffC98D1D},
        .badgeInkColour = juce::Colour{0xff251D0E},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

AuraLookAndFeel::AuraLookAndFeel() : HardwarePanelLookAndFeel(auraTheme) {}
