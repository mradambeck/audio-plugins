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

    // Baselines the tilt gain High-offset curves in InhaltReferenceData.h are anchored to (their
    // own value AT High=0, matching AuraParameterMap.cpp's identical convention for its own
    // decay-gain and damping offset curves). tilt_low_gain/tilt_high_gain are direct measurements
    // (see build_measured_gate_curves.py) anchored at an exact, genuinely neutral 1.0.
    // plateau_droop_db_per_s needs no equivalent constant here: its own "Time curve"
    // (time_to_plateau_droop_db_per_sPoints) is already the absolute High=0 baseline value at
    // each Time, not a zero-anchored offset.
    constexpr float tiltLowGainBaselineAtHigh0 = 1.0f;
    constexpr float tiltHighGainBaselineAtHigh0 = 1.0f;

    // Fixed tilt pivot, NOT a fitted/High-dependent curve (see build_measured_gate_curves.py's own
    // module docstring for the full story). Originally kept from the fit (~4200-4700Hz), which
    // "looked physically plausible" but turned out to be structurally wrong: a real complaint
    // ("Bringing it down to -9dB to match a -9dB IR it is still much brighter and clear") led to
    // measuring the real captures' WHOLE-DECAY average spectrum (LTAS), not just the onset window
    // the tilt gains themselves were calibrated from - a ~4.5kHz pivot puts the one-pole shelf's
    // transition too close to the 6-16kHz band being darkened, capping the achievable low/high
    // spread at ~8.8dB EVEN AT EXTREME GAIN, well short of the real captures' own measured
    // 10-16dB spread at High=-4/-9. 1500Hz - an onset-band pivot ESTIMATE this catalog's own
    // earlier work flagged as "less precise" and discarded in favor of the fit's value - raises
    // the achievable ceiling enough to actually reach the real target. Verified directly (not
    // just reasoned): a bounded numerical search at this pivot reproduces the real captures' own
    // measured LTAS band levels at High=-4/-9 with a small residual; the same search at the old
    // ~4.5kHz pivot could not, no matter how extreme the gain.
    constexpr float tiltPivotHz = 1500.0f;

    // Direct/early-arrival tap gain (see InhaltIRSynth.h's own Params::directGain comment) - a
    // hand-measured constant, not fit-derived, matching tiltPivotHz's own convention. Calibrated
    // by rendering candidate values and matching the real captures' own measured
    // RMS[0-10ms]/RMS[40-50ms] ratio: 5 real captures measured 0.35-0.49 (H=0 clustering
    // 0.44-0.49; the one H=-9 capture measured 0.35, on its own not enough to justify a
    // High-dependent curve). 0.96 reproduces 0.46 at Time=4.8/High=0 (within the H=0 cluster) -
    // re-calibrated from an earlier 0.79 after the tank's own delay-line set changed (see
    // leftDelaysMs/rightDelaysMs's own comment in InhaltIRSynth.cpp), which shifted the tank's
    // own 40-50ms RMS level enough to need a slightly higher constant for the same real-capture
    // ratio target. The High=-9 residual gap this constant can't close (real ~0.35 vs whatever
    // this value reaches there) remains documented in README.md.
    constexpr float directGainConstant = 0.96f;
} // namespace

