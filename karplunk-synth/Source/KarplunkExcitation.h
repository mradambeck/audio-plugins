#pragma once

#include <cstdint>

// The Excitation seam: "generate N samples to seed/re-seed the delay line." Deliberately no JUCE
// include at all (own xorshift PRNG instead of juce::Random) - matches
// gradient-pitch/GradientPitchShiftEngine's convention of keeping standalone DSP classes free of
// any JUCE dependency, so they build/test fast in isolation and stay trivially swappable.
//
// To add a new excitation variant (filtered noise with a different colour, a sample-based burst,
// etc.), write a new class matching this same method set (prepare/reset/setBrightness/generate)
// and swap the template argument in KarplunkVoice.h's SingleLineKarplunkVoice instantiation -
// nothing in KarplunkLoopFilter.h, KarplunkStringLine.h, or KarplunkVoice.h needs to change.
class NoiseBurstExcitation
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // 0 = fully darkened (one-pole lowpassed) noise, 1 = raw white noise. Applied inside
    // generate(), not as a separate post-process pass.
    void setBrightness(float amount01) noexcept;

    // Fills out[0..numSamples) with a fresh pluck excitation, scaled by velocity01. Called
    // exactly once per noteOn, never mid-block or per-sample - numSamples is always bounded by
    // the caller (SingleLineKarplunkVoice::noteOn) to that note's delay length, itself bounded by
    // the capacity preallocated in KarplunkStringLine::prepare(). This method never allocates.
    void generate(float* out, int numSamples, float velocity01) noexcept;

private:
    uint32_t rngState = 1;
    float lowpassState = 0.0f;
    float brightness = 1.0f;
};
