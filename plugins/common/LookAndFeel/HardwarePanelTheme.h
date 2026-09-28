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

        // The hardware-section outline (the rounded rectangle drawHardwareSection() strokes around
        // each control group). Defaults match every existing plugin's own copy-pasted
        // drawHardwareSection() literals - only the shared convolution editor reads these fields
        // today (see ConvolutionEditor.cpp), so every other plugin's private drawHardwareSection()
        // copy is unaffected either way.
        juce::Colour sectionBorderColour = juce::Colour(0xffe6ece6).withAlpha(0.62f);
        float sectionBorderThickness = 3.5f;
        float sectionBorderCornerRadius = 7.0f;

        // When true, drawHardwareSection() draws the section label as plain text (in
        // sectionBorderColour, matching the border it breaks) instead of the default filled
        // accentMuted badge sitting inside/on it. Off by default - every existing plugin's own
        // drawHardwareSection() copy keeps its current filled-badge look; only the shared
        // convolution editor reads this field so far.
        bool sectionLabelBreaksBorder = false;

        // Fixed-pixel override for getLinearSliderThumbWidth()'s default 0.8x-of-bounds proportional
        // width (see that method's own comment on why the default isn't actually catalog-uniform).
        // 0 (the default) keeps that proportional behaviour - every plugin with its own LookAndFeel
        // subclass overrides the virtual directly instead (that's most of the catalog); this field
        // exists because the shared convolution editor has no per-variant subclass to override it
        // in, so a theme-driven escape hatch is the only way one variant can fix its fader thumb
        // width without changing every other variant's (proportional, column-width-dependent) one.
        float linearSliderThumbWidthOverride = 0.0f;

        // When true, the shared convolution editor draws 9 evenly-spaced hardware-fader-panel tick
        // marks between the Dry and Wet faders (sectionBorderColour, half sectionBorderThickness).
        // Off by default - purely decorative, opt-in per variant.
        bool drawMixDividerTicks = false;

        // When true, the shared convolution editor skips the outer "chassis" bezel (a separately-
        // rounded, drop-shadowed, grain-textured device shape the panel normally sits inset within)
        // and instead fills the panel edge to edge across the whole component. The editor's own
        // fixed window size (editorWidth/editorHeight) is unaffected either way - only how much
        // of it the chassis eats into. Off by default - every existing variant keeps its current
        // chassis-framed look.
        bool hideChassisBezel = false;

        EmbeddedTypeface displayTypeface;
        EmbeddedTypeface smallPrintTypeface;
    };
}
