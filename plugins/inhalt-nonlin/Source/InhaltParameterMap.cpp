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
    static const auto timeToKneeSoftness = toCurve(wildjag::dsp::time_to_tau_k_msPoints);
    // Per-band knee time - REPLACED a single shared time_to_t_knee_ms/high_to_t_knee_ms_offset
    // pair (see InhaltIRSynth.h's own comment on Params::kneeTimeSubLowMs and
    // build_measured_gate_curves.py's own _build_per_band_knee_time_curves docstring for the full
    // story - the "knee lands at the same time in every band" assumption this shared pair was
    // originally built on turned out to be false when actually checked). Same
    // H0-equivalent-baseline-plus-offset combination as every other gate parameter here.
    static const auto timeToKneeSubLow = toCurve(wildjag::dsp::time_to_knee_time_subLow_msPoints);
    static const auto highToKneeSubLowOffset = toCurve(wildjag::dsp::high_to_knee_time_subLow_ms_offsetPoints);
    static const auto timeToKneeLow = toCurve(wildjag::dsp::time_to_knee_time_low_msPoints);
    static const auto highToKneeLowOffset = toCurve(wildjag::dsp::high_to_knee_time_low_ms_offsetPoints);
    static const auto timeToKneeMid = toCurve(wildjag::dsp::time_to_knee_time_mid_msPoints);
    static const auto highToKneeMidOffset = toCurve(wildjag::dsp::high_to_knee_time_mid_ms_offsetPoints);
    static const auto timeToKneeHigh = toCurve(wildjag::dsp::time_to_knee_time_high_msPoints);
    static const auto highToKneeHighOffset = toCurve(wildjag::dsp::high_to_knee_time_high_ms_offsetPoints);
    static const auto timeToEarlyExcess = toCurve(wildjag::dsp::time_to_early_excess_dbPoints);
    static const auto highToEarlyExcessOffset = toCurve(wildjag::dsp::high_to_early_excess_db_offsetPoints);

    // Per-band decay curves - REPLACED a single broadband time_to_plateau_droop_db_per_s/
    // time_to_fall_rate_db_per_s pair (see InhaltIRSynth.h's own comment on
    // Params::plateauDroopLowDbPerSec and build_measured_gate_curves.py's own
    // _build_per_band_gate_curves docstring for the full story - one broadband rate structurally
    // cannot represent real hardware's own per-band decay variation, and no tank dampingWeight
    // value can reproduce it either). Same H0-equivalent-baseline-plus-offset combination as
    // every other gate parameter here, just three times over.
    static const auto timeToDroopSubLow = toCurve(wildjag::dsp::time_to_plateau_droop_subLow_db_per_sPoints);
    static const auto highToDroopSubLowOffset = toCurve(wildjag::dsp::high_to_plateau_droop_subLow_db_per_s_offsetPoints);
    static const auto timeToDroopLow = toCurve(wildjag::dsp::time_to_plateau_droop_low_db_per_sPoints);
    static const auto highToDroopLowOffset = toCurve(wildjag::dsp::high_to_plateau_droop_low_db_per_s_offsetPoints);
    static const auto timeToDroopMid = toCurve(wildjag::dsp::time_to_plateau_droop_mid_db_per_sPoints);
    static const auto highToDroopMidOffset = toCurve(wildjag::dsp::high_to_plateau_droop_mid_db_per_s_offsetPoints);
    static const auto timeToDroopHigh = toCurve(wildjag::dsp::time_to_plateau_droop_high_db_per_sPoints);
    static const auto highToDroopHighOffset = toCurve(wildjag::dsp::high_to_plateau_droop_high_db_per_s_offsetPoints);
    static const auto timeToFallSubLow = toCurve(wildjag::dsp::time_to_fall_rate_subLow_db_per_sPoints);
    static const auto highToFallSubLowOffset = toCurve(wildjag::dsp::high_to_fall_rate_subLow_db_per_s_offsetPoints);
    static const auto timeToFallLow = toCurve(wildjag::dsp::time_to_fall_rate_low_db_per_sPoints);
    static const auto highToFallLowOffset = toCurve(wildjag::dsp::high_to_fall_rate_low_db_per_s_offsetPoints);
    static const auto timeToFallMid = toCurve(wildjag::dsp::time_to_fall_rate_mid_db_per_sPoints);
    static const auto highToFallMidOffset = toCurve(wildjag::dsp::high_to_fall_rate_mid_db_per_s_offsetPoints);
    static const auto timeToFallHigh = toCurve(wildjag::dsp::time_to_fall_rate_high_db_per_sPoints);
    static const auto highToFallHighOffset = toCurve(wildjag::dsp::high_to_fall_rate_high_db_per_s_offsetPoints);

    bool tauAExtrapolated = false,
         softnessExtrapolated = false,
         earlyExcessTimeExtrapolated = false, earlyExcessHighExtrapolated = false,
         droopSubLowTimeExtrapolated = false, droopSubLowHighExtrapolated = false,
         droopLowTimeExtrapolated = false, droopLowHighExtrapolated = false,
         droopMidTimeExtrapolated = false, droopMidHighExtrapolated = false,
         droopHighTimeExtrapolated = false, droopHighHighExtrapolated = false,
         fallSubLowTimeExtrapolated = false, fallSubLowHighExtrapolated = false,
         fallLowTimeExtrapolated = false, fallLowHighExtrapolated = false,
         fallMidTimeExtrapolated = false, fallMidHighExtrapolated = false,
         fallHighTimeExtrapolated = false, fallHighHighExtrapolated = false,
         kneeSubLowTimeExtrapolated = false, kneeSubLowHighExtrapolated = false,
         kneeLowTimeExtrapolated = false, kneeLowHighExtrapolated = false,
         kneeMidTimeExtrapolated = false, kneeMidHighExtrapolated = false,
         kneeHighTimeExtrapolated = false, kneeHighHighExtrapolated = false;

    GateParams params;
    params.buildUpMs = timeToTauA.evaluate(timeKnob, &tauAExtrapolated);
    params.kneeSoftnessMs = timeToKneeSoftness.evaluate(timeKnob, &softnessExtrapolated);
    // Per-band knee time - REPLACES a single shared kneeTimeMs (see InhaltIRSynth.h's own comment
    // on Params::kneeTimeSubLowMs for the "knee lands at the same time in every band" assumption
    // this was built on turning out false). Baseline+offset combine safely here for the SAME
    // reason the old shared version's own comment explained: build_measured_gate_curves.py's own
    // _build_per_band_knee_time_curves converts EVERY capture to an H=0-equivalent value first,
    // so the two are self-consistent by construction, just once per band now.
    params.kneeTimeSubLowMs = timeToKneeSubLow.evaluate(timeKnob, &kneeSubLowTimeExtrapolated)
        + highToKneeSubLowOffset.evaluate(highKnob, &kneeSubLowHighExtrapolated);
    params.kneeTimeLowMs = timeToKneeLow.evaluate(timeKnob, &kneeLowTimeExtrapolated)
        + highToKneeLowOffset.evaluate(highKnob, &kneeLowHighExtrapolated);
    params.kneeTimeMidMs = timeToKneeMid.evaluate(timeKnob, &kneeMidTimeExtrapolated)
        + highToKneeMidOffset.evaluate(highKnob, &kneeMidHighExtrapolated);
    params.kneeTimeHighMs = timeToKneeHigh.evaluate(timeKnob, &kneeHighTimeExtrapolated)
        + highToKneeHighOffset.evaluate(highKnob, &kneeHighHighExtrapolated);
    // Per-band droop/fall - each pair follows the SAME H0-equivalent-baseline-plus-offset
    // combination as t_knee_ms/fall_rate_db_per_s above (High offset added on top of a Time
    // baseline that was itself built by converting every capture to an H0-equivalent value first
    // - see _build_per_band_gate_curves' own docstring), just once per band.
    params.plateauDroopSubLowDbPerSec = timeToDroopSubLow.evaluate(timeKnob, &droopSubLowTimeExtrapolated)
        + highToDroopSubLowOffset.evaluate(highKnob, &droopSubLowHighExtrapolated);
    params.plateauDroopLowDbPerSec = timeToDroopLow.evaluate(timeKnob, &droopLowTimeExtrapolated)
        + highToDroopLowOffset.evaluate(highKnob, &droopLowHighExtrapolated);
    params.plateauDroopMidDbPerSec = timeToDroopMid.evaluate(timeKnob, &droopMidTimeExtrapolated)
        + highToDroopMidOffset.evaluate(highKnob, &droopMidHighExtrapolated);
    params.plateauDroopHighDbPerSec = timeToDroopHigh.evaluate(timeKnob, &droopHighTimeExtrapolated)
        + highToDroopHighOffset.evaluate(highKnob, &droopHighHighExtrapolated);
    params.fallRateSubLowDbPerSec = timeToFallSubLow.evaluate(timeKnob, &fallSubLowTimeExtrapolated)
        + highToFallSubLowOffset.evaluate(highKnob, &fallSubLowHighExtrapolated);
    params.fallRateLowDbPerSec = timeToFallLow.evaluate(timeKnob, &fallLowTimeExtrapolated)
        + highToFallLowOffset.evaluate(highKnob, &fallLowHighExtrapolated);
    params.fallRateMidDbPerSec = timeToFallMid.evaluate(timeKnob, &fallMidTimeExtrapolated)
        + highToFallMidOffset.evaluate(highKnob, &fallMidHighExtrapolated);
    params.fallRateHighDbPerSec = timeToFallHigh.evaluate(timeKnob, &fallHighTimeExtrapolated)
        + highToFallHighOffset.evaluate(highKnob, &fallHighHighExtrapolated);
    // Fixes a real "the decay and timing doesn't match the convolution" complaint that
    // fallRateDbPerSec alone can't close: real captures' post-knee fall is CURVED, not a single
    // constant dB/s rate - see build_measured_gate_curves.py's own _build_early_excess_curves
    // docstring for the direct per-capture measurement this is built from, and InhaltIRSynth.h's
    // own earlyExcessDb comment for what it does to the render. Same H0-equivalent-baseline-plus-
    // offset combination as fallRateDbPerSec/kneeTimeSubLowMs etc. above.
    params.earlyExcessDb = timeToEarlyExcess.evaluate(timeKnob, &earlyExcessTimeExtrapolated)
        + highToEarlyExcessOffset.evaluate(highKnob, &earlyExcessHighExtrapolated);

    if (extrapolated != nullptr)
        *extrapolated = tauAExtrapolated
            || softnessExtrapolated || earlyExcessTimeExtrapolated || earlyExcessHighExtrapolated
            || droopSubLowTimeExtrapolated || droopSubLowHighExtrapolated
            || droopLowTimeExtrapolated || droopLowHighExtrapolated
            || droopMidTimeExtrapolated || droopMidHighExtrapolated
            || droopHighTimeExtrapolated || droopHighHighExtrapolated
            || fallSubLowTimeExtrapolated || fallSubLowHighExtrapolated
            || fallLowTimeExtrapolated || fallLowHighExtrapolated
            || fallMidTimeExtrapolated || fallMidHighExtrapolated
            || fallHighTimeExtrapolated || fallHighHighExtrapolated
            || kneeSubLowTimeExtrapolated || kneeSubLowHighExtrapolated
            || kneeLowTimeExtrapolated || kneeLowHighExtrapolated
            || kneeMidTimeExtrapolated || kneeMidHighExtrapolated
            || kneeHighTimeExtrapolated || kneeHighHighExtrapolated;
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
