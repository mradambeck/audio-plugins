#pragma once

// Phase 4-equivalent mapping layer for Aura (AMS RMX16 "Ambience" emulation): converts UI-facing
// Time/High values into DSP-ready feedback-loop coefficients. Deliberately no JUCE or
// AuraFDNEngine dependency - kept separate so the mapping can be refit without touching the DSP,
// same convention as IntruderParameterMap.
//
// Time and High are combined ADDITIVELY throughout this file: value = timeCurve(time) +
// highOffsetCurve(high) - i.e. High's effect is treated as a Time-independent offset from the
// Time curve's own High=0 baseline. Confirmed for onset tone (checked across all 6 available Time
// settings, 2026-09-02 - see AuraOnsetTiltData.h); the decay-gain curve's own High offset
// (AuraDecayGainData.h) is only calibrated at Time=2.3s, so its Time-independence is unconfirmed.
namespace AuraParameterMap
{
    struct DecayParams
    {
        float decayGain;
        float dampingWeight;
    };

    // decayGain is a single SHARED feedback gain (fed to both of AuraFDNEngine::setBandGains()'s
    // arguments), from AuraDecayGainData.h - NOT the independent high_band_gain/low_band_gain
    // curves in AuraReferenceData.h. Those looked reasonable in isolation but translate into a
    // wildly exaggerated decay-time split once rendered (measured ~2x, where the real hardware's
    // own low/high decay times are within ~1.3% of each other) - see AuraDecayGainData.h's
    // comment for the full story. dampingWeight is still the original fitted curve
    // (AuraReferenceData.h) - that one wasn't found to have the same problem.
    //
    // extrapolated, if non-null, is set true if EITHER timeSeconds or highDb fell outside its own
    // measured range (0.1-5.5s for Time, -8..0dB for High).
    DecayParams mapTimeAndHighToDecayParams(float timeSeconds, float highDb, bool* extrapolated = nullptr);

    // High -> the input-stage TiltFilter coefficient that reproduces the measured onset spectral
    // tilt (AuraOnsetTiltData.h), as a DELTA from the High=0 reference point - same convention and
    // reasoning as IntruderParameterMap::mapTiltDbFromH (TiltFilter's own zero-point means "no
    // added coloration", but the engine's raw signal has real inherent tilt even at High=0, so
    // feeding it the absolute measured value would double-count that baseline). Separate from
    // mapTimeAndHighToDecayParams() - this drives AuraFDNEngine::setInputTilt().
    float mapInputTiltDb(float highDb, bool* extrapolated = nullptr);

    // Low (-8..+6 on the real hardware) is NOT wired to anything below yet - deliberately, not an
    // oversight. The one place the capture grid lets its effect be isolated (Low=+5 vs Low=0,
    // High=-8 fixed, across all 8 Time settings) shows a delta that's negative at short Time
    // settings and only turns positive at longer ones - no clean sign pattern, not something that
    // can be hardcoded into a formula without presenting false precision. If wired up later,
    // calibrate from the one clean direct RT60 measurement (+13.7% at Low=+5, High=-8, Time=2.3s),
    // not from the automated fit's own low_band_gain parameter - that showed the same
    // underestimates-a-real-effect pattern the fit's tilt gains did (see AuraOnsetTiltData.h). The
    // parameter still exists on the plugin (matching the real hardware's control set, and so
    // presets/automation referencing it don't need to change later) - it just has no measured
    // effect to apply yet.
}
