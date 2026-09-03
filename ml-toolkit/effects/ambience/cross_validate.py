#!/usr/bin/env python3
"""Phase B8: cross-validation. Predicts (high_band_gain, low_band_gain, damping_weight_mean) from
curves.json's Time+High combination model for every capture NOT used to build those curves (i.e.
everything except the Low=0/High=0 Time baseline and the Low=0/Time=2.3s High sweep), renders
AmbienceFDN at the predicted values, and compares extracted features (core.features - the same
functions used throughout, not a different metric) against that capture's own direct measurement.

Deliberately includes captures the model is EXPECTED to predict poorly (the (Low=5, High=-8)
sweep, where build_curves.py's own notes flag Low's effect as unmodeled) - the point of this step
is an honest read of what the current Time+High-only model gets right and where it breaks, not a
number to make look good.

full_decay_rt60_s is included but is NOT the primary metric: an early pass using it alone showed
apparent 1000%+ "errors" that turned out to be a measurement artifact, not a real model failure -
fast-decaying renders (short Time, low gain) hit this pipeline's numerical noise floor
(~-90dB, from the FFT-based frequency-sampling render) within well under a second, and the
-6..-70dB linear fit picks up the post-noise-floor wobble as a fake slow decay (confirmed via a
direct raw-dB-level check and a very low full_decay_r2, ~0.18, on the worst offenders - the same
pathology found and understood during Phase B4's model verification, just re-triggered here by
not carrying that lesson into this script the first time). envelope_correlation (scale- and
noise-floor-invariant) is the primary comparison metric as a result.
"""
import json
import math
import os

import numpy as np
import torch

from core.features import analyze_capture, find_onset
from core.interp import Curve1D
from core.io import load_audio, load_manifest
from effects.ambience.capture_schema import AMBIENCE_SCHEMA
from effects.ambience.model import AmbienceFDN, _MAX_DAMPING_WEIGHT, _MAX_FEEDBACK_GAIN
from effects.ambience.fit_ambience import CAPTURES_DIR, FIT_SAMPLE_RATE, FIT_DURATION_S

HERE = os.path.dirname(__file__)
CURVES_PATH = os.path.join(HERE, "curves.json")
REPORT_PATH = os.path.join(HERE, "cross_validation_report.md")


def load_curve(curves: dict, name: str) -> Curve1D:
    points = curves[name]["points"]
    return Curve1D([p[0] for p in points], [p[1] for p in points])


def logit(p: float) -> float:
    p = min(max(p, 1e-6), 1 - 1e-6)
    return math.log(p / (1 - p))


def predict(curves: dict, time: float, high: float) -> dict:
    preds = {}
    for param in ["high_band_gain", "low_band_gain", "damping_weight_mean"]:
        time_curve = load_curve(curves, f"time_to_{param}")
        high_curve = load_curve(curves, f"high_to_{param}_offset")
        base, _ = time_curve.evaluate(time)
        offset, _ = high_curve.evaluate(high)
        preds[param] = base + offset
    return preds


def render_prediction(preds: dict, num_samples: int) -> np.ndarray:
    model = AmbienceFDN(batch=1, num_samples=num_samples, sample_rate=FIT_SAMPLE_RATE)
    with torch.no_grad():
        model.high_band_gain_raw.fill_(logit(preds["high_band_gain"] / _MAX_FEEDBACK_GAIN))
        model.low_band_gain_raw.fill_(logit(preds["low_band_gain"] / _MAX_FEEDBACK_GAIN))
        model.damping_weight_raw.fill_(logit(preds["damping_weight_mean"] / _MAX_DAMPING_WEIGHT))
        model.tilt_low_gain.fill_(1.0)
        model.tilt_high_gain.fill_(1.0)
        model.output_gain.fill_(1.0)
        out = model()
    return out[0].numpy().astype(np.float64)


def envelope_correlation(a: np.ndarray, b: np.ndarray, sr: float, win_s: float = 0.02) -> float:
    """Normalized cross-correlation of windowed RMS envelopes - scale-invariant and robust to
    where the two signals happen to bottom out into noise floor, unlike a linear RT60 fit (see
    module docstring's note on why that metric alone is unreliable here)."""
    win = max(1, int(sr * win_s))
    n = min(len(a), len(b)) // win
    if n < 2:
        return float("nan")
    ea = np.sqrt(np.mean(a[: n * win].reshape(n, win) ** 2, axis=1))
    eb = np.sqrt(np.mean(b[: n * win].reshape(n, win) ** 2, axis=1))
    if np.std(ea) == 0 or np.std(eb) == 0:
        return float("nan")
    return float(np.corrcoef(ea, eb)[0, 1])


