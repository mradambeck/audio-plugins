#include "ConvolutionVariantTheme.h"

#include "BinaryData.h"

// ConvBase's other half of the variant contract: what it looks like. Rebranding is editing this
// file - there is no LookAndFeel subclass, because wildjag::HardwarePanelLookAndFeel already takes
// a theme and the shared editor constructs it from here.
namespace wildjag::conv
{

const wildjag::HardwarePanelTheme& variantTheme()
{
    // Indigo at H=244, from the approved mockup. Chosen from the catalog's actual open hue gaps
    // rather than by taste: the shipped accents sit at H=3, 21, 39, 57, 63, 145, 169, 202, 220,
    // 269 and 339, and 244 is the midpoint of the widest remaining gap (Concrete's periwinkle at
    // 220 to Flux's violet at 269), leaving 24deg of clearance either side. An earlier cyan pick
    // at H=190 was rejected for landing 12deg from Alloy.
    static const wildjag::HardwarePanelTheme theme {
        .accentMuted = juce::Colour { 0xff55509B },
        .accentBrightHi = juce::Colour { 0xff766EED },
        .accentBrightLo = juce::Colour { 0xff493EE0 },
        .badgeInkColour = juce::Colour { 0xff121127 },
        // heightCorrectionRatio values come from the fonts themselves, not from this variant -
        // see HardwarePanelTheme.h's note on measuring them with fontTools.
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };

    return theme;
}

} // namespace wildjag::conv
