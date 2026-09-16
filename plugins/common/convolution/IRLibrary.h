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
    std::shared_ptr<const juce::AudioBuffer<float>> getDecodedIR(int index);

    // Exposed for IRLibraryTests, which need to exercise decode and resample in isolation from any
    // variant or cache.
    static bool decodeBlob(const void* data, size_t dataSize,
                           juce::AudioBuffer<float>& destination, double& sourceSampleRate);
    static juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& source,
                                             double sourceSampleRate, double targetSampleRate);

private:
    const ConvolutionVariant& variant;
    double targetSampleRate = 0.0;
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> cache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRLibrary)
};

} // namespace wildjag::conv
