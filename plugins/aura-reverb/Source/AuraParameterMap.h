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

    // Time -> AuraFDNEngine::setSubBassGain() (AuraSubBassGainData.h) - the in-loop 120Hz-pivot
    // attenuation addressing the sub-bass decay-RATE limit (see that file's own comment for the
    // full calibration story). Time-only, no High dependence - not measured against a High sweep
    // at all, unlike decayGain/dampingWeight, since the low/mid decay-rate gap this targets was
    // only ever characterized at High=0.
    float mapTimeToSubBassGain(float timeSeconds, bool* extrapolated = nullptr);

    // No entry here for the plugin's "Low Cut" control (was the real hardware's own "Low" knob,
    // carried unwired - the capture grid couldn't isolate a usable, sign-consistent effect for it,
    // see git history for the full story) - it was repurposed into a plain 0-300Hz input high-pass
    // utility control with no fitted-curve mapping at all, wired directly in
    // PluginProcessor::processBlock() straight to AuraFDNEngine::setLowCutHz(), the same way
    // Pre-Delay and Bit Depth are. Nothing for this file to do with it.
}
