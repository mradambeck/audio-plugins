#!/usr/bin/env python3
"""Phase 3: fits NonLinGatedFDN against all real NonLin captures at once (one batched Adam run,
same reasoning as effects/ambience/fit_ambience.py - a per-capture Python loop would reintroduce
the sequential-overhead problem core/dsp_primitives.py's module docstring documents ruling out).

Two deliberate departures from fit_ambience.py, both load-bearing, not stylistic:

  - FIT_SAMPLE_RATE = 44100.0, the real rate, NOT Ambience's reduced 11025Hz. Ambience's 11025Hz
    fit rate has a 5.5kHz Nyquist, which would discard the entire 6-16kHz band where H does its
    measured -9.1dB tilt (see findings.md) - the tilt would be literally unfittable at that rate.
    With only 9 captures (not 65), the resulting larger per-step tensor is still affordable:
    measured ~5.8s/step at batch=9, 1.5s window, 44.1kHz on this machine - ~58min for 600 steps,
    the same order of magnitude as Ambience's own ~89min at a much larger batch and reduced rate.
    This also removes the real_rate_damping_weight conversion step entirely - nothing here is fit
    at one rate and re-derived at another.
  - Every target is ONSET-ALIGNED to sample 0 before fitting (roll each capture so
    core.features.find_onset's own crossing sits at index 0). Ambience's exponential-decay loss is
    largely onset-insensitive (a few ms of onset jitter barely changes an envelope_loss/stft
    comparison against a smooth decay); this module's gate-knee loss is NOT - a 5ms onset
    misalignment would corrupt the fitted t_knee_ms on every single capture, since the knee is a
    genuinely sharp time event, not a slowly-varying trend.

Render window: NUM_SAMPLES covers the longest capture with headroom AND satisfies
NonLinGatedFDN's own aliasing-convergence requirement (see model.py's "Feedback gain ceiling"
docstring section and tests/test_effects_nonlin_model.py's
test_render_is_converged_at_the_chosen_window) - 1.5s clears both bars for every capture in the
expected grid (longest capture ~1.0s; convergence measured comfortable well below 1.5s).

Initialization: gate-shape parameters are initialized from core.features.gate_envelope_params()'s
DIRECT measurement of each target, not blind zeros - unlike Ambience's in-loop gains (which have
no cheap direct measurement to start from), every gate parameter here has one, and starting near
the right answer both speeds convergence and makes a divergent fit easier to diagnose (compare the
fitted result back to the same starting measurement in build_curves.py's cross-check).
"""
from __future__ import annotations

import json
import math
import os
import time

import numpy as np
import torch

from core.features import find_onset, gate_envelope_params
from core.fit import (
    decorrelation_regularizer,
    mid_side_stft_loss,
    onset_density_loss,
    spectral_flatness_loss,
    stft_magnitude_loss,
    weighted_envelope_loss,
)
from core.io import load_audio_channels, load_manifest
from effects.nonlin.capture_schema import NONLIN_SCHEMA
from effects.nonlin.model import (
    FALL_RATE_MAX_DB_S,
    FALL_RATE_MIN_DB_S,
    NonLinGatedFDN,
    PLATEAU_DROOP_MAX_DB_S,
    PLATEAU_DROOP_MIN_DB_S,
    T_KNEE_MAX_MS,
    T_KNEE_MIN_MS,
    TAU_A_MAX_MS,
    TAU_A_MIN_MS,
    TAU_K_MAX_MS,
    TAU_K_MIN_MS,
)

HERE = os.path.dirname(__file__)
CAPTURES_DIR = os.path.join(HERE, "captures")
FITTED_RAW_PATH = os.path.join(HERE, "fitted_raw.json")

FIT_SAMPLE_RATE = 44100.0
FIT_DURATION_S = 1.5  # see module docstring - covers every expected capture plus the aliasing-
# convergence headroom NonLinGatedFDN's feedback-gain ceiling was chosen against.
ITERS = 600
LR = 0.02

# Weights are a starting point, not tuned against real data yet (no captures existed when this
# was written) - re-sweep once Phase 2's findings.md exists, the same way fit_ambience.py's own
# TILT_REGULARIZATION_WEIGHT was swept (0.03/0.3/1.0) against real captures before being trusted.
SPECTRAL_WEIGHT = 1.0
ENVELOPE_WEIGHT = 1.0
MID_SIDE_WEIGHT = 1.0
DECORRELATION_WEIGHT = 0.5

