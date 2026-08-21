#include "KarplunkExcitation.h"

namespace
{
    // Minimum one-pole lowpass coefficient at brightness = 0 - heavy but not frozen smoothing
    // (alpha = 0 would never update the filter state at all). At brightness = 1, alpha = 1.0,
    // an exact passthrough (raw white noise, no filtering).
    constexpr float minLowpassAlpha = 0.02f;
}

void NoiseBurstExcitation::prepare(double) noexcept
{
    reset();
}

void NoiseBurstExcitation::reset() noexcept
{
    lowpassState = 0.0f;
}

void NoiseBurstExcitation::setBrightness(float amount01) noexcept
{
    brightness = amount01;
}

void NoiseBurstExcitation::generate(float* out, int numSamples, float velocity01) noexcept
{
    const auto alpha = minLowpassAlpha + brightness * (1.0f - minLowpassAlpha);

    for (int i = 0; i < numSamples; ++i)
    {
        // xorshift32 - fast, allocation-free, deterministic given a seed. Not juce::Random, to
        // keep this class free of any JUCE dependency (see header comment).
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;

        const auto whiteNoise = (float) rngState / (float) UINT32_MAX * 2.0f - 1.0f;

        lowpassState += alpha * (whiteNoise - lowpassState);
        out[i] = lowpassState * velocity01;
    }
}
