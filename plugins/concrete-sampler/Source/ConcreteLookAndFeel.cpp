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

void ConcreteLookAndFeel::paintSilkscreenLabel(juce::Graphics& g, juce::Rectangle<float> area,
                                                const juce::String& text, bool centered) const
{
    // SilkscreenLabel.module.css: font-size 10, letter-spacing 2px, uppercase, color #6a6a6a; the
    // rule is #3a3a3a, 1px, with an 8px gap from the text. 2px/10px = 0.2 kerning factor - the
    // same ratio already established for this typeface/size pairing (see ConcretePadGrid's own
    // "Pads" label and ConcreteKnob's legend, both derived the same way).
    const juce::Colour labelColour { 0xff6a6a6a };
    const juce::Colour ruleColour { 0xff3a3a3a };
    const auto font = getSmallPrintFont(10.0f).withExtraKerningFactor(0.2f);
    const auto upper = text.toUpperCase();

    g.setColour(labelColour);
    g.setFont(font);

    if (centered)
    {
        g.drawText(upper, area, juce::Justification::centred);
        return;
    }

    auto remaining = area;
    const auto textWidth = juce::GlyphArrangement::getStringWidth(font, upper);
    g.drawText(upper, remaining.removeFromLeft(textWidth), juce::Justification::centredLeft);
    g.setColour(ruleColour);
    g.fillRect(juce::Rectangle<float>(remaining.getX() + 8.0f, remaining.getCentreY() - 0.5f,
                                       juce::jmax(0.0f, remaining.getWidth() - 8.0f), 1.0f));
}
