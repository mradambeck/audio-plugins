#pragma once

#include "ConvolutionVariant.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

// Turns a variant's embedded IR blobs into decoded, session-rate sample buffers, and remembers the
// results.
//
// Sample-rate conversion happens here, once per (IR, session rate) pair, rather than being left to
// juce::dsp::Convolution's own internal resampler. Two reasons: the shaped buffer handed to
// loadImpulseResponse() is then already at the session rate, so Length and Attack are applied to
// exactly the samples that get convolved; and the waveform the editor draws is exactly the IR the
// engine is using, not a pre-resampling approximation of it.
//
// Threading: every non-const member is blocking (decode, resample, allocation) and the cache is
// unsynchronised. IRLoadWorker's thread is the only owner in production; tests and the render
// harness call it synchronously. Never touch it from the audio thread.
namespace wildjag::conv
{

// A decoded IR plus where it came from. The native rate is kept alongside the samples so the
// editor can say "44.1 kHz" about an IR that is being convolved at 48 kHz - the resampling is
// invisible in the buffer itself, and whether an IR is being resampled is exactly the sort of
// thing worth being able to see.
struct DecodedIR
{
    juce::AudioBuffer<float> samples;   // at the session rate, normalised
    double nativeSampleRate = 0.0;      // the rate the blob was authored at
};

class IRLibrary
{
public:
    explicit IRLibrary(const ConvolutionVariant& variantToUse);

    // Discards the cache if the rate actually changed. Cheap and safe to call on every
    // prepareToPlay, including the common case where the host re-prepares at the same rate.
    void setTargetSampleRate(double newSampleRate);
    double getTargetSampleRate() const noexcept { return targetSampleRate; }

    int getNumIRs() const noexcept { return (int) variant.irs.size(); }

    // Decodes and resamples IR `index` if it is not already cached. Returns nullptr for an
    // out-of-range index or an undecodable blob - callers must treat that as "leave the current IR
    // alone", not as a reason to load silence.
    //
    // Shared ownership because the same decoded buffer feeds both the shaper and, potentially, a
    // later re-shape at different Length/Attack values without decoding twice.
    std::shared_ptr<const DecodedIR> getDecodedIR(int index);

    // Exposed for IRLibraryTests, which need to exercise decode, resample and normalise in
    // isolation from any variant or cache.
    static bool decodeBlob(const void* data, size_t dataSize,
                           juce::AudioBuffer<float>& destination, double& sourceSampleRate);
    static juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& source,
                                             double sourceSampleRate, double targetSampleRate);

    // Scales an IR to unit energy, in place, so convolving with it roughly preserves the input's
    // level. Applied once at decode time, to the WHOLE IR, which is the point: a per-shape
    // normalisation would make Length and Attack change the output level, and no normalisation at
    // all leaves the wet level at the mercy of how much energy a given capture happens to contain
    // (measured at ~34x for one of the harness IRs, which makes a shared Wet knob meaningless
    // across a variant's set). juce::dsp::Convolution is therefore loaded with Normalise::no.
    //
    // A unit impulse normalises to itself, so convolution with a Dirac IR stays an identity.
    static void normaliseToUnitEnergy(juce::AudioBuffer<float>& buffer);

private:
    const ConvolutionVariant& variant;
    double targetSampleRate = 0.0;
    std::vector<std::shared_ptr<const DecodedIR>> cache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRLibrary)
};

} // namespace wildjag::conv
