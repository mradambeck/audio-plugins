#pragma once

#include <cmath>

namespace wildjag::dsp
{

// One-pole leaky-integrator lowpass: state += weight * (x - state); return state. weight in
// [0, 1] - 0 leaves state frozen (infinite memory), 1 makes it track x instantly (no filtering).
// Shared across plugins that each hand-derived the same "coeff = 1 - exp(-2*pi*fc/fs)" formula
// separately (originally gradient-pitch's darkening/drift filters and shields-reverb's per-line
// FDN damping and output bandwidth limiter).
class OnePoleFilter
{
public:
    void reset(float value = 0.0f) noexcept { state = value; }

    // Sets the update weight directly (0-1). Use this for a filter driven by a raw 0-1 knob value
    // rather than a frequency (e.g. an FDN damping control where the knob value itself is the leak).
    void setWeight(float weight01) noexcept { weight = weight01; }

    // Sets the weight from a target -3dB cutoff frequency: weight = 1 - exp(-2*pi*fc/fs).
    void setCutoffHz(float cutoffHz, double sampleRateHz) noexcept
    {
        constexpr float pi = 3.14159265358979323846f;
        weight = 1.0f - std::exp(-2.0f * pi * cutoffHz / (float) sampleRateHz);
    }

    float processSample(float x) noexcept
    {
        state += weight * (x - state);
        return state;
    }

    float getState() const noexcept { return state; }

private:
    float weight = 1.0f;
    float state = 0.0f;
};

} // namespace wildjag::dsp