# Added to catch the onset-density/"openness vs. grit" gap DURING fitting rather than after
# building and listening to the plugin (the actual sequence that happened on the first pass here -
# see core.fit.onset_density_loss/spectral_flatness_loss's own docstrings for the specific,
# ear-caught symptom each term targets). Not yet swept against real data the way
# TILT_REGULARIZATION_WEIGHT below eventually was - first values, re-sweep once a fit run with
# these terms exists to compare against.
ONSET_DENSITY_WEIGHT = 0.5
FLATNESS_WEIGHT = 0.3

# Added after the first real fit against findings.md's 9 captures: tilt_low_gain/tilt_high_gain
# (unconstrained, not in the feedback loop) drifted up to 1.3-1.9 (neutral is 1.0) with no pull
# back toward neutral - the identical degeneracy fit_ambience.py's own TILT_REGULARIZATION_WEIGHT
# comment describes (damping/feedback gain and the tilt gains can both shape frequency response,
# so an unregularized fit distributes that ambiguity inconsistently). Not yet swept against real
# data the way Ambience's own weight was (0.03/0.3/1.0) - this is a first value, not a final one;
# re-sweep once a full fit run is cheap enough to iterate on.
TILT_REGULARIZATION_WEIGHT = 1.0


def _logit(fraction: torch.Tensor) -> torch.Tensor:
    fraction = fraction.clamp(1e-4, 1 - 1e-4)
    return torch.log(fraction / (1 - fraction))


def _inverse_bounded_range(value: torch.Tensor, lo: float, hi: float) -> torch.Tensor:
    """Inverse of model._bounded_range() - the raw (pre-sigmoid) parameter that would produce
    `value` under that reparameterization. Fitting-time-only (used to INITIALIZE the model from a
    direct measurement, not part of the model's own forward path), so it lives here rather than
    in model.py's public API."""
    return _logit((value - lo) / (hi - lo))


def onset_align(x: np.ndarray, sr: int) -> np.ndarray:
    """Rolls x so its own find_onset() crossing sits at index 0 - mandatory here (see module
    docstring); trims the pre-onset region entirely rather than wrapping it to the end, since a
    wrapped pre-onset tail would otherwise leak into the render window's own tail region."""
    onset = find_onset(x, sr)
    return x[onset:]


def load_targets(captures) -> tuple[torch.Tensor, list[dict]]:
    """Returns ([batch, 2, num_samples] onset-aligned stereo targets, per-capture metadata)."""
    num_samples = int(FIT_SAMPLE_RATE * FIT_DURATION_S)
    targets = np.zeros((len(captures), 2, num_samples), dtype=np.float32)
    meta = []
    for i, capture in enumerate(captures):
        channels, sr = load_audio_channels(capture.path)
        if sr != FIT_SAMPLE_RATE:
            raise ValueError(
                f"{capture.path}: sample rate {sr} != FIT_SAMPLE_RATE {FIT_SAMPLE_RATE} - "
                "resample the capture set rather than silently mismatching rates (unlike "
                "fit_ambience.py, this module does not resample, since FIT_SAMPLE_RATE is meant "
                "to BE the real capture rate, not a reduced one)."
            )
        if channels.shape[0] == 1:
            channels = np.concatenate([channels, channels], axis=0)
        l = onset_align(channels[0], sr)
        r = onset_align(channels[1], sr)
        n_l, n_r = min(len(l), num_samples), min(len(r), num_samples)
        targets[i, 0, :n_l] = l[:n_l]
        targets[i, 1, :n_r] = r[:n_r]
        meta.append({"filename": os.path.basename(capture.path), "params": capture.params})
    return torch.tensor(targets), meta