GateParams mapTimeAndHighToGateParams(float timeKnob, float highKnob, bool* extrapolated) noexcept
{
    static const auto timeToTauA = toCurve(wildjag::dsp::time_to_tau_a_msPoints);
    static const auto timeToKnee = toCurve(wildjag::dsp::time_to_t_knee_msPoints);
    static const auto highToKneeOffset = toCurve(wildjag::dsp::high_to_t_knee_ms_offsetPoints);
    static const auto timeToFallRate = toCurve(wildjag::dsp::time_to_fall_rate_db_per_sPoints);
    static const auto timeToKneeSoftness = toCurve(wildjag::dsp::time_to_tau_k_msPoints);
    static const auto timeToDroopBaseline = toCurve(wildjag::dsp::time_to_plateau_droop_db_per_sPoints);
    static const auto highToDroopOffset = toCurve(wildjag::dsp::high_to_plateau_droop_db_per_s_offsetPoints);

    bool tauAExtrapolated = false, kneeTimeExtrapolated = false, kneeHighExtrapolated = false,
         fallExtrapolated = false, softnessExtrapolated = false,
         droopTimeExtrapolated = false, droopHighExtrapolated = false;

    GateParams params;
    params.buildUpMs = timeToTauA.evaluate(timeKnob, &tauAExtrapolated);
    // NOW combined with high_to_t_knee_ms_offsetPoints - a first attempt at this (Time-only
    // baseline + offset, both taken at face value from their own separate measurements) was tried
    // and REVERTED after it made the two short-Time captures dramatically WORSE (knee error
    // ~2-4ms -> ~91-93ms): a real, ear-caught "reverb rings out too long at High=0" complaint led
    // to tracing that regression to its actual root cause - the Time-only baseline was never a
    // clean High=0 reference to begin with (Time=0.1s/0.8s only exist at High=-3 in the real
    // capture set, so their raw H=-3 knee_time_ms was being pooled straight into the "Time-only"
    // curve), so adding a further High-dependent offset on top double-counted that capture's own
    // High=-3 effect. build_measured_gate_curves.py's own _build_t_knee_ms_curves now converts
    // EVERY capture to an H=0-equivalent value (subtracting this same offset curve) before
    // building the Time baseline, so the two are self-consistent and can be safely combined here.
    params.kneeTimeMs = timeToKnee.evaluate(timeKnob, &kneeTimeExtrapolated)
        + highToKneeOffset.evaluate(highKnob, &kneeHighExtrapolated);
    params.fallRateDbPerSec = timeToFallRate.evaluate(timeKnob, &fallExtrapolated);
    params.kneeSoftnessMs = timeToKneeSoftness.evaluate(timeKnob, &softnessExtrapolated);
    // plateauDroopDbPerSec = time_to_plateau_droop_db_per_s(Time) [High=0 baseline, already
    // absolute - see time_to_plateau_droop_db_per_sPoints' own values] + high offset (zero-
    // anchored at High=0, per findings.md's "High: timing-NEUTRAL overall, but NOT damping-
    // neutral" finding).
    params.plateauDroopDbPerSec = timeToDroopBaseline.evaluate(timeKnob, &droopTimeExtrapolated)
        + highToDroopOffset.evaluate(highKnob, &droopHighExtrapolated);

    if (extrapolated != nullptr)
        *extrapolated = tauAExtrapolated || kneeTimeExtrapolated || kneeHighExtrapolated
            || fallExtrapolated || softnessExtrapolated || droopTimeExtrapolated || droopHighExtrapolated;
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
    static const auto timeToDiffuserGain = toCurve(wildjag::dsp::time_to_diffuser_gainPoints);

    bool gainExtrapolated = false, dampingExtrapolated = false, diffuserExtrapolated = false;
    TankParams params;
    params.feedbackGain = timeToFeedbackGain.evaluate(timeKnob, &gainExtrapolated);
    params.dampingWeight = timeToDamping.evaluate(timeKnob, &dampingExtrapolated);
    params.diffuserGain = timeToDiffuserGain.evaluate(timeKnob, &diffuserExtrapolated);
    params.directGain = directGainConstant;
    if (extrapolated != nullptr)
        *extrapolated = gainExtrapolated || dampingExtrapolated || diffuserExtrapolated;
    return params;
}

TiltParams mapHighKnobToTilt(float highKnob, bool* extrapolated) noexcept
{
    static const auto highToLowGainOffset = toCurve(wildjag::dsp::high_to_tilt_low_gain_offsetPoints);
    static const auto highToHighGainOffset = toCurve(wildjag::dsp::high_to_tilt_high_gain_offsetPoints);

    bool lowExtrapolated = false, highExtrapolated = false;
    TiltParams params;
    params.lowGain = tiltLowGainBaselineAtHigh0 + highToLowGainOffset.evaluate(highKnob, &lowExtrapolated);
    params.highGain = tiltHighGainBaselineAtHigh0 + highToHighGainOffset.evaluate(highKnob, &highExtrapolated);
    params.pivotHz = tiltPivotHz;
    if (extrapolated != nullptr)
        *extrapolated = lowExtrapolated || highExtrapolated;
    return params;
}

} // namespace InhaltParameterMap
