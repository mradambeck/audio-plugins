#!/usr/bin/env python3
"""Phase B7: builds knob -> fitted-parameter curves from fitted_raw.json.

Per the plan's default (1D curves, independent per knob, escalate to N-D only where evidence
shows real cross-knob interaction 1D curves can't capture): Time is the dominant, best-supported
knob (8 points at the Low=0/High=0 baseline) and gets a direct Curve1D per parameter. High is
built as an ADDITIVE offset curve (delta from the High=0 baseline at a fixed Time=2.3s, 3 points)
rather than its own absolute curve, then combined with the Time curve - a simplification that
assumes High's effect is roughly Time-independent, which the sparse grid (only one Time setting
has a High sweep) can't fully confirm, but is the same kind of "honest reading of what's
measured, not inventing what isn't" approach IntruderParameterMap.cpp already uses for Tighter.

Low is the case B3's findings.md flagged as a real interaction (no effect at High=0, but a real
one at High=-8) that a plain 1D curve would misrepresent - and the capture grid doesn't have
enough Low-at-multiple-High data points to fit a trustworthy 2D surface either (one single
comparison: Low=0 vs Low=+5 at High=-8). Rather than inventing precision the data doesn't
support, this deliberately does NOT build a Low curve - it's left as a documented gap for either
more targeted captures (ideal) or by-ear tuning during Phase C, the same way Intruder's own
automated fit needed a hand-tuned tankSustainMultiplier sweep afterward
(intruder-gated-reverb/analysis/validation_report.md's "Status" section).

tilt_low_gain/tilt_high_gain are excluded entirely: B6 found they stayed within ~2-3% of neutral
(1.0) across all 65 fits even after removing the initial fit's degenerate noise, meaning the
in-loop damping/band-gain parameters already explain nearly all of the measured tonal variation -
the output-tilt stage isn't pulling its weight. Kept fixed at 1.0 rather than curve-fit.
"""
import json
import os

from core.interp import fit_curve

HERE = os.path.dirname(__file__)
FITTED_RAW_PATH = os.path.join(HERE, "fitted_raw.json")
CURVES_PATH = os.path.join(HERE, "curves.json")

# Parameters that get a direct Time curve (at the Low=0/High=0 baseline) plus an additive High
# offset. output_gain is deliberately excluded - it's a per-capture level-matching artifact of
# fitting against inconsistently-leveled recordings, not a knob-driven DSP parameter the plugin
# should reproduce.
CURVE_PARAMS = ["high_band_gain", "low_band_gain", "damping_weight_mean"]


def main() -> None:
    data = json.load(open(FITTED_RAW_PATH))
    caps = data["captures"]

    time_baseline = sorted(
        (c["params"]["time"], c) for c in caps if c["params"]["low"] == 0 and c["params"]["high"] == 0
    )
    print(f"Time baseline (Low=0, High=0): {len(time_baseline)} points")

    high_sweep = sorted(
        (c["params"]["high"], c) for c in caps if c["params"]["low"] == 0 and c["params"]["time"] == 2.3
    )
    print(f"High sweep (Low=0, Time=2.3s): {len(high_sweep)} points")
    if not any(h == 0 for h, _ in high_sweep):
        raise RuntimeError("High sweep must include High=0 to anchor the offset curve at 0")

    curves = {}
    for param in CURVE_PARAMS:
        time_curve = fit_curve([(t, c[param]) for t, c in time_baseline])
        curves[f"time_to_{param}"] = {"points": time_curve.points()}

        baseline_at_high0 = next(c[param] for h, c in high_sweep if h == 0)
        high_offset_curve = fit_curve([(h, c[param] - baseline_at_high0) for h, c in high_sweep])
        curves[f"high_to_{param}_offset"] = {"points": high_offset_curve.points()}

        print(f"{param}: time points={[round(v,3) for _,v in time_curve.points()]}")
        print(f"{param}: high offset points={[round(v,4) for _,v in high_offset_curve.points()]}")

    curves["_notes"] = {
        "low_band": (
            "No Low curve built - findings.md found Low has no measurable onset-tone effect and "
            "no measurable decay effect at High=0, but does lengthen decay when High is very "
            "negative (one data point: Low=+5 vs Low=0 at High=-8, Time=2.3s, gave "
            "full_decay_rt60_s 2.217 vs 1.950). Not enough Low-at-multiple-High captures to fit a "
            "trustworthy interaction curve - needs either more targeted captures or by-ear tuning "
            "in Phase C, matching how Intruder's own automated fit still needed hand-tuning "
            "afterward."
        ),
        "tilt": (
            "tilt_low_gain/tilt_high_gain excluded - stayed within ~2-3% of neutral (1.0) across "
            "all 65 fits (see fitted_raw.json), meaning damping_weight_mean and the "
            "high_band_gain/low_band_gain split already explain nearly all measured tonal "
            "variation. Fixed at 1.0 in the plugin rather than curve-fit."
        ),
        "combination_model": (
            "value(Time, High) = time_curve(Time) + high_offset_curve(High), i.e. High's effect "
            "is treated as an additive, Time-independent offset from the Time curve's own "
            "Low=0/High=0 baseline. Only one Time setting (2.3s) has a High sweep, so this "
            "Time-independence assumption is unconfirmed - check in Phase D validation whether "
            "predicted values at other Time settings with nonzero High hold up."
        ),
    }

    with open(CURVES_PATH, "w") as fh:
        json.dump(curves, fh, indent=2)
    print(f"\nWrote {CURVES_PATH}")


if __name__ == "__main__":
    main()
