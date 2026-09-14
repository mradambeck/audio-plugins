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

ConcreteLookAndFeel::ConcreteLookAndFeel() : HardwarePanelLookAndFeel(concreteTheme)
{
    lcdTypeface = juce::Typeface::createSystemTypefaceFor(
        BinaryData::VCROSDMonoRegular_ttf, (size_t) BinaryData::VCROSDMonoRegular_ttfSize);
}

juce::Font ConcreteLookAndFeel::getLcdFont(float height) const
{
    // VCR OSD Mono's hhea ascent+descent (1800+0) is 0.8789x its unitsPerEm (2048) - measured with
    // fontTools, same technique/reasoning as HardwarePanelTheme::EmbeddedTypeface's own
    // heightCorrectionRatio (see that struct's comment): JUCE's Font height is ascent+descent, not
    // a CSS-px em box, so a raw height match to the mockup's CSS px would render this font larger
    // than intended. Not wired through EmbeddedTypeface since this typeface isn't part of the
    // shared theme struct at all - see this class's header comment.
    constexpr float heightCorrectionRatio = 0.87890625f;
    if (lcdTypeface != nullptr)
        return juce::Font(juce::FontOptions(lcdTypeface).withHeight(height * heightCorrectionRatio));
    return getDisplayFont(height); // fallback if the typeface somehow failed to load
}
