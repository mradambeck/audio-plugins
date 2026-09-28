#include "IRShaper.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv::IRShaper
{

namespace
{
    // Raised cosine rising from 0 to 1 across position 0..1. Used for both the attack fade-in and
    // (mirrored) the truncation fade-out. Chosen over a linear ramp because its first derivative is
    // zero at both ends, so splicing it onto the IR introduces no slope discontinuity either where
    // the fade starts or where it finishes.
    float raisedCosine(float position) noexcept
    {
        const auto clamped = juce::jlimit(0.0f, 1.0f, position);
        return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * clamped);
    }
}

bool isIdentity(const Params& params) noexcept
{
    return params.lengthPercent >= 100.0f && params.attackMs <= 0.0f;
}

juce::AudioBuffer<float> shape(const juce::AudioBuffer<float>& source, double sampleRate, const Params& params)
{
    const auto numChannels = source.getNumChannels();
    const auto sourceLength = source.getNumSamples();

    if (numChannels <= 0 || sourceLength <= 0)
        return {};

    // The transparent default, taken before any of the arithmetic below so it is exact by
    // construction rather than by the rounding happening to land on sourceLength.
    if (isIdentity(params))
    {
        juce::AudioBuffer<float> unchanged(numChannels, sourceLength);
        for (int channel = 0; channel < numChannels; ++channel)
            unchanged.copyFrom(channel, 0, source, channel, 0, sourceLength);
        return unchanged;
    }

    const auto lengthFraction = juce::jlimit(0.0f, 1.0f, params.lengthPercent * 0.01f);
    const auto requested = (int) std::lround((double) sourceLength * (double) lengthFraction);
    const auto retained = juce::jlimit(std::min(minimumShapedSamples, sourceLength), sourceLength, requested);

    juce::AudioBuffer<float> shaped(numChannels, retained);
    for (int channel = 0; channel < numChannels; ++channel)
        shaped.copyFrom(channel, 0, source, channel, 0, retained);

    // Fade-out, only when something was actually cut off. At full length the IR's own ending is
    // already whatever it was recorded as, and imposing a fade there would quietly shorten every
    // IR in the set.
    if (retained < sourceLength)
    {
        const auto fadeFromMs = (int) std::lround(maxTruncationFadeMs * 0.001 * sampleRate);
        const auto fadeFromFraction = (int) std::lround((double) retained * (double) truncationFadeFraction);
        const auto fadeLength = std::max(1, std::min(fadeFromMs, fadeFromFraction));
        const auto fadeStart = retained - fadeLength;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = shaped.getWritePointer(channel);

            for (int i = 0; i < fadeLength; ++i)
            {
                // Mirrored: 1 at fadeStart falling to 0 at the final sample.
                const auto position = (float) (i + 1) / (float) fadeLength;
                samples[fadeStart + i] *= raisedCosine(1.0f - position);
            }
        }
    }

    // Attack fade-in. Clamped to half the retained length so a long attack on a short IR can never
    // consume the entire thing and leave silence.
    if (params.attackMs > 0.0f)
    {
        const auto requestedAttack = (int) std::lround((double) params.attackMs * 0.001 * sampleRate);
        const auto attackLength = juce::jlimit(0, std::max(1, retained / 2), requestedAttack);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = shaped.getWritePointer(channel);

            for (int i = 0; i < attackLength; ++i)
                samples[i] *= raisedCosine((float) i / (float) attackLength);
        }
    }

    return shaped;
}

std::vector<float> computePeakEnvelope(const juce::AudioBuffer<float>& buffer, int numPoints,
                                        int numSamplesOverride)
{
    if (numPoints <= 0)
        return {};

    std::vector<float> envelope((size_t) numPoints, 0.0f);

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = numSamplesOverride >= 0
                              ? std::min(numSamplesOverride, buffer.getNumSamples())
                              : buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return envelope;

    for (int point = 0; point < numPoints; ++point)
    {
        const auto start = (int) ((juce::int64) point * numSamples / numPoints);
        const auto end = (int) ((juce::int64) (point + 1) * numSamples / numPoints);

        // A point whose span rounds to zero samples (more points than samples) still reports the
        // one sample under it rather than a hole in the waveform.
        const auto spanEnd = std::max(end, start + 1);

        auto peak = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* samples = buffer.getReadPointer(channel);
            for (int i = start; i < spanEnd && i < numSamples; ++i)
                peak = std::max(peak, std::abs(samples[i]));
        }

        envelope[(size_t) point] = peak;
    }

    return envelope;
}

int findActiveLength(const juce::AudioBuffer<float>& buffer, double sampleRate,
                      float thresholdDb, double tailPaddingSeconds, float minimumFraction)
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() <= 0 || sampleRate <= 0.0)
        return numSamples;

    const auto peak = buffer.getMagnitude(0, numSamples);

    // Silent (or empty) buffer - nothing to trim towards, so don't pretend otherwise.
    if (peak <= 0.0f)
        return numSamples;

    const auto threshold = peak * juce::Decibels::decibelsToGain(thresholdDb);

    // ~5ms windows rather than sample-by-sample: fast, and immune to a single near-zero-crossing
    // sample mid-decay looking like silence when its neighbours plainly aren't.
    const auto windowSamples = std::max(1, (int) std::lround(sampleRate * 0.005));

    int lastActiveWindowEnd = 0;
    for (int start = 0; start < numSamples; start += windowSamples)
    {
        const auto length = std::min(windowSamples, numSamples - start);

        if (buffer.getMagnitude(start, length) >= threshold)
            lastActiveWindowEnd = start + length;
    }

    const auto padded = lastActiveWindowEnd + (int) std::lround(sampleRate * tailPaddingSeconds);
    const auto floor = (int) std::lround((double) numSamples * (double) minimumFraction);

    return juce::jlimit(std::max(floor, 1), numSamples, padded);
}

} // namespace wildjag::conv::IRShaper
