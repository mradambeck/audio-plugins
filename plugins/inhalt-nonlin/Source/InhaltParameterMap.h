#pragma once

#include "../../common/dsp/FittedCurve1D.h"

// Knob values -> InhaltIRSynth coefficients.
//
// PLACEHOLDER DATA, clearly marked as such below - Adam has not yet supplied the NonLin capture
// set (see ml-toolkit/effects/nonlin/captures/, currently empty and gitignored), so none of the
// ml-toolkit Phase 2-4 pipeline (analyze.py/fit_nonlin.py/build_curves.py/export_params.py) has
// run against real hardware audio yet. This map uses only what IS already directly measured (the
// hand-measured -20dB gate-length table and the H tilt's onset dB values - both established by
// direct inspection of a real NonLin capture set, see the project plan) plus honestly-labelled
// placeholder shapes for everything that measurement doesn't cover (the gate's internal
// build-up/knee/fall breakdown, the tank's own density/damping). Once real captures land and the
// pipeline runs, `InhaltReferenceData.h` gets generated for real and this file's placeholder
// tables are replaced with it - this file's own shape (thin FittedCurve1D wrapper) doesn't need
// to change, only the numbers inside it, matching AuraParameterMap.cpp's own precedent.
//
// MEASURED GAP, not yet fixed (needs real captures to fix correctly, not more guessing):
// InhaltRenderIR --timeKnob 4.8 was rendered end-to-end and analyzed with ml-toolkit's own
// gate_envelope_params() - the real verification loop this whole pipeline is built around. It
// measured a ~99.6ms gate length against a ~216.6ms target (the real hardware measurement this
// map's gateLengthMsCurve is trying to reproduce), and a real plateau_droop_db_per_s of -188dB/s
// where this map hands the synth an explicit 0dB/s. Root cause: mapTimeKnobToTankParams()'s
// placeholder feedbackGain/dampingWeight (0.78/0.5, chosen arbitrarily) don't build the tank to
// full density fast enough for the explicit gate multiplier to dominate the shape the way the
// architecture assumes - the tank's own still-sparse, still-decaying early response leaks through
// instead. This is exactly the calibration Phase 2's real captures are for, not a bug in the gate
// formula, the synthesis math, or the measurement tooling (all three were independently confirmed
// correct in isolation before concluding this). Documented here rather than hand-tuned away by
// guessing better placeholder numbers - that would invent precision the data doesn't support.
namespace InhaltParameterMap
{

struct GateParams
{
    float buildUpMs = 3.0f;
    float plateauDroopDbPerSec = 0.0f;
    float kneeTimeMs = 150.0f;
    float fallRateDbPerSec = -250.0f;
    float kneeSoftnessMs = 4.0f;
};

struct TankParams
{
    float feedbackGain = 0.78f;
    float dampingWeight = 0.5f;
};

struct TiltParams
{
    float lowGain = 1.0f;
    float highGain = 1.0f;
    float pivotHz = 1500.0f;
};

// Time knob (0.1-9.8, the hardware's own label scale - deliberately not seconds, see
// PluginProcessor.h's timeKnobParamID comment) -> the gate envelope shape. extrapolated, if
// non-null, is set when timeKnob falls outside every mapping curve's measured range.
GateParams mapTimeKnobToGateParams(float timeKnob, bool* extrapolated = nullptr) noexcept;

// The one thing about this mapping that IS a direct hardware measurement, not a placeholder -
// returns the real hand-measured -20dB gate length (ms) for display in the editor, independent of
// mapTimeKnobToGateParams()'s (still placeholder) internal shape breakdown. See
// InhaltParameterMapTests.cpp's hardcoded spot-check against the same table.
float gateLengthMsForDisplay(float timeKnob, bool* extrapolated = nullptr) noexcept;

// High knob (-9..0 dB) -> tank density/damping. PLACEHOLDER - no direct measurement yet of
// whether High has any real effect on the tank's own damping (see the project plan's "one brief
// assumption to test" note: H's measured envelope-timing-neutrality is evidence AGAINST an
// independent damping effect, so this returns a Time-only, High-independent constant until
// Phase 2 either confirms a real per-band droop difference or rules one out).
TankParams mapTimeKnobToTankParams(float timeKnob, bool* extrapolated = nullptr) noexcept;

// High knob (-9..0 dB) -> input tilt. REAL measurement: H=0 (neutral) vs H=-9 (+4.2dB low,
// -9.1dB high, pivot ~1-2kHz) - see the project plan's "already measured" section. Only two
// points (both ends of the hardware's own range), so every High setting other than exactly 0 or
// -9 is interpolated, not separately measured - refine once the real H sweep is analyzed.
TiltParams mapHighKnobToTilt(float highKnob, bool* extrapolated = nullptr) noexcept;

} // namespace InhaltParameterMap
