#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace wildjag
{
    // A typeface sourced from a plugin's own BinaryData. HardwarePanelLookAndFeel never includes
    // any plugin's BinaryData.h itself -- each plugin's BinaryData target/namespace is private to
    // that plugin's build -- so the raw bytes are handed in here by the subclass instead.
    struct EmbeddedTypeface
    {
        const void* data = nullptr;
        size_t dataSize = 0;

        // JUCE's Font "height" is defined as ascent+descent, not the em/CSS-px box. Most fonts'
        // hhea ascent+descent happens to equal unitsPerEm (ratio 1.0), but some (Oswald, Rajdhani)
        // don't -- this is the per-font correction factor so a requested height behaves like a
        // CSS px value regardless of font. Measure via fontTools: (hhea.ascent + hhea.descent) /
        // head.unitsPerEm.
        float heightCorrectionRatio = 1.0f;
    };

    // The one set of values that varies per plugin: the accent colour pair/badge ink (the
    // historic "PLUGIN-SPECIFIC" block), plus the display/small-print typefaces.
    struct HardwarePanelTheme
    {
        juce::Colour accentMuted;      // badges, combo arrows
        juce::Colour accentBrightHi;   // fader fill, brand wordmark family
        juce::Colour accentBrightLo;
        juce::Colour badgeInkColour;

        // Slider text-box (knob value readout) text colour. Default (0xff7f938f, a teal-grey)
        // matches every plugin except Gradient, which uses a warm tan (0xffc9a68c) to match its
        // terracotta accent instead of the generic grey.
        juce::Colour sliderTextBoxTextColour{0xff7f938f};

        EmbeddedTypeface displayTypeface;
        EmbeddedTypeface smallPrintTypeface;
    };
}
