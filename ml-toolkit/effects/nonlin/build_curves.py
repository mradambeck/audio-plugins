#!/usr/bin/env python3
"""Phase 4: builds knob -> fitted-parameter curves from fitted_raw.json.

Split into THREE families (revised after findings.md's real capture analysis - see that file's
"High: timing-NEUTRAL overall, but NOT damping-neutral" section, which corrected the working
hypothesis this module originally shipped with):

  - TIME_ONLY_PARAMS (the gate envelope's overall timing + tank density/damping) get a single Time
    curve. Pooled across DIFFERENT High settings at the same Time IF, and only if, the
    H-timing-neutrality check below actually confirms H doesn't move them in this specific fitted
    data - findings.md confirmed this directly for gate_length/t_knee_ms (identical to two decimal
    places across different High at the same Time), which is what makes the sparse 9-capture cross
    usable at all for these parameters.
  - TONAL_PARAMS (the input tilt) get a High offset curve, built from whichever single Time
    setting has the richest H sweep in the actual capture set.
  - H_DEPENDENT_TIMING_PARAMS: plateau_droop_db_per_s specifically. findings.md found this is NOT
    H-neutral (roughly 4-12x different between High=0 and negative High at the same Time, while
    the OVERALL gate length stays exactly fixed) - High redistributes energy loss between the
    plateau and the post-knee fall without changing the total time to reach -20dB. Gets the SAME
    additive Time(H=0 baseline) + High-offset treatment as the tonal params, not pooled into the
    Time-only curve the way it was before this was measured.

No H*Time interaction curve is built for anything, matching effects/ambience/build_curves.py's
refusal to fit Low's interaction with High from too few points - see _notes["h_time_interaction"]
for the honest reading of what the actual captured grid can and can't support, populated with real
residual numbers rather than left as a bare assertion.
"""
from __future__ import annotations

import json
import os

from core.interp import fit_curve

HERE = os.path.dirname(__file__)
FITTED_RAW_PATH = os.path.join(HERE, "fitted_raw.json")
CURVES_PATH = os.path.join(HERE, "curves.json")

TIME_ONLY_PARAMS = [
    "t_knee_ms", "tau_a_ms", "fall_rate_db_per_s", "tau_k_ms",
    "feedback_gain", "damping_weight_mean", "diffuser_gain",
]
TONAL_PARAMS = ["tilt_low_gain", "tilt_high_gain"]
# tilt_pivot_hz used to be here too, fit-derived. Removed after direct measurement (see
# build_measured_gate_curves.py's own docstring) found the fit's own pivot (~4200-4700Hz) puts the
# one-pole shelf's transition too close to the 6-16kHz band a "murky/dark" complaint is measured
# in - even at extreme gain, that pivot structurally caps the achievable low/high spread well below
# the real captures' own measured spread at negative High. tilt_pivot_hz is now a hand-measured
# CONSTANT (1500Hz, matching tiltLowGainBaselineAtHigh0/tiltHighGainBaselineAtHigh0's own existing
# convention), not a fit-derived High-dependent curve - see InhaltParameterMap.cpp.
# See module docstring - moved out of TIME_ONLY_PARAMS after findings.md measured this specific
# parameter is NOT H-neutral, unlike every other timing parameter checked.
H_DEPENDENT_TIMING_PARAMS = ["plateau_droop_db_per_s"]

# How much a timing parameter is allowed to spread across different High settings AT THE SAME
# Time setting before H is judged to have a real effect on timing after all (contradicting the
# working hypothesis from direct hardware measurement - see module docstring). t_knee_ms is the
# one checked directly, since it's the parameter effects/nonlin/analyze.py already cross-checks
# against a hand-measured table (see GATE_LENGTH_TOLERANCE_MS there) - same order of tolerance.
H_TIMING_NEUTRALITY_TOLERANCE_MS = 10.0


def _group_by_time_and_high(captures: list[dict]) -> dict:
    grouped = {}
    for c in captures:
        key = (c["params"]["time"], c["params"]["high"])
        grouped[key] = c
    return grouped


