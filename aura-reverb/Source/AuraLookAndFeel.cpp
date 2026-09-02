#include "AuraLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Indigo/periwinkle - distinct from every existing accent in the catalog (blue #6ea8c9,
    // teal-cyan #5be6cc, olive-yellow #d3d95f (Intruder), red #f04c42, purple #b47bf0 (more
    // magenta-leaning than this), orange #f0a177, pink/magenta #ec6594 (Shields), green #6bc490).
    // Placeholder pending real UI work (juce-hardware-panel-ui skill) - easy to adjust then.
    const wildjag::HardwarePanelTheme auraTheme
    {
        .accentMuted = juce::Colour{0xff3a4499},
        .accentBrightHi = juce::Colour{0xff8b9af2},
        .accentBrightLo = juce::Colour{0xff5b6ee8},
        .badgeInkColour = juce::Colour{0xff1a1e3d},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

AuraLookAndFeel::AuraLookAndFeel() : HardwarePanelLookAndFeel(auraTheme) {}
