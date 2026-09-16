#include "ConvolutionVariantTheme.h"

#include "BinaryData.h"

// ConvBase's other half of the variant contract: what it looks like. Rebranding is editing this
// file - there is no LookAndFeel subclass, because wildjag::HardwarePanelLookAndFeel already takes
// a theme and the shared editor constructs it from here.
namespace wildjag::conv
{

const wildjag::HardwarePanelTheme& variantTheme()
{
    static const wildjag::HardwarePanelTheme theme {
        .accentMuted = juce::Colour { 0xff2E7D8A },
        .accentBrightHi = juce::Colour { 0xff4FC3D9 },
        .accentBrightLo = juce::Colour { 0xff3AA7BD },
        .badgeInkColour = juce::Colour { 0xff0A2429 },
        // heightCorrectionRatio values come from the fonts themselves, not from this variant -
        // see HardwarePanelTheme.h's note on measuring them with fontTools.
        .displayTypeface = { BinaryData::OxaniumBold_ttf, (size_t) BinaryData::OxaniumBold_ttfSize, 1.0f },
        .smallPrintTypeface = { BinaryData::OswaldSemiBold_ttf, (size_t) BinaryData::OswaldSemiBold_ttfSize, 1.482f },
    };

    return theme;
}

} // namespace wildjag::conv
