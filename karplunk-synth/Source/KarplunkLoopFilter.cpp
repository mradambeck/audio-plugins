#include "KarplunkLoopFilter.h"

void TwoPointAverageLoopFilter::prepare(double) noexcept
{
    reset();
}

void TwoPointAverageLoopFilter::reset() noexcept
{
    prevInput = 0.0f;
}

void TwoPointAverageLoopFilter::setDamping(float amount01) noexcept
{
    loopGain = minLoopGain + amount01 * (maxLoopGain - minLoopGain);
}

float TwoPointAverageLoopFilter::processSample(float x) noexcept
{
    // Decay time here is inherently pitch-dependent at a fixed loopGain: a shorter delay length
    // (higher note) means more loop passes per second, so the same per-pass gain reduction adds
    // up to a faster decay in real time than a longer delay length (lower note) at the identical
    // damping setting. This is genuine Karplus-Strong physics, not something to normalise away in
    // this class - Jaffe/Smith's stretched-allpass technique is the named future loop-filter
    // swap-in for decoupling decay time from pitch, not a fix to apply here.
    const auto y = loopGain * 0.5f * (x + prevInput);
    prevInput = x;
    return y;
}
