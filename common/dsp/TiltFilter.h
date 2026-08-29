#pragma once

#include "OnePoleFilter.h"

#include <cmath>

namespace wildjag::dsp
{

// One-pole tilt/shelf EQ: splits the signal into a low band (one-pole lowpass at the pivot
// frequency) and a high band (the remainder), then recombines them with independent gains. A
// positive tilt brightens (boosts high, cuts low); negative darkens (boosts low, cuts high) -
// unlike a plain lowpass, both bands move, in opposite directions, around the pivot. Built for
// intruder-gated-reverb's H parameter, whose measured effect (see analysis/findings.md) is a
// bass-up/treble-down tilt pivoting around ~1-4kHz, not a treble-only damping curve.
class TiltFilter
{
public:
    void reset() noexcept { lowpass.reset(); }

    void setPivotHz(float hz, double sampleRateHz) noexcept
    {
        lowpass.setCutoffHz(hz, sampleRateHz);
    }

    // tiltDb > 0 brightens (high band up, low band down by the same amount); < 0 darkens.
    // halfRangeDb controls how much gain swing a given tiltDb produces in each band.
    void setTiltDb(float tiltDb, float halfRangeDb = 6.0f) noexcept
    {
        const auto highGainDb = tiltDb * (halfRangeDb / 6.0f);
        const auto lowGainDb = -highGainDb;
        highGain = std::pow(10.0f, highGainDb / 20.0f);
        lowGain = std::pow(10.0f, lowGainDb / 20.0f);
    }

    float processSample(float x) noexcept
    {
        const auto low = lowpass.processSample(x);
        const auto high = x - low;
        return low * lowGain + high * highGain;
    }

private:
    OnePoleFilter lowpass;
    float lowGain = 1.0f;
    float highGain = 1.0f;
};

} // namespace wildjag::dsp
