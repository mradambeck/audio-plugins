#!/usr/bin/env python3
"""Phase 4 correction: replaces build_curves.py's FITTED gate-timing curves with ones built
directly from features.json's real measurements, after comparing the fit's own t_knee_ms against
ground truth per-capture and finding it systematically wrong (not just noisy) - short-Time
captures fit a t_knee_ms 2.5-2.9x TOO LONG (e.g. Time=0.1: measured 125.1ms, fitted 366.2ms) and
long-Time captures fit one too short by 20-35%. fall_rate_db_per_s showed the same problem even
more severely (measured -141 to -187dB/s fairly consistently across every Time/High setting;
fitted ranged wildly from -101 to -1614dB/s, inversely correlated with Time in a way the real
hardware doesn't show at all). This is the exact "the fit's numbers don't hold up, use direct
measurement instead" pattern this project's own AuraDecayGainData.h/AuraOnsetTiltData.h already
established as precedent - not a new kind of gap.

KEPT from the fit (fitted_raw.json, via build_curves.py's own curves.json), NOT overwritten here:
feedback_gain, damping_weight_mean, diffuser_gain, tilt_pivot_hz - none of these have as complete
a direct-measurement replacement (the tank's internal density/damping isn't something core.features
measures directly at all), and the fitted pivot values look physically plausible (clustering
4200-4700Hz, consistent with this module's own earlier direct tilt_fit() estimate of ~4044Hz).

tilt_low_gain/tilt_high_gain ARE overwritten here too, for the same reason as the gate-timing
parameters: TILT_REGULARIZATION_WEIGHT (added to fit_nonlin.py after the first fit diverged, to
stop tilt_low_gain/tilt_high_gain drifting unboundedly - see that module's own comment) pulled the
fitted tilt magnitude back toward neutral so strongly that it undershot the real measured tilt by
roughly 2-3x (fit's own High=-9 offset: lowGain +0.034/highGain -0.022; findings.md's direct onset
5-band measurement at the same setting: lowGain +0.68/highGain -0.66, from a real +4.5dB/-9.5dB
onset tilt). The regularization weight was never swept against real data before this run (flagged
as a known gap in its own comment) - rather than re-sweep it now, this replaces the tilt GAIN
curves with direct measurement (the same real onset-band numbers findings.md already reports),
which needs no regularization tuning at all. tilt_pivot_hz is kept from the fit since it isn't
subject to this same regularizer and its own value already lines up with an independent direct
estimate.

Honest compromise, not invented precision: the two shortest Time settings (0.1s, 0.8s) only exist
at High=-3 in the real capture set - there is no High=0 measurement at short Time at all. Rather
than have zero gate-shape data below Time=2.2s, this pools those H=-3 points into the same
Time-only curve as the H=0 points at longer Time, on the working assumption (not fully confirmed -
see findings.md's own finding that internal gate-shape parameters are NOT as cleanly H-neutral as
the overall gate_length_ms_at_20db is) that the difference is small enough at this specific H
value to not distort the Time curve badly. Documented in _notes, not silently assumed.

tau_a_ms (the model's exponential build-up time constant) has no DIRECT equivalent in
gate_envelope_params()'s output (which measures build_up_ms, a threshold-crossing time, not a
time constant) - converted via the closed-form relationship for a 1-exp(-t/tau) ramp to reach
-3dB (used as the "reached" threshold in gate_envelope_params()): tau = build_up_ms / 1.231.

tau_k_ms (knee softness) has NO direct measurement at all - core.features has no function for it.
Kept from the fit as a rough guide (not trusted as tightly as the other fit-derived parameters),
clearly flagged as an unmeasured gap in the exported header's own comment.
"""
from __future__ import annotations

import json
import math
import os

import numpy as np

from core.features import find_onset
from core.interp import Curve1D, fit_curve
from core.io import load_audio_channels

