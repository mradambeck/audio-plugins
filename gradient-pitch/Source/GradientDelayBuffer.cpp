#include "GradientDelayBuffer.h"

#include <algorithm>
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
    writeIndex = (writeIndex + 1) % (int) buffer.size();
}

float GradientDelayBuffer::readInterpolated(float delayInSamples) const noexcept
{
    const auto size = (int) buffer.size();

    auto wrapIndex = [size](int index) noexcept
    {
        index %= size;
        return index < 0 ? index + size : index;
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
