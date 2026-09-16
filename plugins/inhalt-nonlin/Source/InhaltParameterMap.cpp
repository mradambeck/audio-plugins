#include "InhaltParameterMap.h"

namespace InhaltParameterMap
{

namespace
{
    // Real hand-measured -20dB gate length (ms) per Time-knob label, from direct inspection of a
    // real NonLin capture set (see the project plan's "already measured" section) - NOT a
    // placeholder. gate_envelope_params()'s own gate_length_ms_at_20db must reproduce this table
    // once Phase 2 runs for real; InhaltParameterMapTests.cpp spot-checks the same numbers here.
    const wildjag::dsp::FittedCurve1D<6> gateLengthMsCurve { { {
        { 0.1f, 102.5f },
        { 0.8f, 102.5f },
        { 2.2f, 149.3f },
        { 4.8f, 216.6f },
        { 7.0f, 278.1f },
        { 9.8f, 300.4f },
    } } };

    // PLACEHOLDER internal gate-shape breakdown, derived from gateLengthMsCurve alone (not a real
    // per-parameter measurement) so the plugin has SOME reasonable envelope shape at every Time
    // setting rather than a flat default. buildUpMs fixed; kneeTimeMs set so that (roughly) the
    // knee-to-floor fall reaches -20dB at the SAME time gateLengthMsCurve says the real hardware
    // does, given a fixed -250dB/s fall rate. Replace with the real fitted/measured breakdown
    // once ml-toolkit's Phase 2 runs on real captures.
    constexpr float placeholderFallRateDbPerSec = -250.0f;
    constexpr float placeholderBuildUpMs = 3.0f;
    constexpr float placeholderKneeSoftnessMs = 4.0f;

    // High=0 (neutral, no coloration) vs High=-9 (+4.2dB low-band, -9.1dB high-band, pivot
    // ~1-2kHz) - REAL measurement, converted from dB to the linear gains InhaltIRSynth's
    // BandShelf takes. Only the two range endpoints are measured; everything between is a linear
    // interpolation, not a separate measurement.
    const wildjag::dsp::FittedCurve1D<2> tiltLowGainCurve { { {
        { -9.0f, 1.62181f },
        { 0.0f, 1.0f },
    } } };
    const wildjag::dsp::FittedCurve1D<2> tiltHighGainCurve { { {
        { -9.0f, 0.350752f },
        { 0.0f, 1.0f },
    } } };
    constexpr float measuredTiltPivotHz = 1500.0f; // "pivot ~1-2kHz" - midpoint, not curve-fit
} // namespace

GateParams mapTimeKnobToGateParams(float timeKnob, bool* extrapolated) noexcept
{
    const auto gateLengthMs = gateLengthMsCurve.evaluate(timeKnob, extrapolated);

    GateParams params;
    params.buildUpMs = placeholderBuildUpMs;
    params.fallRateDbPerSec = placeholderFallRateDbPerSec;
    params.kneeSoftnessMs = placeholderKneeSoftnessMs;
    params.plateauDroopDbPerSec = 0.0f;
    // -20dB point sits kneeSoftnessMs-ish past the knee itself (softplus's own transition width),
    // so back-solve kneeTimeMs from the measured -20dB gate length and the fixed fall rate:
    // gateLengthMs ~= kneeTimeMs + 20dB / |fallRateDbPerSec| * 1000.
    params.kneeTimeMs = gateLengthMs - (20.0f / -placeholderFallRateDbPerSec) * 1000.0f;
    if (params.kneeTimeMs < 1.0f)
        params.kneeTimeMs = 1.0f;
    return params;
}

float gateLengthMsForDisplay(float timeKnob, bool* extrapolated) noexcept
{
    return gateLengthMsCurve.evaluate(timeKnob, extrapolated);
}

TankParams mapTimeKnobToTankParams(float timeKnob, bool* extrapolated) noexcept
{
    // Time-only (not High-dependent - see this file's header comment), and currently a flat
    // placeholder constant rather than a real Time-dependent curve: no direct measurement yet of
    // whether the tank's own density/damping should vary with Time at all, versus the gate
    // envelope doing all the audible shaping (per model.py's central design split). Returns a
    // constant regardless of timeKnob's value, so `extrapolated` is always false here - honestly
    // reflects that this isn't measuring anything Time-dependent yet, not a bug to fix later.
    (void) timeKnob;
    if (extrapolated != nullptr)
        *extrapolated = false;
    return TankParams {};
}

TiltParams mapHighKnobToTilt(float highKnob, bool* extrapolated) noexcept
{
    bool lowExtrapolated = false, highExtrapolated = false;
    TiltParams params;
    params.lowGain = tiltLowGainCurve.evaluate(highKnob, &lowExtrapolated);
    params.highGain = tiltHighGainCurve.evaluate(highKnob, &highExtrapolated);
    params.pivotHz = measuredTiltPivotHz;
    if (extrapolated != nullptr)
        *extrapolated = lowExtrapolated || highExtrapolated;
    return params;
}

} // namespace InhaltParameterMap