HERE = os.path.dirname(__file__)
FEATURES_PATH = os.path.join(HERE, "features.json")
CAPTURES_DIR = os.path.join(HERE, "captures")
CURVES_PATH = os.path.join(HERE, "curves.json")
FITTED_RAW_PATH = os.path.join(HERE, "fitted_raw.json")

# The four params build_curves.py's own docstring says are "KEPT from the fit, not overwritten
# here" - see this module's docstring. These are tank-density/topology-ish quantities (or, for
# tau_k_ms, an unmeasured but still Time-varying one), not gate TIMING - build_curves.py's
# H-timing-neutrality gate (checked against t_knee_ms specifically) shouldn't be the thing
# deciding whether THESE curves get to pool the short-Time (0.1s/0.8s, High=-3-only) captures in.
# Re-pooled here unconditionally, straight from fitted_raw.json, to restore low-Time coverage
# whenever a fit run's own t_knee_ms happens to fail that unrelated neutrality check (see
# _repool_fit_only_time_params's own docstring for the concrete bug this fixes).
FIT_ONLY_TIME_PARAMS = ["feedback_gain", "damping_weight_mean", "diffuser_gain", "tau_k_ms"]

# 1 - exp(-t/tau) = 10^(-3/20) at t = build_up_ms (gate_envelope_params()'s own "reached -3dB
# below plateau" threshold) => tau = build_up_ms / -ln(1 - 10^(-3/20)).
BUILD_UP_TO_TAU_A_FACTOR = -1.0 / math.log(1.0 - 10 ** (-3.0 / 20.0))

# Same 5-band onset breakdown as findings.md's own "High: broadband tilt" section, recomputed
# here (not hand-copied from that file's prose) so the exact numbers stay reproducible from the
# real captures rather than transcribed. Mean-subtracted per capture - controls for overall onset
# level differences between captures, isolating the TILT SHAPE rather than raw level.
_TILT_BANDS_HZ = ((20, 120), (120, 500), (500, 2000), (2000, 6000), (6000, 16000))
_TILT_LOW_BAND_INDEX = 0
_TILT_HIGH_BAND_INDEX = 4


def _onset_band_energy_db(path: str, window_ms: float = 20.0) -> np.ndarray:
    channels, sr = load_audio_channels(path)
    mono = channels.mean(axis=0)
    onset = find_onset(mono, sr)
    seg = mono[onset : onset + int(sr * window_ms / 1000)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    freqs = np.fft.rfftfreq(len(seg), 1 / sr)
    energies = np.array([
        10 * np.log10(spec[(freqs >= lo) & (freqs < hi)].sum() + 1e-20) for lo, hi in _TILT_BANDS_HZ
    ])
    return energies - energies.mean()


def _build_tilt_gain_curves(features: dict) -> dict:
    """Real onset-tilt measurement, replacing the fit's own tilt_low_gain/tilt_high_gain - see
    module docstring for why (TILT_REGULARIZATION_WEIGHT suppressed the fitted magnitude to
    roughly a third of the real measured one). Uses the richest available High sweep (same Time
    setting build_curves.py's own tonal-curve logic picks), anchored at High=0 (offset 0 by
    construction, matching every other offset curve's convention here)."""
    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))

    sweep = sorted(
        (c["params"]["high"], c["filename"]) for c in features["captures"] if c["params"]["time"] == richest_time
    )
    print(f"\nTilt gain curves (direct onset measurement) at Time={richest_time}s: "
          f"High values {[h for h, _ in sweep]}")

    baseline_bands = None
    low_pairs, high_pairs = [], []
    for high, filename in sweep:
        bands = _onset_band_energy_db(os.path.join(CAPTURES_DIR, filename))
        if high == 0:
            baseline_bands = bands
        low_pairs.append((high, bands[_TILT_LOW_BAND_INDEX]))
        high_pairs.append((high, bands[_TILT_HIGH_BAND_INDEX]))

    if baseline_bands is None:
        print("  no High=0 point in this sweep - cannot anchor an offset curve, skipping")
        return {}

    baseline_low_db = next(v for h, v in low_pairs if h == 0)
    baseline_high_db = next(v for h, v in high_pairs if h == 0)

    low_offset_points = [(h, 10 ** ((v - baseline_low_db) / 20) - 1.0) for h, v in low_pairs]
    high_offset_points = [(h, 10 ** ((v - baseline_high_db) / 20) - 1.0) for h, v in high_pairs]

    print(f"  low gain offsets:  {[round(v, 4) for _, v in low_offset_points]}")
    print(f"  high gain offsets: {[round(v, 4) for _, v in high_offset_points]}")

    return {
        "high_to_tilt_low_gain_offset": {
            "points": Curve1D([p[0] for p in low_offset_points], [p[1] for p in low_offset_points]).points(),
            "baseline_at_high0": 1.0,
        },
        "high_to_tilt_high_gain_offset": {
            "points": Curve1D([p[0] for p in high_offset_points], [p[1] for p in high_offset_points]).points(),
            "baseline_at_high0": 1.0,
        },
    }


