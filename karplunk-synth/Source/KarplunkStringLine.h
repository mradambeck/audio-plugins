#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// The Delay Tuning seam: fractional-delay/interpolation logic, isolated so it can be swapped
// without touching Excitation or the Loop Filter.
//
// IMPORTANT, found empirically while building this class (see its test file): this is a
// hand-rolled ring buffer, deliberately NOT a wrapper around juce::dsp::DelayLine, despite
// DelayLine being proven elsewhere in this catalog for a per-sample-inside-a-feedback-loop use
// case (caverns-delay's delayLineL/R). The difference is priming: Karplus-Strong's noteOn needs
// to seed the line with a whole burst of excitation samples *before* any of them are read back,
// then read that seeded content starting from the very next tick. juce::dsp::DelayLine's
// popSample() is a strictly causal, state-consuming read - its internal read pointer only
// advances via popSample() calls, completely independent of how many samples have been pushed,
// so a write()-only seed phase leaves that pointer frozen while the write pointer races ahead,
// silently corrupting the effective delay length the instant real read/write ticks begin (there
// is no way to "pre-load then read back later" through DelayLine's public API - every pushed
// sample must be popped exactly once, in the order it was pushed, or the delay amount drifts).
// This class's read() has no such state: it's a pure function of the current write position, so
// priming is just repeated write() calls, exactly matching Gradient's own GradientDelayBuffer
// (which independently avoided DelayLine for a related but different reason - see that class's
// own comment).
//
// InterpolationType is the compile-time swap point (Interpolator::interpolate(...)): the base
// scaffold's LinearInterpolator matches GradientDelayBuffer's exact proven math. A future allpass
// (Thiran-style) interpolator would need to carry its own persistent state between calls (unlike
// Linear/Lagrange, which are pure functions of the buffer content) - that's a real, flagged
// difference from this seam's current shape, not a drop-in swap.
struct LinearInterpolator
{
    static float interpolate(const std::vector<float>& buffer, int writeIndex,
                              float delayInSamples) noexcept
    {
        const auto size = (int) buffer.size();

        auto wrapIndex = [size](int index) noexcept
        {
            index %= size;
            return index < 0 ? index + size : index;
        };

        // The most-recently-written sample lives at (writeIndex - 1), not writeIndex itself.
        const auto readPosition = (float) (writeIndex - 1) - delayInSamples;

        const auto floorPosition = std::floor(readPosition);
        const auto fraction = readPosition - floorPosition;

        const auto index0 = wrapIndex((int) floorPosition);
        const auto index1 = wrapIndex(index0 + 1);

        const auto sample0 = buffer[(size_t) index0];
        const auto sample1 = buffer[(size_t) index1];

        return sample0 + (sample1 - sample0) * fraction;
    }
};

template <typename Interpolator = LinearInterpolator>
class KarplunkStringLine
{
public:
    // Allocates - only ever call this from SingleLineKarplunkVoice::prepare(), never from the
    // audio thread. maxDelaySamples should come from
    // SingleLineKarplunkVoice::requiredCapacitySamples() - never resized after this call.
    void prepare(double, int maxDelaySamples) noexcept
    {
        buffer.assign((size_t) std::max(maxDelaySamples, 1), 0.0f);
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void setDelaySamples(float delaySamples) noexcept
    {
        delayInSamples = delaySamples;
    }

    void write(float sample) noexcept
    {
        buffer[(size_t) writeIndex] = sample;
        writeIndex = (writeIndex + 1) % (int) buffer.size();
    }

    // read() is a pure function of the current write position and the delay set via
    // setDelaySamples() - calling it repeatedly with no intervening write() gives the same
    // result each time, unlike a causal/consuming read. This is exactly what makes priming safe:
    // there is no separate "priming" method - noteOn() just calls write() for every seed sample,
    // the same method used every render tick afterward.
    float read() const noexcept
    {
        return Interpolator::interpolate(buffer, writeIndex, delayInSamples);
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
    float delayInSamples = 0.0f;
};