def main() -> None:
    curves = json.load(open(CURVES_PATH))
    captures = load_manifest(CAPTURES_DIR, AMBIENCE_SCHEMA)

    # Held out from curve-building: everything except the exact Time baseline (Low=0,High=0) and
    # High sweep (Low=0, Time=2.3s) points.
    held_out = [
        c for c in captures
        if not (c.params["low"] == 0 and c.params["high"] == 0)
        and not (c.params["low"] == 0 and c.params["time"] == 2.3)
    ]
    print(f"{len(held_out)} held-out captures (of {len(captures)} total)")

    num_samples = int(FIT_SAMPLE_RATE * FIT_DURATION_S)
    results = []
    for capture in held_out:
        time, low, high = capture.params["time"], capture.params["low"], capture.params["high"]
        preds = predict(curves, time, high)
        rendered = render_prediction(preds, num_samples)

        ref_x, ref_sr = load_audio(capture.path)
        from scipy.signal import resample_poly
        from math import gcd
        g = gcd(int(ref_sr), int(FIT_SAMPLE_RATE))
        ref_resampled = resample_poly(ref_x, int(FIT_SAMPLE_RATE) // g, int(ref_sr) // g)
        n = min(len(ref_resampled), num_samples)

        ref_onset = find_onset(ref_resampled[:n], FIT_SAMPLE_RATE)
        rendered_onset = find_onset(rendered, FIT_SAMPLE_RATE)
        ref_feat = analyze_capture(ref_resampled[:n], FIT_SAMPLE_RATE)
        rendered_feat = analyze_capture(rendered, FIT_SAMPLE_RATE)

        env_corr = envelope_correlation(
            ref_resampled[ref_onset:n], rendered[rendered_onset:], FIT_SAMPLE_RATE
        )

        results.append({
            "filename": os.path.basename(capture.path),
            "params": capture.params,
            "predicted": preds,
            "envelope_correlation": env_corr,
            "ref_full_decay_rt60_s": ref_feat["full_decay_rt60_s"],
            "rendered_full_decay_rt60_s": rendered_feat["full_decay_rt60_s"],
            "ref_full_decay_r2": ref_feat["full_decay_r2"],
            "rendered_full_decay_r2": rendered_feat["full_decay_r2"],
            "ref_echo_density_half_rise_s": ref_feat["echo_density_half_rise_time_s"],
            "rendered_echo_density_half_rise_s": rendered_feat["echo_density_half_rise_time_s"],
        })

    with open(os.path.join(HERE, "cross_validation_results.json"), "w") as fh:
        json.dump(results, fh, indent=2)

    write_report(results)
    print(f"Wrote {REPORT_PATH}")


def write_report(results: list[dict]) -> None:
    lines = ["# Phase B8 cross-validation report\n"]
    lines.append(
        "Predicts (high_band_gain, low_band_gain, damping_weight_mean) from curves.json's "
        "Time+High model for every capture not used to build those curves, renders AmbienceFDN "
        "at the predicted values, and compares against the real capture. envelope_correlation "
        "(windowed RMS envelope, onset-aligned) is the primary metric - full_decay_rt60_s is "
        "shown too but is unreliable exactly where the render's own full_decay_r2 is low (see "
        "module docstring): fast-decaying renders hit this pipeline's numerical noise floor "
        "before the -6..-70dB linear fit has enough real signal to work with, producing wildly "
        "inflated apparent RT60s that reflect noise-floor wobble, not the model's actual decay.\n"
    )

    lines.append("## Summary\n")
    corrs = [r["envelope_correlation"] for r in results if r["envelope_correlation"] == r["envelope_correlation"]]
    lines.append(f"- Envelope correlation: mean {np.mean(corrs):.3f}, median {np.median(corrs):.3f} "
                  f"across {len(corrs)} held-out captures (1.0 = identical shape).")

    low5 = [r for r in results if r["params"]["low"] == 5 and r["params"]["high"] == -8]
    low5_corrs = [r["envelope_correlation"] for r in low5 if r["envelope_correlation"] == r["envelope_correlation"]]
    other = [r for r in results if not (r["params"]["low"] == 5 and r["params"]["high"] == -8)]
    other_corrs = [r["envelope_correlation"] for r in other if r["envelope_correlation"] == r["envelope_correlation"]]
    if low5_corrs and other_corrs:
        lines.append(f"- (Low=+5, High=-8) sweep specifically (Low's effect unmodeled by design - see "
                      f"build_curves.py's notes): mean envelope correlation {np.mean(low5_corrs):.3f}, "
                      f"vs. {np.mean(other_corrs):.3f} for every other held-out capture - the gap is the "
                      f"real, measurable cost of not modeling Low.")

    reliable = [
        r for r in results
        if (r["ref_full_decay_r2"] or 0) > 0.9 and (r["rendered_full_decay_r2"] or 0) > 0.9
    ]
    if reliable:
        diffs_pct = [
            100 * (r["rendered_full_decay_rt60_s"] - r["ref_full_decay_rt60_s"]) / r["ref_full_decay_rt60_s"]
            for r in reliable
        ]
        lines.append(f"- RT60 diff, restricted to the {len(reliable)}/{len(results)} captures where BOTH the "
                      f"reference's and the render's full_decay_r2 exceed 0.9 (a trustworthy linear fit on "
                      f"both sides): mean {np.mean(diffs_pct):+.1f}%, median {np.median(diffs_pct):+.1f}%.")
    lines.append("")

    lines.append("## Per-setting results\n")
    lines.append("| file | Time | Low | High | env corr | ref RT60 (s) | ref r2 | pred RT60 (s) | pred r2 |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for r in sorted(results, key=lambda r: (r["params"]["low"], r["params"]["high"], r["params"]["time"])):
        p = r["params"]
        ec = r["envelope_correlation"]
        ec_str = f"{ec:.3f}" if ec == ec else "n/a"
        lines.append(
            f"| {r['filename']} | {p['time']} | {p['low']} | {p['high']} | {ec_str} | "
            f"{r['ref_full_decay_rt60_s']} | {r['ref_full_decay_r2']} | "
            f"{r['rendered_full_decay_rt60_s']} | {r['rendered_full_decay_r2']} |"
        )

    with open(REPORT_PATH, "w") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    main()
