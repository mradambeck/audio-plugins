#include "InhaltParameterMap.h"
#include "InhaltReferenceData.h"

namespace InhaltParameterMap
{

namespace
{
    template <std::size_t N>
    wildjag::dsp::FittedCurve1D<N> toCurve(const std::array<wildjag::dsp::FittedPoint, N>& src)
    {
        std::array<typename wildjag::dsp::FittedCurve1D<N>::Point, N> pts;
        for (std::size_t i = 0; i < N; ++i)
            pts[i] = { src[i].x, src[i].y };
        return wildjag::dsp::FittedCurve1D<N>(pts);
    }

    // Real hand-measured -20dB gate length (ms) per Time-knob label, from direct inspection of a
    // real NonLin capture set (see effects/nonlin/findings.md) - kept as its own small table
    // (not derived from InhaltReferenceData.h's t_knee_ms curve) because it's a genuinely
    // independent measurement of a different, coarser quantity, used only for the editor's
    // display readout. gate_envelope_params()'s own gate_length_ms_at_20db reproduced this table
    // within a few ms when run for real - see effects/nonlin/analyze.py's own automatic check.
    const wildjag::dsp::FittedCurve1D<6> gateLengthMsCurve { { {
        { 0.1f, 102.5f },
        { 0.8f, 102.5f },
        { 2.2f, 149.3f },
        { 4.8f, 216.6f },
        { 7.0f, 278.1f },
        { 9.8f, 300.4f },
    } } };

    // Baselines the tilt High-offset curves in InhaltReferenceData.h are anchored to (their own
    // value AT High=0, matching AuraParameterMap.cpp's identical convention for its own decay-
    // gain and damping offset curves). tilt_low_gain/tilt_high_gain are direct measurements
    // (see build_measured_gate_curves.py) anchored at an exact, genuinely neutral 1.0 - unlike
    // tilt_pivot_hz, still from the fit, whose own baseline is read directly from
    // ml-toolkit/effects/nonlin/curves.json's "baseline_at_high0" field. plateau_droop_db_per_s
    // needs no equivalent constant here: its own "Time curve" (time_to_plateau_droop_db_per_sPoints)
    // is already the absolute High=0 baseline value at each Time, not a zero-anchored offset.
    constexpr float tiltLowGainBaselineAtHigh0 = 1.0f;
    constexpr float tiltHighGainBaselineAtHigh0 = 1.0f;
    constexpr float tiltPivotHzBaselineAtHigh0 = 4473.755859375f;
} // namespace

GateParams mapTimeAndHighToGateParams(float timeKnob, float highKnob, bool* extrapolated) noexcept
{
    static const auto timeToTauA = toCurve(wildjag::dsp::time_to_tau_a_msPoints);
    static const auto timeToKnee = toCurve(wildjag::dsp::time_to_t_knee_msPoints);
    static const auto timeToFallRate = toCurve(wildjag::dsp::time_to_fall_rate_db_per_sPoints);
    static const auto timeToKneeSoftness = toCurve(wildjag::dsp::time_to_tau_k_msPoints);
    static const auto timeToDroopBaseline = toCurve(wildjag::dsp::time_to_plateau_droop_db_per_sPoints);
    static const auto highToDroopOffset = toCurve(wildjag::dsp::high_to_plateau_droop_db_per_s_offsetPoints);

    bool tauAExtrapolated = false, kneeExtrapolated = false, fallExtrapolated = false,
         softnessExtrapolated = false, droopTimeExtrapolated = false, droopHighExtrapolated = false;

    GateParams params;
    params.buildUpMs = timeToTauA.evaluate(timeKnob, &tauAExtrapolated);
    params.kneeTimeMs = timeToKnee.evaluate(timeKnob, &kneeExtrapolated);
    params.fallRateDbPerSec = timeToFallRate.evaluate(timeKnob, &fallExtrapolated);
    params.kneeSoftnessMs = timeToKneeSoftness.evaluate(timeKnob, &softnessExtrapolated);
    // plateauDroopDbPerSec = time_to_plateau_droop_db_per_s(Time) [High=0 baseline, already
    // absolute - see time_to_plateau_droop_db_per_sPoints' own values] + high offset (zero-
    // anchored at High=0, per findings.md's "High: timing-NEUTRAL overall, but NOT damping-
    // neutral" finding).
    params.plateauDroopDbPerSec = timeToDroopBaseline.evaluate(timeKnob, &droopTimeExtrapolated)
        + highToDroopOffset.evaluate(highKnob, &droopHighExtrapolated);

    if (extrapolated != nullptr)
        *extrapolated = tauAExtrapolated || kneeExtrapolated || fallExtrapolated
            || softnessExtrapolated || droopTimeExtrapolated || droopHighExtrapolated;
    return params;
}

float gateLengthMsForDisplay(float timeKnob, bool* extrapolated) noexcept
{
    return gateLengthMsCurve.evaluate(timeKnob, extrapolated);
}

TankParams mapTimeKnobToTankParams(float timeKnob, bool* extrapolated) noexcept
{
    static const auto timeToFeedbackGain = toCurve(wildjag::dsp::time_to_feedback_gainPoints);
    static const auto timeToDamping = toCurve(wildjag::dsp::time_to_damping_weight_meanPoints);

    bool gainExtrapolated = false, dampingExtrapolated = false;
    TankParams params;
    params.feedbackGain = timeToFeedbackGain.evaluate(timeKnob, &gainExtrapolated);
    params.dampingWeight = timeToDamping.evaluate(timeKnob, &dampingExtrapolated);
    if (extrapolated != nullptr)
        *extrapolated = gainExtrapolated || dampingExtrapolated;
    return params;
}

TiltParams mapHighKnobToTilt(float highKnob, bool* extrapolated) noexcept
{
    static const auto highToLowGainOffset = toCurve(wildjag::dsp::high_to_tilt_low_gain_offsetPoints);
    static const auto highToHighGainOffset = toCurve(wildjag::dsp::high_to_tilt_high_gain_offsetPoints);
    static const auto highToPivotOffset = toCurve(wildjag::dsp::high_to_tilt_pivot_hz_offsetPoints);

    bool lowExtrapolated = false, highExtrapolated = false, pivotExtrapolated = false;
    TiltParams params;
    params.lowGain = tiltLowGainBaselineAtHigh0 + highToLowGainOffset.evaluate(highKnob, &lowExtrapolated);
    params.highGain = tiltHighGainBaselineAtHigh0 + highToHighGainOffset.evaluate(highKnob, &highExtrapolated);
    params.pivotHz = tiltPivotHzBaselineAtHigh0 + highToPivotOffset.evaluate(highKnob, &pivotExtrapolated);
    if (extrapolated != nullptr)
        *extrapolated = lowExtrapolated || highExtrapolated || pivotExtrapolated;
    return params;
}

} // namespace InhaltParameterMap