def _build_knee_time_high_offset(features: dict) -> dict:
    """t_knee_ms additive High offset - added after comparing validate.py's real render-vs-
    reference report against ground truth and finding the largest remaining per-capture errors
    concentrated exactly where High is most negative (Time=7.0/High=-7: -67ms; Time=9.8/High=-4/-9:
    -47.5ms), while High=0 captures matched much more closely (-1.6 to -5.5ms). This was already
    documented as a known gap (t_knee_ms's Time-only curve, pooled from H=-3/H=0 points, doesn't
    model any Time*High interaction) - direct measurement DOES support at least an additive High
    offset at one Time setting, the same combination model already used for plateau_droop_db_per_s
    and the tilt gains, so this closes that gap the same documented way rather than leaving it."""
    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))

    sweep = sorted(
        (c["params"]["high"], c["gate"]["knee_time_ms"])
        for c in features["captures"] if c["params"]["time"] == richest_time
    )
    print(f"\nt_knee_ms High offset (direct measurement) at Time={richest_time}s: {sweep}")

    if not any(h == 0 for h, _ in sweep) or len(sweep) < 2:
        print("  no High=0 anchor or too few points - skipping, keeping t_knee_ms Time-only")
        return {}

    baseline = next(v for h, v in sweep if h == 0)
    offset_points = [(h, v - baseline) for h, v in sweep]
    print(f"  offsets: {[round(v, 2) for _, v in offset_points]} (baseline {baseline:.2f}ms)")

    return {
        "high_to_t_knee_ms_offset": {
            "points": Curve1D([p[0] for p in offset_points], [p[1] for p in offset_points]).points(),
        }
    }


