#include "KarplunkLookAndFeel.h"
#include "BinaryData.h"

namespace
{
    // Accent pair derived from the given highlight colour #A57B69 (HSL ~18deg, 25%, 53%) the same
    // way every other plugin's theme derives its pair from one highlight colour - accentBrightLo
    // is the highlight colour itself; accentBrightHi is the same hue lightened (~68% L, +5% S);
    // accentMuted is the same hue darkened/desaturated (~40% L, 20% S); badgeInkColour is a near-
    // black shade of the same hue for text-on-badge contrast. No mockup HTML exists yet for
    // Karplunk (UI polish is out of scope for the base scaffold - see README.md), so these are
    // computed directly rather than read off a mockup file the way e.g. Shields' theme comment
    // points at mockups/shields-mockup-v1.html.
    const wildjag::HardwarePanelTheme karplunkTheme
    {
        .accentMuted = juce::Colour{0xff7a5e52},
        .accentBrightHi = juce::Colour{0xffc6a495},
        .accentBrightLo = juce::Colour{0xffa57b69},
        .badgeInkColour = juce::Colour{0xff24150f},
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };
}

KarplunkLookAndFeel::KarplunkLookAndFeel() : HardwarePanelLookAndFeel(karplunkTheme) {}
