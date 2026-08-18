#pragma once

#include <vector>

// Hand-rolled circular buffer with fractional (linearly interpolated) read, used as the shared
// write history for GradientPitchShiftEngine's two independently-ramping read taps. Deliberately
// not juce::dsp::DelayLine - see the implementation plan for why (DelayLine's setDelay() clamps/
// asserts right at the zero-delay boundary, which is exactly where this algorithm's splices need
// to land).
class GradientDelayBuffer
{
public:
    // Allocates capacity - never call this on the audio thread (matches every other buffer-owning
    // class in this catalog, e.g. juce::dsp::DelayLine::setMaximumDelayInSamples()).
    void setSize(int numSamples);

    void reset() noexcept;

    // Writes one sample and advances the write head.
    void write(float sample) noexcept;

    // Reads a linearly-interpolated sample at the given delay (in samples, may be fractional)
    // behind the sample most recently passed to write(). Callers are responsible for keeping
    // delayInSamples within [0, capacity - 2] - GradientPitchShiftEngine's tap ramps stay in
    // range by construction, given the sizing rule in the implementation plan.
    float readInterpolated(float delayInSamples) const noexcept;

    int getSize() const noexcept { return (int) buffer.size(); }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};