def initialize_from_measurement(model: NonLinGatedFDN, targets: torch.Tensor, meta: list[dict]) -> None:
    """Sets every gate-shape raw parameter from core.features.gate_envelope_params()'s direct
    measurement of each target's MONO sum, rather than leaving fit_nonlin at the blind mid-range
    default every raw=0 parameter starts at. Falls back to the mid-range default (raw=0, i.e. the
    range's own midpoint) for any field gate_envelope_params() couldn't measure (e.g. too little
    post-onset data) - printed, not silently skipped."""
    sr = FIT_SAMPLE_RATE
    tau_a_ms, droop, t_knee_ms, fall_rate, tau_k_ms = [], [], [], [], []
    for i in range(targets.shape[0]):
        mono = targets[i].mean(dim=0).numpy()
        gate = gate_envelope_params(mono, sr, onset_idx=0)  # already onset-aligned to 0
        missing = [k for k, v in gate.items() if v is None]
        if missing:
            print(f"  note: {meta[i]['filename']} - gate_envelope_params couldn't measure {missing}, "
                  "using mid-range initialization for those fields")
        tau_a_ms.append(gate["build_up_ms"] if gate["build_up_ms"] is not None else (TAU_A_MIN_MS + TAU_A_MAX_MS) / 2)
        droop.append(gate["plateau_droop_db_per_s"] if gate["plateau_droop_db_per_s"] is not None else 0.0)
        t_knee_ms.append(gate["knee_time_ms"] if gate["knee_time_ms"] is not None else (T_KNEE_MIN_MS + T_KNEE_MAX_MS) / 2)
        fall_rate.append(gate["fall_rate_db_per_s"] if gate["fall_rate_db_per_s"] is not None else (FALL_RATE_MIN_DB_S + FALL_RATE_MAX_DB_S) / 2)
        tau_k_ms.append(5.0)  # knee softness isn't directly measured by gate_envelope_params - a
        # small, gentle default; the fit is free to move it.

    with torch.no_grad():
        model.tau_a_raw.copy_(_inverse_bounded_range(torch.tensor(tau_a_ms).clamp(TAU_A_MIN_MS, TAU_A_MAX_MS).unsqueeze(-1), TAU_A_MIN_MS, TAU_A_MAX_MS))
        model.plateau_droop_raw.copy_(_inverse_bounded_range(torch.tensor(droop).clamp(PLATEAU_DROOP_MIN_DB_S, PLATEAU_DROOP_MAX_DB_S).unsqueeze(-1), PLATEAU_DROOP_MIN_DB_S, PLATEAU_DROOP_MAX_DB_S))
        model.t_knee_raw.copy_(_inverse_bounded_range(torch.tensor(t_knee_ms).clamp(T_KNEE_MIN_MS, T_KNEE_MAX_MS).unsqueeze(-1), T_KNEE_MIN_MS, T_KNEE_MAX_MS))
        model.fall_rate_raw.copy_(_inverse_bounded_range(torch.tensor(fall_rate).clamp(FALL_RATE_MIN_DB_S, FALL_RATE_MAX_DB_S).unsqueeze(-1), FALL_RATE_MIN_DB_S, FALL_RATE_MAX_DB_S))
        model.tau_k_raw.copy_(_inverse_bounded_range(torch.tensor(tau_k_ms).clamp(TAU_K_MIN_MS, TAU_K_MAX_MS).unsqueeze(-1), TAU_K_MIN_MS, TAU_K_MAX_MS))


def nonlin_regularization(model: NonLinGatedFDN, rendered: torch.Tensor) -> torch.Tensor:
    """Takes the ALREADY-RENDERED output rather than calling model() again - an earlier version
    called model() a second time here, doubling the per-step compute for no reason (the
    decorrelation term only needs the same [batch, 2, num_samples] tensor nonlin_loss already
    has)."""
    decorrelation = DECORRELATION_WEIGHT * decorrelation_regularizer(rendered, target_correlation=0.0)
    tilt = TILT_REGULARIZATION_WEIGHT * (
        ((model.tilt_low_gain - 1.0) ** 2).mean() + ((model.tilt_high_gain - 1.0) ** 2).mean()
    )
    return decorrelation + tilt


