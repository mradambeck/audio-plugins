#include "InhaltLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Electric violet/magenta, H=300 - approved via HTML mockup iteration
    // (plugins/inhalt-nonlin/mockups/inhalt-mockup-v1.html, 2026-09-19). Full catalog hue survey
    // (accentBrightHi for each): Damage 3, Gradient 21, Aura 39, Corrosion 57, Intruder 63,
    // Strike 145, Caverns 169, Alloy 202, Concrete 221, Flux 269, Shields 339. H=300 sits in the
    // open Flux(269)-Shields(339) gap, 31deg clear of Flux and 39deg clear of Shields - both past
    // the ~18deg minimum clearance Aura's own accent picked in a much tighter gap. Deliberately a
    // cool violet, not another warm yellow/amber/olive hue - Inhalt sits topically next to both
    // Aura (RMX16 Ambience, H39 amber) and Intruder (gated reverb, H63 yellow-green), so this
    // reads clearly distinct from both at a glance, not just by degree count. accentMuted/
    // accentBrightLo/badgeInk are variations at the same H=300, matching the catalog's usual S/L
    // shape (muted: S32,L46 / brightHi: S66,L59 / brightLo: S75,L45 - Aura's own documented shape,
    // just rotated to H300).
    const wildjag::HardwarePanelTheme inhaltTheme
    {
        .accentMuted = juce::Colour{0xff9B509B},
        .accentBrightHi = juce::Colour{0xffDB51DB},
        .accentBrightLo = juce::Colour{0xffC91DC9},
        .badgeInkColour = juce::Colour{0xff281528},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

InhaltLookAndFeel::InhaltLookAndFeel() : HardwarePanelLookAndFeel(inhaltTheme) {}
