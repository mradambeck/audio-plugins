#!/usr/bin/env python3
"""Phase B5: fits AmbienceFDN against all 65 real captures at once (one batched Adam run, per
core/dsp_primitives.py's frequency-sampling design - a per-capture Python loop over 65 separate
fits would reintroduce the exact sequential-overhead problem Phase A0's spike ruled out).

Fits at a reduced internal sample rate (see FIT_SAMPLE_RATE) rather than the real 44.1kHz - per
Phase A0's measured timings, this is what keeps a 65-capture batch computationally practical.
Dumps raw per-capture fitted values (before any curve-fitting) to fitted_raw.json for hand-review
(B6) - nothing gets curve-fit blind.
"""
import json
import os
import time

import numpy as np
import torch
from scipy.signal import resample_poly

from core.fit import build_loss, fit_model
from core.io import load_audio, load_manifest
from effects.ambience.capture_schema import AMBIENCE_SCHEMA
from effects.ambience.model import AmbienceFDN

HERE = os.path.dirname(__file__)
CAPTURES_DIR = os.path.join(HERE, "captures")
FITTED_RAW_PATH = os.path.join(HERE, "fitted_raw.json")

FIT_SAMPLE_RATE = 11025.0
FIT_DURATION_S = 3.5  # covers the median (~2.3s) capture comfortably; longer captures' tails
# beyond this are truncated for fitting purposes - decay RATE (what build_curves.py needs) is
# well-determined from an earlier window, and Phase D's validation renders at full length/real
# sample rate against the real plugin anyway, independent of this fitting-time truncation.
ITERS = 600  # first-pass fit (~8.9s/step measured on real data -> ~89min); extend if B6's
# hand-review of the loss curve/fitted values suggests it hasn't converged.
LR = 0.02

# B6 hand-review of the first fit found high_band_gain/low_band_gain/damping_weight came out
# clean and monotonic against Time/Low/High (matching findings.md well), but tilt_low_gain/
# tilt_high_gain/output_gain were noisy and occasionally sign-flipped even at the Time-only
# (Low=0, High=0) baseline, where they should stay flat. Output tilt and per-line damping can
# both shape frequency response, so an unregularized fit distributes that ambiguity
# inconsistently across captures. This pulls the tilt gains toward their neutral (no-op) value
# of 1.0 unless the loss genuinely needs them to move, without constraining the well-behaved
# in-loop parameters at all.
#
# Weight swept on the Time-only (Low=0, High=0) subset before committing to a full 65-capture
# run: 0.03 (the first attempt) barely changed anything - tilt_high still sign-flipped negative
# at some settings. 0.3 was still visibly drifting at longer Time captures. 1.0 held both tilt
# gains tightly near 1.0 across the whole sweep (as they should be, since Time alone shouldn't
# move an EQ control) while barely changing the achieved loss (1.117 vs. 1.114 at weight=0.03) -
# confirming this really is pure degeneracy the main loss doesn't care about resolving on its
# own, not a real effect fighting the regularizer.
TILT_REGULARIZATION_WEIGHT = 1.0


def tilt_regularization(model: AmbienceFDN) -> torch.Tensor:
    return TILT_REGULARIZATION_WEIGHT * (
        ((model.tilt_low_gain - 1.0) ** 2).mean() + ((model.tilt_high_gain - 1.0) ** 2).mean()
    )


def resample_to_fit_rate(x: np.ndarray, sr: int) -> np.ndarray:
    if sr == FIT_SAMPLE_RATE:
        return x
    from math import gcd

    g = gcd(int(sr), int(FIT_SAMPLE_RATE))
    return resample_poly(x, int(FIT_SAMPLE_RATE) // g, int(sr) // g)


def load_targets(captures) -> tuple[torch.Tensor, list[dict]]:
    num_samples = int(FIT_SAMPLE_RATE * FIT_DURATION_S)
    targets = np.zeros((len(captures), num_samples), dtype=np.float32)
    meta = []
    for i, capture in enumerate(captures):
        x, sr = load_audio(capture.path)
        x = resample_to_fit_rate(x, sr)
        n = min(len(x), num_samples)
        targets[i, :n] = x[:n]
        meta.append({"filename": os.path.basename(capture.path), "params": capture.params})
    return torch.tensor(targets), meta


def main() -> None:
    captures = load_manifest(CAPTURES_DIR, AMBIENCE_SCHEMA)
    print(f"Loaded {len(captures)} captures")

    targets, meta = load_targets(captures)
    print(f"Target tensor: {targets.shape} at {FIT_SAMPLE_RATE}Hz ({FIT_DURATION_S}s window)")

    model = AmbienceFDN(batch=len(captures), num_samples=targets.shape[1], sample_rate=FIT_SAMPLE_RATE)
    loss_fn = build_loss()

    t0 = time.time()
    result = fit_model(model, targets, loss_fn, iters=ITERS, lr=LR, log_every=50, regularization_fn=tilt_regularization)
    elapsed = time.time() - t0

    print(f"\nFit finished in {elapsed/60:.1f} min - converged={result.converged} "
          f"final_loss={result.final_loss:.6f} diverged_at={result.diverged_at_step}")

    with torch.no_grad():
        high_gain = model.effective_high_band_gain().squeeze(-1).tolist()
        low_gain = model.effective_low_band_gain().squeeze(-1).tolist()
        damping = model.effective_damping_weight().mean(dim=-1).tolist()
        tilt_low = model.tilt_low_gain.squeeze(-1).tolist()
        tilt_high = model.tilt_high_gain.squeeze(-1).tolist()
        output_gain = model.output_gain.squeeze(-1).tolist()

    records = []
    for i, m in enumerate(meta):
        records.append({
            "filename": m["filename"],
            "params": m["params"],
            "high_band_gain": high_gain[i],
            "low_band_gain": low_gain[i],
            "damping_weight_mean": damping[i],
            "tilt_low_gain": tilt_low[i],
            "tilt_high_gain": tilt_high[i],
            "output_gain": output_gain[i],
        })

    with open(FITTED_RAW_PATH, "w") as fh:
        json.dump({
            "fit_sample_rate": FIT_SAMPLE_RATE,
            "fit_duration_s": FIT_DURATION_S,
            "iters": ITERS,
            "lr": LR,
            "converged": result.converged,
            "final_loss": result.final_loss,
            "diverged_at_step": result.diverged_at_step,
            "loss_curve": result.loss_curve,
            "captures": records,
        }, fh, indent=2)

    print(f"Wrote {FITTED_RAW_PATH}")


if __name__ == "__main__":
    main()
