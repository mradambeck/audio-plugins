#pragma once

#include <array>

// Raw measured reference points from analysis/findings.md (derived from analysis/features.json,
// the Phase 1 extraction over all 19 captured IRs in ir-captures/). Kept as a single source of
// truth that IntruderParameterMap.cpp's interpolation reads from and Phase 6's validation can
// cross-check renders against - if these numbers are ever re-measured (e.g. analyze_irs.py
// changes), update them here, not in the interpolation logic itself.
namespace IntruderReferenceData
{
    struct HToTiltPoint
    {
        float hDb;
        float onsetTiltDb; // 10*log10(HF/LF energy) in the first ~20ms, non-Tighter files, averaged across decay settings that share this H (see findings.md's "H" table - decay-independent).
    };

    // From findings.md's H table. Sorted ascending by hDb - IntruderParameterMap.cpp's
    // interpolation assumes this ordering.
    static constexpr std::array<HToTiltPoint, 5> hToOnsetTiltDb { {
        { -9.0f, -0.31f },
        { -7.0f, 1.13f },
        { -4.0f, 3.86f },
        { -3.0f, 4.73f },
        { 0.0f, 9.665f }, // average of the four H=0 files (9.65, 9.66, 9.66, 9.68 - all within 0.03dB)
    } };
    static_assert(hToOnsetTiltDb.back().hDb == 0.0f,
        "IntruderParameterMap::mapTiltDbFromH() treats the last point as the H=0 neutral reference");

    // Average, across all 9 measured Tighter/non-Tighter pairs, of
    // (echo_density_half_rise_time_s with Tighter) / (echo_density_half_rise_time_s without) -
    // see findings.md's Tighter table for the individual pairs (0.639-0.871, averaging 0.760).
    constexpr float tighterHalfRiseRatio = 0.760f;

    struct DecayLabelToRT60Point
    {
        float decayLabelSeconds; // the hardware's own filename label - NOT literal seconds, see findings.md
        float measuredRT60Seconds; // Schroeder RT60, non-Tighter files, averaged where H varied at the same label
    };

    // Reference only (Phase 6 cross-validation) - the plugin's own Decay control is deliberately
    // NOT driven by this table. See IntruderParameterMap.h's class comment for why.
    static constexpr std::array<DecayLabelToRT60Point, 6> decayLabelToRT60 { {
        { 0.1f, 0.1918f },
        { 0.8f, 0.1918f },
        { 2.2f, 0.2285f },
        { 4.8f, 0.3209f },
        { 7.0f, 0.4263f }, // average of -7H (0.4205) and 0H (0.4320)
        { 9.8f, 0.4767f }, // average of -4H (0.4899), -9H (0.4923), 0H (0.4879)
    } };
}
