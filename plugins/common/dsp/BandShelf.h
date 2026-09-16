#pragma once

#include "OnePoleFilter.h"

// Independent low/high-band gain shelf, one-pole split. Unlike TiltFilter.h (which always moves
// its two bands by equal-and-opposite dB amounts around a center), this takes two INDEPENDENT
// linear gains - needed whenever a fitted or measured tilt is genuinely asymmetric (e.g. Aura's
// AmbienceFDN fit produced independent high_band_gain/low_band_gain values, and Inhalt's measured
// H tilt is +4.2dB low / -9.1dB high, not symmetric around any single pivot).
//
// Promoted out of AuraFDNEngine.h (where it started as a private struct) once Inhalt needed the
// identical shape for its own input-stage tilt - per AGENTS.md's LookAndFeel-extension
// convention applied the same way to common/dsp/: a shared capability moves to shared code once a
// SECOND plugin needs it, not speculatively for the first one that does.
namespace wildjag::dsp
{

struct BandShelf
{
    OnePoleFilter lowpass;
    float lowGain = 1.0f;
    float highGain = 1.0f;

    void reset() noexcept { lowpass.reset(); }
    void setPivotHz(float hz, double sampleRate) noexcept { lowpass.setCutoffHz(hz, sampleRate); }

    float processSample(float x) noexcept
    {
        const auto low = lowpass.processSample(x);
        const auto high = x - low;
        return low * lowGain + high * highGain;
    }
};

} // namespace wildjag::dsp