def _repool_fit_only_time_params(fitted_raw: dict) -> dict:
    """Rebuilds FIT_ONLY_TIME_PARAMS' Time curves by pooling ALL 9 fitted captures (not just
    High=0), regardless of build_curves.py's own H-timing-neutrality check outcome.

    The concrete bug this fixes: build_curves.py groups every TIME_ONLY_PARAMS curve under ONE
    shared "is H-timing-neutral" gate, checked against t_knee_ms's own spread across High at a
    fixed Time. When that check passes, every TIME_ONLY_PARAMS curve (including these four) pools
    all 9 captures, giving Time coverage down to 0.1s/0.8s (only available at High=-3). When it
    fails - which happened on the fit run that added the onset-density/flatness loss terms
    (core.fit.onset_density_loss/spectral_flatness_loss), shifting t_knee_ms's per-High
    convergence enough to push the check's spread over its 10ms tolerance at Time=7.0/9.8 - EVERY
    TIME_ONLY_PARAMS curve loses the 0.1s/0.8s points, including these four, which have nothing to
    do with t_knee_ms's own neutrality. tau_a_ms/fall_rate_db_per_s/plateau_droop_db_per_s don't
    feel this because they get overwritten by direct measurement above (which always pools
    unconditionally); these four don't have that safety net, so a Time=0.1-9.8 knob position
    querying feedback_gain/damping_weight_mean/diffuser_gain/tau_k_ms via
    InhaltParameterMap::mapTimeKnobToTankParams's curve LINEARLY EXTRAPOLATES below the curve's
    now-2.2s-only floor - and did, concretely: InhaltParameterMapTests caught tau_k_ms going
    negative (kneeSoftnessMs must be finite and positive) and feedback_gain exceeding its own 0.95
    ceiling at short Time knob values, both from extrapolating a steep 2.2s-4.8s slope backward.
    InhaltIRSynth::render() clamps both defensively, so this was never audible, but an
    out-of-documented-range value flowing quietly through an "extrapolated"-flagged path is
    exactly the kind of gap this project's tests are meant to catch before it reaches a user."""
    by_time: dict[float, dict[str, list[float]]] = {}
    for c in fitted_raw["captures"]:
        t = c["params"]["time"]
        bucket = by_time.setdefault(t, {p: [] for p in FIT_ONLY_TIME_PARAMS})
        for p in FIT_ONLY_TIME_PARAMS:
            bucket[p].append(c[p])

    result = {}
    print("\nRe-pooled fit-only Time curves (all 9 captures, not gated on t_knee_ms's own "
          "H-timing-neutrality check - see _repool_fit_only_time_params's own docstring):")
    for p in FIT_ONLY_TIME_PARAMS:
        pairs = sorted((t, sum(vals[p]) / len(vals[p])) for t, vals in by_time.items())
        curve = fit_curve(pairs)
        result[f"time_to_{p}"] = {"points": curve.points()}
        print(f"  {p}: {[round(v, 4) for _, v in curve.points()]}")
    return result


