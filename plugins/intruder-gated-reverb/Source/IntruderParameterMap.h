#pragma once

// Phase 4: parameter interpolation. Converts UI-facing control values (H in hardware dB units,
// Tighter as a percentage) into DSP-ready coefficients, using the measured reference points from
// analysis/findings.md rather than an assumed linear/proportional relationship. Deliberately no
// JUCE or IntruderFDNEngine dependency - kept separate per IMPLEMENTATION.md's Phase 5 spec ("a
// parameter mapping layer... kept separate from the FDN/envelope core so the mapping can be refit
// without touching the DSP"), and independently unit-testable.
//
// Decay is NOT handled here: findings.md showed the hardware's own 0.1-9.8 label isn't literal
// seconds at all (measured RT60 only spans ~0.19-0.49s against a 98x label range), so the plugin
// exposes Decay directly in perceptually-meaningful seconds rather than replicating that confusing
// compressed scale - there's no measured-point interpolation to do for a control that was
// redefined on purpose. See IntruderReferenceData.h's decayLabelToRT60 table for the raw
// measurement, kept for Phase 6 cross-validation only.
namespace IntruderParameterMap
{
    // H -> the tilt-filter parameter that reproduces the measured onset spectral tilt curve.
    // findings.md's 5 measured (H, onset-tilt-dB) points are NOT linearly related to H (the slope
    // roughly doubles between the -3..0dB region and the -9..-3dB region - see
    // IntruderReferenceData.h's hToOnsetTiltDb table) - piecewise-linear interpolation between
    // them, linearly extrapolated by the nearest segment's slope outside [-9, 0], reproduces that
    // shape. `extrapolated` is set true when hDb fell outside the measured range, so a caller that
    // wants to know (logging, a future UI indicator) can - IMPLEMENTATION.md Phase 4 asks that an
    // unverified region be flagged rather than presented as fitted, not that it be refused.
    float mapTiltDbFromH(float hDb, bool* extrapolated = nullptr);

    // Tighter (0-1) -> a multiplier applied to the envelope's hold-time fraction (see
    // IntruderFDNEngine::setSpacingMultiplier()). Tighter is a binary flag in every capture (present or not),
    // not a continuous knob on the reference unit, so there are no intermediate measured points to
    // interpolate between - what IS measured is the off/on ratio itself (echo-density half-rise
    // time dropped to ~0.76x of its untightened value, averaged across all 9 measured pairs - see
    // IntruderReferenceData.h's tighterHalfRiseRatio). This linearly interpolates from 1.0 (no
    // effect) at Tighter=0 to that measured ratio at Tighter=1, which is an honest reading of what
    // "how tight" the plugin's own continuous control means: nothing beyond the two measured
    // endpoints is being invented, just interpolated between them.
    float mapTighterToSpacingMultiplier(float tighter01);
}