def nonlin_loss(rendered: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
    """rendered/target: [batch, 2, num_samples]. Combines a per-channel spectral+envelope loss
    (channel flattened into batch, matching stft_magnitude_loss/weighted_envelope_loss's [batch,
    num_samples] convention) with the stereo-aware M/S term."""
    batch, channels, n = rendered.shape
    flat_rendered = rendered.reshape(batch * channels, n)
    flat_target = target.reshape(batch * channels, n)

    per_channel = (
        SPECTRAL_WEIGHT * stft_magnitude_loss(flat_rendered, flat_target)
        + ENVELOPE_WEIGHT * weighted_envelope_loss(flat_rendered, flat_target)
        + ONSET_DENSITY_WEIGHT * onset_density_loss(flat_rendered, flat_target, FIT_SAMPLE_RATE)
        + FLATNESS_WEIGHT * spectral_flatness_loss(flat_rendered, flat_target)
    )
    stereo = MID_SIDE_WEIGHT * mid_side_stft_loss(rendered, target)
    return per_channel + stereo


def main() -> None:
    captures = load_manifest(CAPTURES_DIR, NONLIN_SCHEMA)
    print(f"Loaded {len(captures)} captures")
    if not captures:
        print("No captures found in", CAPTURES_DIR, "- nothing to fit.")
        return

    targets, meta = load_targets(captures)
    print(f"Target tensor: {targets.shape} at {FIT_SAMPLE_RATE}Hz ({FIT_DURATION_S}s window, onset-aligned)")

    model = NonLinGatedFDN(batch=len(captures), num_samples=targets.shape[-1], sample_rate=FIT_SAMPLE_RATE)
    initialize_from_measurement(model, targets, meta)

    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    loss_curve = []
    diverged_at_step = None
    t0 = time.time()
    for step in range(ITERS):
        optimizer.zero_grad()
        rendered = model()
        loss = nonlin_loss(rendered, targets) + nonlin_regularization(model, rendered)
        if not torch.isfinite(loss):
            diverged_at_step = step
            break
        loss.backward()
        grads_finite = all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters())
        if not grads_finite:
            diverged_at_step = step
            break
        optimizer.step()
        loss_curve.append(float(loss.item()))
        if step % 50 == 0:
            print(f"  step {step}: loss={loss.item():.6f}")
    elapsed = time.time() - t0

    converged = diverged_at_step is None and len(loss_curve) > 0
    print(f"\nFit finished in {elapsed/60:.1f} min - converged={converged} "
          f"final_loss={loss_curve[-1] if loss_curve else float('nan'):.6f} diverged_at={diverged_at_step}")

    with torch.no_grad():
        feedback_gain = model.effective_feedback_gain().squeeze(-1).tolist()
        damping = model.effective_damping_weight().mean(dim=-1).tolist()
        tilt_low = model.tilt_low_gain.squeeze(-1).tolist()
        tilt_high = model.tilt_high_gain.squeeze(-1).tolist()
        tilt_pivot_hz = model.tilt_pivot_hz().squeeze(-1).tolist()
        tau_a_ms = (model.tau_a_s().squeeze(-1) * 1000.0).tolist()
        plateau_droop = model.plateau_droop_db_per_s().squeeze(-1).tolist()
        t_knee_ms = (model.t_knee_s().squeeze(-1) * 1000.0).tolist()
        fall_rate = model.fall_rate_db_per_s().squeeze(-1).tolist()
        tau_k_ms = (model.tau_k_s().squeeze(-1) * 1000.0).tolist()
        output_gain = model.output_gain.squeeze(-1).tolist()
        diffuser_gain = model.diffuser_gain().squeeze(-1).tolist()

    records = []
    for i, m in enumerate(meta):
        records.append({
            "filename": m["filename"],
            "params": m["params"],
            "feedback_gain": feedback_gain[i],
            "damping_weight_mean": damping[i],
            "tilt_low_gain": tilt_low[i],
            "tilt_high_gain": tilt_high[i],
            "tilt_pivot_hz": tilt_pivot_hz[i],
            "tau_a_ms": tau_a_ms[i],
            "plateau_droop_db_per_s": plateau_droop[i],
            "t_knee_ms": t_knee_ms[i],
            "fall_rate_db_per_s": fall_rate[i],
            "tau_k_ms": tau_k_ms[i],
            "output_gain": output_gain[i],
            "diffuser_gain": diffuser_gain[i],
        })

    with open(FITTED_RAW_PATH, "w") as fh:
        json.dump({
            "fit_sample_rate": FIT_SAMPLE_RATE,
            "fit_duration_s": FIT_DURATION_S,
            "iters": ITERS,
            "lr": LR,
            "converged": converged,
            "final_loss": loss_curve[-1] if loss_curve else None,
            "diverged_at_step": diverged_at_step,
            "loss_curve": loss_curve,
            "captures": records,
        }, fh, indent=2)
    print(f"Wrote {FITTED_RAW_PATH}")


if __name__ == "__main__":
    main()
