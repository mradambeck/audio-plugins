#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// Length/truncation and Attack/Shape applied to a decoded impulse response, as a pure function:
// same input buffer + same params always yields the same output buffer, with no state and no
// dependency on the engine, the processor, or a sample rate beyond the one passed in. That is what
// makes the two "default is transparent" guarantees testable rather than merely intended - see
// isIdentity() below.
//
// Never call shape() on the audio thread: it allocates. IRLoadWorker owns the only production call
// site and runs it on a background thread; the tests and the render harness call it synchronously.
namespace wildjag::conv::IRShaper
{
    struct Params
    {
        // Portion of the IR retained, 0-100. 100 means the full IR as recorded, and is the default.
        // Expressed as a percentage rather than seconds so the control means the same thing for a
        // 0.8s room and an 8s hall, and so a variant swap never leaves a stale absolute value.
        float lengthPercent = 100.0f;

        // Fade-in applied to the front of the IR, in milliseconds. 0 means the attack as recorded,
        // and is the default.
        float attackMs = 0.0f;
    };

    // Longest fade-out spliced onto a truncated IR, and the fraction of the retained length used
    // when that is shorter. A hard cut at the truncation point is a step discontinuity in the IR,
    // which convolves into an audible click on every transient - the fade is not cosmetic.
    inline constexpr float maxTruncationFadeMs = 30.0f;
    inline constexpr float truncationFadeFraction = 0.1f;

    // Shortest IR the shaper will produce. Below roughly this length the result stops behaving like
    // a reverb and starts behaving like a comb filter, and the fade-out has no room to work.
    inline constexpr int minimumShapedSamples = 64;

    // True when the params leave the IR exactly as recorded. shape() returns a bit-identical copy in
    // this case, which is the contract behind "Length and Attack default to playing the IR as
    // recorded" - see IRShaperTests.
    bool isIdentity(const Params& params) noexcept;

    // Applies the length truncation (with its fade-out) and then the attack fade-in. Allocates.
    juce::AudioBuffer<float> shape(const juce::AudioBuffer<float>& source, double sampleRate, const Params& params);

    // Downsampled absolute-peak envelope for the waveform display, one value per output point, each
    // the maximum absolute sample across all channels within that point's span. Computed off the
    // audio thread alongside the shaped buffer so the editor never reads the live IR.
    std::vector<float> computePeakEnvelope(const juce::AudioBuffer<float>& buffer, int numPoints);
}
