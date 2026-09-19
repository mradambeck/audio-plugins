#pragma once

#include "../../common/dsp/FittedCurve1D.h"

// Knob values -> InhaltIRSynth coefficients.
//
// Real fitted/measured data, from the 9-capture set in ml-toolkit/effects/nonlin/captures/ - see
// InhaltReferenceData.h's own provenance comment for the exact git commit/capture-set hash this
// was generated from, and ml-toolkit/effects/nonlin/findings.md for the analysis behind it.
//
// NOT a straight copy of the Python fit's own numbers. Every gate-timing parameter
// (tau_a_ms/t_knee_ms/fall_rate_db_per_s/plateau_droop_db_per_s) was cross-checked per-capture
// against core.features.gate_envelope_params()'s DIRECT measurement of the real captures and
// found systematically wrong in the fit (e.g. Time=0.1: measured knee time 125ms, fitted 366ms) -
// replaced with curves built directly from measurement instead (see
// ml-toolkit/effects/nonlin/build_measured_gate_curves.py). feedback_gain, damping_weight_mean,
// and diffuser_gain are kept from the fit - see that script's own module docstring for why those
// three specifically were trusted. tilt_low_gain/tilt_high_gain/tilt_pivot_hz were ALSO originally
// kept from the fit, but the pivot turned out to be structurally wrong (see InhaltParameterMap.cpp's
// own comment on tiltPivotHz) and is now a hand-measured constant, not fit-derived. This is the
// same "fit numbers don't hold up, use direct measurement" correction AuraDecayGainData.h/AuraOnsetTiltData.h
// already established as precedent for this catalog, applied here from the start rather than
// discovered after shipping placeholder data.
//
// Known remaining gaps, not silently papered over:
//   - tau_k_ms (knee softness) has no direct measurement at all - kept from the fit, not
//     cross-checked the way the other timing parameters were.
//   - The two shortest Time settings (0.1s, 0.8s) exist only at High=-3 in the real capture set;
//     their gate-timing values are pooled into the same Time-only curve as the High=0 points at
//     longer Time (see build_measured_gate_curves.py's own docstring for why, and findings.md for
//     why this is an approximation, not a confirmed High-neutral measurement, for these
//     specific internal parameters - unlike the overall gate_length_ms_at_20db, which IS
//     confirmed High-neutral).
//   - mapTimeKnobToTankParams() and the plateau-droop combination don't yet depend on the tank's
//     per-band behaviour (only core.features.band_gate_params() measures that) - a broadband
//     droop/damping figure is used for the whole spectrum.
namespace InhaltParameterMap
{

struct GateParams
{
    float buildUpMs = 3.0f;
    float kneeTimeMs = 150.0f;
    float kneeSoftnessMs = 4.0f;
    // Per-band decay rates - see InhaltIRSynth.h's own comment on Params::plateauDroopSubLowDbPerSec
    // etc. for why a single broadband rate was replaced with four.
    float plateauDroopSubLowDbPerSec = 0.0f;
    float plateauDroopLowDbPerSec = 0.0f;
    float plateauDroopMidDbPerSec = 0.0f;
    float plateauDroopHighDbPerSec = 0.0f;
    float fallRateSubLowDbPerSec = -250.0f;
    float fallRateLowDbPerSec = -250.0f;
    float fallRateMidDbPerSec = -250.0f;
    float fallRateHighDbPerSec = -250.0f;
    float earlyExcessDb = 0.0f;
};

struct TankParams
{
    float feedbackGain = 0.78f;
    float dampingWeight = 0.5f;
    // Input diffuser gain (see InhaltIRSynth.h's own comment on the allpass diffuser chain this
    // feeds) - added to close a real, ear-caught onset-density gap ("denser quality... in the
    // initial attack" / "more gritty" missing from the render vs. the real captures). Fitted
    // jointly with feedback_gain/damping_weight_mean rather than hand-tuned, per
    // core.fit.onset_density_loss - see that function's docstring.
    float diffuserGain = 0.45f;
    // Direct/early-arrival tap gain (see InhaltIRSynth.h's own Params::directGain comment) - a
    // hand-measured CONSTANT (Time/High-independent within the real capture set's own sampling),
    // not fit-derived, matching InhaltParameterMap.cpp's tiltPivotHz convention.
    float directGain = 0.35f;
};

struct TiltParams
{
    float lowGain = 1.0f;
    float highGain = 1.0f;
    float pivotHz = 1500.0f;
};

// Time knob (0.1-9.8, the hardware's own label scale - deliberately not seconds, see
// PluginProcessor.h's timeKnobParamID comment) -> the gate envelope shape. Also depends on High
// for plateauDroopDbPerSec (findings.md found High redistributes energy loss between the plateau
// and the post-knee fall while conserving the overall gate length - see that file's "High:
// TIMING-neutral overall, but NOT damping-neutral" section) and for kneeTimeMs (a real, ear-caught
// "reverb rings out too long at High=0" complaint traced to a genuine High-dependence at
// Time=7.0/9.8 - see InhaltParameterMap.cpp's own comment on kneeTimeMs for the earlier, reverted
// attempt at this and the double-counting bug that caused the regression, now fixed at the source
// in build_measured_gate_curves.py's own _build_t_knee_ms_curves).
// extrapolated, if non-null, is
// set when either knob falls outside its own curves' measured range.
GateParams mapTimeAndHighToGateParams(float timeKnob, float highKnob, bool* extrapolated = nullptr) noexcept;

// The one thing about this mapping that IS always a direct hardware measurement, independent of
// the gate-shape breakdown above - returns the real hand-measured -20dB gate length (ms) for
// display in the editor. See InhaltParameterMapTests.cpp's hardcoded spot-check against the same
// table findings.md records.
float gateLengthMsForDisplay(float timeKnob, bool* extrapolated = nullptr) noexcept;

// Time knob -> tank density/damping (feedback_gain, damping_weight_mean) - from the fit, not a
// direct measurement (core.features has no function that measures a tank's internal density
// directly). Not currently High-dependent - findings.md's own evidence (High doesn't move overall
// envelope timing) argues against an independent High effect here, and the fit's own per-capture
// values didn't show a High-linked pattern strong enough to justify a second curve on this sparse
// a grid.
TankParams mapTimeKnobToTankParams(float timeKnob, bool* extrapolated = nullptr) noexcept;

// High knob (-9..0 dB) -> input tilt. Gains are LTAS-calibrated (whole-decay average spectrum,
// not just the onset window) against the real captures - see build_measured_gate_curves.py's own
// docstring. Pivot is a fixed, hand-measured 1500Hz constant (InhaltParameterMap.cpp's own
// tiltPivotHz comment) - the ~4200-4700Hz value findings.md originally preferred (over a
// ~1.5-2kHz onset-band estimate it flagged as "less precise") turned out to structurally cap the
// achievable High=-4/-9 darkness well below what the real hardware measures, a real, ear-caught
// gap the LTAS measurement traced back to the pivot, not just the gain magnitude.
TiltParams mapHighKnobToTilt(float highKnob, bool* extrapolated = nullptr) noexcept;

} // namespace InhaltParameterMap
