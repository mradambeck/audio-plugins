#pragma once

#include <cassert>
#include <cmath>
#include <vector>

namespace wildjag::dsp
{

// Hand-rolled circular buffer with exact-integer and fractional (linearly interpolated) read.
// Shared across plugins that each used to hand-roll their own version of this - originally
// gradient-pitch's GradientDelayBuffer (fractional read only). Deliberately not
// juce::dsp::DelayLine - DelayLine's setDelay() clamps/asserts right at the zero-delay boundary,
// which is exactly where some callers' splices need to land.
class CircularDelayBuffer
{
public:
    // Allocates capacity - never call this on the audio thread (matches every other buffer-owning
    // class in this catalog, e.g. juce::dsp::DelayLine::setMaximumDelayInSamples()).
    void setSize(int numSamples)
    {
        buffer.assign((size_t) std::max(numSamples, 1), 0.0f);
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    // Writes one sample and advances the write head.
    void write(float sample) noexcept
    {
        buffer[(size_t) writeIndex] = sample;
        ++writeIndex;
        if (writeIndex >= (int) buffer.size())
            writeIndex = 0;
    }

    // Reads the exact sample `delaySamples` behind the most recently written one (delaySamples=0
    // returns the sample just written). Callers are responsible for keeping delaySamples within
    // [0, capacity - 1].
    float read(int delaySamples) const noexcept
    {
        const auto size = (int) buffer.size();
        assert(delaySamples >= 0 && delaySamples < size);
        auto index = writeIndex - 1 - delaySamples;
        if (index < 0)
            index += size;
        return buffer[(size_t) index];
    }

    // Reads a linearly-interpolated sample at the given delay (in samples, may be fractional)
    // behind the sample most recently passed to write(). Callers are responsible for keeping
    // delayInSamples within [0, capacity - 2].
    float readInterpolated(float delayInSamples) const noexcept
    {
        const auto size = (int) buffer.size();

        // Branch-based wrap, not true modulo: correct only within one buffer-length of [0, size).
        // Callers must keep delayInSamples within that range (see requiredBufferCapacitySamples-
        // style sizing rules at each call site). The assert is a real safety net, not decoration:
        // unlike %, this is only correct under that invariant, so a caller change that breaks it
        // fails loudly in debug builds instead of silently reading/writing out of bounds.
        auto wrapIndex = [size](int index) noexcept
        {
            assert(index > -size && index < 2 * size && "single-wrap invariant violated - see comment above");
            if (index < 0) index += size;
            else if (index >= size) index -= size;
            return index;
        };

        // The most-recently-written sample lives at (writeIndex - 1), not writeIndex itself -
        // reading "delayInSamples behind" walks back from there.
        const auto readPosition = (float) (writeIndex - 1) - delayInSamples;

        const auto floorPosition = std::floor(readPosition);
        const auto fraction = readPosition - floorPosition;

        const auto index0 = wrapIndex((int) floorPosition);
        const auto index1 = wrapIndex(index0 + 1);

        const auto sample0 = buffer[(size_t) index0];
        const auto sample1 = buffer[(size_t) index1];

        return sample0 + (sample1 - sample0) * fraction;
    }

    int getSize() const noexcept { return (int) buffer.size(); }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};

} // namespace wildjag::dsp
