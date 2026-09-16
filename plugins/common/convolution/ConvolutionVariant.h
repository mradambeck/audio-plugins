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
        // in practice they match.
        const char* displayName = nullptr;

        // The bundled IRs, in dropdown order. Must not be empty - a convolution reverb with no IR
        // has nothing to convolve with, and ConvolutionProcessor asserts on it at construction.
        std::vector<IRAsset> irs;

        // Which IR a fresh instance starts on. Clamped into range at construction.
        int defaultIRIndex = 0;
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