def main() -> None:
    features = json.load(open(FEATURES_PATH))
    curves = json.load(open(CURVES_PATH))
    fitted_raw = json.load(open(FITTED_RAW_PATH))
    curves.update(_repool_fit_only_time_params(fitted_raw))

    # Dict keys here are the MODEL's own parameter names (matching build_curves.py's
    # TIME_ONLY_PARAMS/H_DEPENDENT_TIMING_PARAMS and InhaltParameterMap's field names), not
    # features.json's field names, specifically so this correctly OVERWRITES the same
    # "time_to_<param>" curves.json keys build_curves.py wrote from the (unreliable) fit - e.g.
    # the model's t_knee_ms, not gate_envelope_params()'s own knee_time_ms. A first version of
    # this script used the features.json name directly and silently created a NEW, differently-
    # named, unused curve instead of overwriting the one InhaltParameterMap actually reads -
    # caught by inspecting the generated header before wiring it into C++, not asserted.
    points = {"tau_a_ms": [], "t_knee_ms": [], "fall_rate_db_per_s": [], "plateau_droop_db_per_s": []}
    for c in features["captures"]:
        t = c["params"]["time"]
        g = c["gate"]
        points["tau_a_ms"].append((t, g["build_up_ms"] * BUILD_UP_TO_TAU_A_FACTOR))
        points["t_knee_ms"].append((t, g["knee_time_ms"]))
        points["fall_rate_db_per_s"].append((t, g["fall_rate_db_per_s"]))
        points["plateau_droop_db_per_s"].append((t, g["plateau_droop_db_per_s"]))

    print("Real-measurement-derived Time curves (H=-3 short-Time points pooled with H=0 longer-Time "
          "points - see module docstring):")
    for param, pairs in points.items():
        # average duplicate Time values (e.g. the two H=-3 points at slightly different Time labels
        # don't collide, but keep this robust in case a future capture set has an exact repeat)
        by_time: dict[float, list[float]] = {}
        for t, v in pairs:
            by_time.setdefault(t, []).append(v)
        averaged = sorted((t, sum(vs) / len(vs)) for t, vs in by_time.items())
        curve = fit_curve(averaged)
        curves[f"time_to_{param}"] = {"points": curve.points(), "source": "direct_measurement"}
        print(f"  {param}: {[round(v, 3) for _, v in curve.points()]}")

    curves.update(_build_tilt_gain_curves(features))
    curves.update(_build_knee_time_high_offset(features))

    curves["_notes"]["gate_timing_source_correction"] = (
        "time_to_tau_a_ms / time_to_t_knee_ms / time_to_fall_rate_db_per_s "
        "/ time_to_plateau_droop_db_per_s were REPLACED after this module's own build (see "
        "build_measured_gate_curves.py) - the fit's own values for these were compared per-capture "
        "against features.json's direct measurement and found systematically wrong (e.g. Time=0.1: "
        "measured knee_time_ms=125.1ms, fitted 366.2ms; fall_rate ranged -101 to -1614dB/s fitted vs "
        "a real -141 to -187dB/s measured range). Replaced with curves built directly from "
        "features.json instead - the exact 'fit numbers don't hold up, use direct measurement' "
        "pattern this catalog's AuraDecayGainData.h already established. tilt_low_gain and "
        "tilt_high_gain are ALSO replaced (same reasoning, see module docstring - the fit's "
        "TILT_REGULARIZATION_WEIGHT suppressed their real magnitude by roughly 2-3x). Only "
        "feedback_gain, damping_weight_mean, diffuser_gain, and tilt_pivot_hz are UNCHANGED from the fit - see "
        "build_measured_gate_curves.py's own module docstring for why those four were kept. The "
        "two shortest Time settings (0.1s, 0.8s) exist only at High=-3 in the "
        "real capture set and are pooled into these Time-only curves alongside the High=0 points at "
        "longer Time - an honest compromise (documented, not hidden), since findings.md found the "
        "internal gate-shape breakdown is NOT as cleanly High-neutral as the overall "
        "gate_length_ms_at_20db is."
    )
    curves["_notes"]["t_knee_ms_high_offset"] = (
        "high_to_t_knee_ms_offset was added after a real validate.py run showed the largest "
        "remaining knee-time errors concentrated at very negative High (T=7.0/H=-7: -67ms; "
        "T=9.8/H=-4/-9: -47.5ms) while High=0 matched much more closely - direct measurement at "
        "the richest available High sweep supports at least an additive offset, the same "
        "combination model already used for plateau_droop_db_per_s. Built from only one Time "
        "setting's sweep (2-3 points), same Time-independence caveat as every other High-offset "
        "curve here."
    )
    curves["_notes"]["fit_only_time_params_repooled"] = (
        "feedback_gain/damping_weight_mean/diffuser_gain/tau_k_ms are re-pooled across all 9 "
        "fitted captures HERE (see _repool_fit_only_time_params), independent of build_curves.py's "
        "own H-timing-neutrality gate (which checks t_knee_ms specifically, an unrelated "
        "parameter) - a fit run can fail that check for reasons that have nothing to do with these "
        "four, and did once (see that function's own docstring for the concrete "
        "InhaltParameterMapTests failure this caused before the fix: tau_k_ms going negative and "
        "feedback_gain exceeding 0.95 from extrapolating a curve missing its 0.1s/0.8s points)."
    )
    curves["_notes"]["tau_k_ms_not_measured"] = (
        "tau_k_ms (the gate's knee softness/transition width) has no direct measurement - "
        "core.features has no function for it. Left as the fit's own value (not cross-checked "
        "against ground truth the way the other timing parameters were), flagged here as a real "
        "gap rather than a validated number."
    )

    with open(CURVES_PATH, "w") as fh:
        json.dump(curves, fh, indent=2)
    print(f"\nWrote {CURVES_PATH} (gate-timing curves replaced with direct-measurement versions)")


if __name__ == "__main__":
    main()
