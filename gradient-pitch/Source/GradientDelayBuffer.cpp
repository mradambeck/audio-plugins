#include "GradientDelayBuffer.h"

#include <algorithm>
#include <cassert>
#include <cmath>

void GradientDelayBuffer::setSize(int numSamples)
{
    buffer.assign((size_t) std::max(numSamples, 1), 0.0f);
    writeIndex = 0;
}

void GradientDelayBuffer::reset() noexcept
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

void GradientDelayBuffer::write(float sample) noexcept
{
    buffer[(size_t) writeIndex] = sample;
    ++writeIndex;
    if (writeIndex >= (int) buffer.size())
        writeIndex = 0;
}

float GradientDelayBuffer::readInterpolated(float delayInSamples) const noexcept
{
    const auto size = (int) buffer.size();

    // Branch-based wrap, not true modulo: correct only within one buffer-length of [0, size), which
    // GradientPitchShiftEngine::requiredBufferCapacitySamples()'s sizing rule (capacity = the SUM of
    // every simultaneous-maximum excursion - delay + ramp window + drift + safety margin) guarantees
    // delayInSamples always stays within. The assert is a real safety net, not decoration: unlike %,
    // this is only correct under that invariant, so a future change that breaks it fails loudly in
    // debug builds instead of silently reading/writing out of bounds.
    auto wrapIndex = [size](int index) noexcept
    {
        assert(index > -size && index < 2 * size && "single-wrap invariant violated - see comment above");
        if (index < 0) index += size;
        else if (index >= size) index -= size;
        return index;
    };

    // The most-recently-written sample lives at (writeIndex - 1), not writeIndex itself - reading
    // "delayInSamples behind" walks back from there.
    const auto readPosition = (float) (writeIndex - 1) - delayInSamples;

    const auto floorPosition = std::floor(readPosition);
    const auto fraction = readPosition - floorPosition;

    const auto index0 = wrapIndex((int) floorPosition);
    const auto index1 = wrapIndex(index0 + 1);

    const auto sample0 = buffer[(size_t) index0];
    const auto sample1 = buffer[(size_t) index1];

    return sample0 + (sample1 - sample0) * fraction;
}
