#pragma once

#include <cstddef>
#include <vector>

// The entire per-variant contract for a bundled-IR convolution reverb. A "variant" is one branded
// product built from the shared engine in this folder: its own name, its own fixed set of impulse
// responses, its own accent colours. Everything else - processor, editor, DSP - is shared.
//
// Deliberately free of any juce_gui_basics dependency: the processor and the headless test/render
// targets need the IR table, but must not drag the LookAndFeel (and therefore a plugin's private
// BinaryData namespace) in with it. A variant's accent colours are supplied separately via
// VariantTheme.h, which only the editor translation unit includes. See this catalog's existing
// Source/Tests/TestCreateEditorStub.cpp trick for the same layering rule applied to createEditor().
namespace wildjag::conv
{
    // One selectable impulse response, pointing at bytes owned by the variant's BinaryData target.
    // The blob is an encoded audio file (FLAC in practice, but any format JUCE's AudioFormatManager
    // registers will decode), not raw samples - IRLibrary decodes and resamples it on a background
    // thread the first time it is selected.
    struct IRAsset
    {
        const char* displayName = nullptr;  // "Plate - Bright", shown in the dropdown
        const char* category = nullptr;     // "Plates", a dropdown sub-heading; null for no grouping
        const void* data = nullptr;         // e.g. BinaryData::plateBright_flac
        size_t dataSize = 0;
    };

    struct ConvolutionVariant
    {
        // The product name as shown in the UI. Not the CMake project name or PRODUCT_NAME, though
        // in practice they match. Still used for the header when logoSvgData is null (see below),
        // and always used for the footer credit regardless.
        const char* displayName = nullptr;

        // The bundled IRs, in dropdown order. Must not be empty - a convolution reverb with no IR
        // has nothing to convolve with, and ConvolutionProcessor asserts on it at construction.
        std::vector<IRAsset> irs;

        // Which IR a fresh instance starts on. Clamped into range at construction.
        int defaultIRIndex = 0;

        // Optional wordmark, drawn in the header in place of displayName as plain text when
        // present. Raw SVG bytes owned by the variant's BinaryData target (e.g.
        // BinaryData::logo_svg) - no juce_gui_basics dependency here, same as IRAsset::data, so the
        // headless test/render harnesses stay unaffected. Null (the default) means "no logo - draw
        // displayName as text", which is every existing variant's current behaviour. Deliberately
        // last: every existing variant's VariantConfig.cpp brace-initializes this struct
        // positionally (displayName, irs, defaultIRIndex), and a trailing field with a default
        // keeps those working unchanged - inserting it earlier breaks that positional init.
        const void* logoSvgData = nullptr;
        size_t logoSvgDataSize = 0;

        // Upper bound of the Wet parameter's range, as a percent. Every existing variant ships
        // 200 (unity-plus-headroom, so there's no separate output gain stage - see
        // ConvolutionProcessor.cpp's own comment on why). A variant can cap this at the more
        // conventional 100 instead; the default parameter value (40%) is unaffected either way
        // since it's already inside both ranges. Also last, for the same positional-init reason as
        // logoSvgData above.
        float wetMaxPercent = 200.0f;

        // The subtitle drawn next to the header wordmark/title. Null (the default) means
        // "Convolution Reverb", every existing variant's current text.
        const char* tagline = nullptr;

        // Width, in px, of the MIX (Dry/Wet fader) column. Every existing variant ships 130; a
        // variant can widen it (e.g. to match a sibling plugin's own Mix column) at the cost of a
        // proportionally wider window, since this also feeds the editor's fixed native size.
        int mixSectionWidth = 130;

        // The footer's manufacturer credit (bottom-right corner). Null (the default) means "WILD
        // JAG", every existing variant's current text. Separate from COMPANY_NAME (the CMakeLists.txt
        // property, used for the actual AU/VST3 manufacturer metadata) - this is only what's drawn
        // on screen, for a variant that wants the two to read differently (e.g. a co-branding
        // credit) without touching the plugin's real registered manufacturer. Drawn exactly as
        // typed here, NOT run through .toUpperCase() - so a variant can mix case deliberately (e.g.
        // a lowercase "x" in an otherwise-uppercase credit) rather than being forced fully upper.
        const char* manufacturerCredit = nullptr;
    };

    // Supplied by each variant, in its own VariantConfig.cpp. Declared here rather than in a
    // per-variant header so the shared processor and plugin entry point can call it without knowing
    // which variant they were compiled into - this plus variantTheme() (ConvolutionVariantTheme.h)
    // is the entire surface a variant has to implement.
    //
    // The returned reference must outlive every processor instance; in practice variants return a
    // function-local static.
    const ConvolutionVariant& variantConfig();
}