def _check_h_timing_neutrality(captures: list[dict]) -> tuple[bool, dict]:
    """For every Time value with >1 distinct High point in the actual fitted data, spreads
    t_knee_ms across those High values. Returns (is_neutral, {time: spread_ms})."""
    by_time = {}
    for c in captures:
        by_time.setdefault(c["params"]["time"], []).append(c)

    spreads = {}
    for time_val, group in by_time.items():
        if len(group) < 2:
            continue
        knees = [c["t_knee_ms"] for c in group]
        spreads[time_val] = max(knees) - min(knees)

    is_neutral = all(spread <= H_TIMING_NEUTRALITY_TOLERANCE_MS for spread in spreads.values())
    return is_neutral, spreads


def main() -> None:
    data = json.load(open(FITTED_RAW_PATH))
    caps = data["captures"]
    print(f"Loaded {len(caps)} fitted captures")

    is_neutral, spreads = _check_h_timing_neutrality(caps)
    for time_val, spread in sorted(spreads.items()):
        print(f"  t_knee_ms spread at Time={time_val}s across available High settings: {spread:.2f}ms")
    print(f"H-timing-neutrality confirmed in fitted data: {is_neutral} "
          f"(tolerance {H_TIMING_NEUTRALITY_TOLERANCE_MS}ms)")

    curves = {}

    # --- Timing curve(s): pool across High IF neutrality holds, else restrict to High=0 ---
    if is_neutral:
        by_time = {}
        for c in caps:
            by_time.setdefault(c["params"]["time"], []).append(c)
        time_points_source = "pooled across all available High settings (H-timing-neutrality confirmed)"
    else:
        by_time = {}
        for c in caps:
            if c["params"]["high"] == 0:
                by_time.setdefault(c["params"]["time"], []).append(c)
        time_points_source = "H=0 only (H-timing-neutrality NOT confirmed in this fitted data)"
    print(f"Timing curves built from: {time_points_source}, {len(by_time)} distinct Time value(s)")

    for param in TIME_ONLY_PARAMS:
        pairs = []
        for time_val, group in sorted(by_time.items()):
            values = [c[param] for c in group]
            pairs.append((time_val, sum(values) / len(values)))
        if len(pairs) < 2:
            print(f"  {param}: only {len(pairs)} distinct Time point(s) available - skipping curve, "
                  "using the single available value as a constant instead")
            curves[f"time_to_{param}"] = {"points": pairs, "constant": len(pairs) == 1}
            continue
        curve = fit_curve(pairs)
        curves[f"time_to_{param}"] = {"points": curve.points()}
        print(f"  {param}: {[round(v, 3) for _, v in curve.points()]}")

    # --- H=0-only Time baseline for the H-dependent timing param(s) - see module docstring ---
    by_time_h0 = {}
    for c in caps:
        if c["params"]["high"] == 0:
            by_time_h0.setdefault(c["params"]["time"], []).append(c)
    print(f"\nH-dependent timing param(s) use an H=0-only Time baseline: {len(by_time_h0)} distinct Time value(s)")
    for param in H_DEPENDENT_TIMING_PARAMS:
        pairs = sorted((t, sum(c[param] for c in group) / len(group)) for t, group in by_time_h0.items())
        if len(pairs) < 2:
            print(f"  {param}: only {len(pairs)} H=0 Time point(s) available - skipping curve, "
                  "using the single available value as a constant instead")
            curves[f"time_to_{param}"] = {"points": pairs, "constant": len(pairs) == 1}
        else:
            curve = fit_curve(pairs)
            curves[f"time_to_{param}"] = {"points": curve.points()}
            print(f"  {param} (H=0 baseline): {[round(v, 3) for _, v in curve.points()]}")

    # --- Tonal + H-dependent-timing offset curves: additive High offset, from whichever Time
    # setting has the richest H sweep in the actual capture set ---
    by_time_high_count = {}
    for c in caps:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))
    high_sweep = sorted((c["params"]["high"], c) for c in caps if c["params"]["time"] == richest_time)
    print(f"\nHigh sweep chosen at Time={richest_time}s: {len(high_sweep)} point(s), "
          f"High values {[h for h, _ in high_sweep]}")

    has_h0 = any(h == 0 for h, _ in high_sweep)
    for param in TONAL_PARAMS + H_DEPENDENT_TIMING_PARAMS:
        if not has_h0 or len(high_sweep) < 2:
            print(f"  {param}: not enough High points at Time={richest_time}s (need >=2, including "
                  "High=0 to anchor) - skipping curve, using neutral default")
            curves[f"high_to_{param}_offset"] = {"points": [], "constant": True, "default": 0.0 if param != "tilt_pivot_hz" else None}
            continue
        baseline = next(c[param] for h, c in high_sweep if h == 0)
        curve = fit_curve([(h, c[param] - baseline) for h, c in high_sweep])
        curves[f"high_to_{param}_offset"] = {"points": curve.points(), "baseline_at_high0": baseline}
        print(f"  {param} offset: {[round(v, 4) for _, v in curve.points()]} (baseline {baseline:.4f})")

    # --- H*Time interaction residual check (report only - no interaction curve is built) ---
    residuals = []
    time_curve_points = {p: curves.get(f"time_to_{p}") for p in TIME_ONLY_PARAMS}
    for c in caps:
        t, h = c["params"]["time"], c["params"]["high"]
        entry = time_curve_points.get("t_knee_ms")
        if not entry or not entry.get("points"):
            continue
        from core.interp import Curve1D
        xs = [p[0] for p in entry["points"]]
        ys = [p[1] for p in entry["points"]]
        predicted, extrapolated = Curve1D(xs, ys).evaluate(t)
        residuals.append({
            "filename": c["filename"], "time": t, "high": h,
            "predicted_t_knee_ms": round(predicted, 2),
            "actual_t_knee_ms": round(c["t_knee_ms"], 2),
            "residual_ms": round(c["t_knee_ms"] - predicted, 2),
            "extrapolated": extrapolated,
        })

    curves["_notes"] = {
        "h_time_interaction": (
            f"No High*Time interaction curve is built - the captured grid (see the project plan's "
            f"capture design) is a cross, not a full grid, and can't support fitting one honestly. "
            f"H-timing-neutrality was CHECKED against the actual fitted data (not assumed): "
            f"{'confirmed' if is_neutral else 'NOT confirmed'} within "
            f"{H_TIMING_NEUTRALITY_TOLERANCE_MS}ms (per-Time t_knee_ms spreads: "
            f"{ {str(k): round(v, 2) for k, v in spreads.items()} }). "
            f"Per-capture residuals against the resulting time_to_t_knee_ms curve are in "
            f"'h_time_interaction_residuals' below - a real interaction would show up as residuals "
            f"that grow with |High|, rather than sitting near the fit's general noise level."
        ),
        "h_time_interaction_residuals": residuals,
        "tonal_time_independence": (
            "tilt_low_gain/tilt_high_gain/tilt_pivot_hz are modeled as depending on High only, "
            "not Time - findings.md's onset 4-band breakdown found Time doesn't move tone at "
            "H=0 (T=7.0s and T=9.8s matched to within 0.1dB per band), so a single High offset "
            "curve (built from whichever Time setting had the richest H sweep, printed above) is "
            "used at every Time setting."
        ),
        "plateau_droop_combination_model": (
            "plateau_droop_db_per_s = time_to_plateau_droop_db_per_s(Time) [H=0 baseline] + "
            "high_to_plateau_droop_db_per_s_offset(High) - moved out of the pooled Time-only "
            "curve after findings.md measured High has a real, large effect on this specific "
            "parameter (roughly 4-12x between High=0 and negative High at the same Time) even "
            "though the OVERALL gate length (t_knee_ms and friends) is exactly High-independent. "
            "The offset curve uses the same richest-High-sweep Time setting as the tonal params, "
            "which is a Time-independence assumption for the OFFSET's shape (not the baseline) - "
            "same caveat as Ambience's own High-offset curves, unconfirmed by a second Time "
            "setting's full sweep."
        ),
    }

    with open(CURVES_PATH, "w") as fh:
        json.dump(curves, fh, indent=2)
    print(f"\nWrote {CURVES_PATH}")


if __name__ == "__main__":
    main()
