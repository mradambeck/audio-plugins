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
feedback_gain, damping_weight_mean, diffuser_gain - none of these have as complete a
direct-measurement replacement (the tank's internal density/damping isn't something core.features
measures directly at all).

tilt_low_gain/tilt_high_gain/tilt_pivot_hz are ALL overwritten here, not just the two gains as an
earlier version of this module did. Original reasoning for replacing the gains still applies:
TILT_REGULARIZATION_WEIGHT (added to fit_nonlin.py after the first fit diverged, to stop
tilt_low_gain/tilt_high_gain drifting unboundedly - see that module's own comment) pulled the
fitted tilt magnitude back toward neutral so strongly that it undershot the real measured tilt by
roughly 2-3x. tilt_pivot_hz was ORIGINALLY kept from the fit (~4200-4700Hz, "physically plausible",
consistent with this module's own earlier direct tilt_fit() estimate of ~4044Hz) - that turned out
to be wrong in a way that mattered a lot more than "slightly off": a real, ear-caught complaint
("Bringing it down to -9dB to match a -9dB IR it is still much brighter and clear") led to
measuring the REAL captures' whole-decay average spectrum (LTAS), not just the onset window the
original tilt_low_gain/tilt_high_gain replacement used - and found a ~4.5kHz pivot puts the
one-pole shelf's transition too close to the 6-16kHz band being darkened for a first-order slope to
achieve the real captures' own measured spread, EVEN AT EXTREME GAIN (a structural ceiling of
~8.8dB vs. the real ~10-16dB needed at High=-4/-9, not a calibration shortfall). Lowering the pivot
to 1500Hz (an onset-band pivot ESTIMATE this catalog's own earlier work flagged as "less precise"
and discarded in favor of the fit's value) raises the achievable ceiling enough to actually reach
the real target - see _build_tilt_gain_curves' own docstring for the full derivation, including how
the gains themselves are now solved numerically at this new pivot (not read off a closed-form
formula), calibrated against the LTAS rather than the onset window.

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

import torch

from core.dsp_primitives import (
    allpass_chain_transfer_function,
    delay_transfer_function,
    hadamard_matrix,
    one_pole_transfer_function,
    rfft_omega,
)
from core.features import find_onset, gate_envelope_params
from core.interp import Curve1D, fit_curve
from core.io import load_audio_channels

# Tank/diffuser topology, duplicated from effects/nonlin/model.py and
# plugins/inhalt-nonlin/Source/InhaltIRSynth.cpp (NOT imported from model.py, which still uses a
# single SHARED diffuser - see _measure_tank_natural_plateau_droop's own docstring for why this
# duplication exists and the Python/C++ sync gap it works around, rather than papering over it).
_NATURAL_DROOP_SR = 44100.0
_NATURAL_DROOP_LEFT_DELAY_SAMPLES = torch.tensor(
    [481.0, 513.0, 561.0, 657.0, 695.0, 805.0, 813.0, 829.0]
)
_NATURAL_DROOP_RIGHT_DELAY_SAMPLES = torch.tensor(
    [721.0, 763.0, 799.0, 969.0, 1079.0, 1089.0, 1133.0, 1561.0]
)
_NATURAL_DROOP_LEFT_DIFFUSER_SAMPLES = torch.tensor([53.0, 79.0, 115.0])
_NATURAL_DROOP_RIGHT_DIFFUSER_SAMPLES = torch.tensor([61.0, 97.0, 149.0])

# Hand-verified overrides for _measure_tank_natural_plateau_droop, keyed by Time. Real, measured
# necessity, not caution: at Time=9.8/4.8/7.0 the Python raw-tap measurement agrees with a direct
# C++ InhaltRenderIR measurement (built with plateauDroopDbPerSec temporarily forced to 0) within
# ~2.5dB/s - close enough to trust. At Time=2.2 specifically, they disagree by 61dB/s (Python:
# -50.6, C++: +10.6) - traced to Time=2.2's own short plateau window (knee at ~137ms, the
# shortest of the four H=0 baseline points) destabilizing gate_envelope_params()'s swept-
# breakpoint fit in a way sensitive to numerical details this Python reimplementation doesn't
# reproduce exactly. Rather than trust an approximation proven unreliable in this one case, these
# four values are the directly C++-measured ground truth - used as an override wherever present,
# with the Python function still computing (and printing) its own estimate alongside for
# comparison. If a future re-fit changes feedback_gain/damping_weight/diffuser_gain at these Time
# settings, these overrides go stale and should be re-verified the same way (temporarily set
# InhaltIRWorker.cpp's plateauDroopDbPerSec to 0.0f, build InhaltRenderIR, render at High=0,
# measure with core.features.gate_envelope_params, revert).
_NATURAL_DROOP_CPP_VERIFIED_OVERRIDE = {2.2: 10.602, 4.8: -53.579, 7.0: -37.096, 9.8: -38.507}

# fall_rate_db_per_s's own additive-vs-total correction (see _build_fall_rate_curves' docstring)
# has the same shape as plateau_droop's, but its analytical estimate (target_fall_total minus
# this Time's own target_plateau_droop_total) has a second-order error the plateau_droop fix
# didn't: kneeDb(t)'s softplus term isn't fully in its asymptotic-linear regime across the whole
# post-knee fit window (particularly at short Time, where the window is short relative to
# tau_k_ms), so the analytical estimate is measurably too shallow. Verified by direct C++
# measurement (same procedure as _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE: temporarily hardcode
# InhaltIRWorker.cpp's params.fallRateDbPerSec to a candidate, build InhaltRenderIR, render at
# High=0, remeasure with core.features.gate_envelope_params, iterate to convergence, revert).
# Time=2.2 is a KNOWN-UNSTABLE case, not a converged one: its render hits the swept-breakpoint
# fit's float32-noise-floor cliff (~-197dB) well inside the segment-B window at every correction
# magnitude tried (-89 to -94dB/s), so the measured "total" swings wildly (-186 to -190dB/s)
# regardless of the actual parameter - the analytical estimate is kept here deliberately rather
# than chased further, the same practical call _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE's own
# docstring made for this same Time setting. Time=4.8/7.0/9.8 converged cleanly (final residuals
# 0.12dB/s, -2.50dB/s, -0.67dB/s respectively).
_FALL_RATE_CPP_VERIFIED_OVERRIDE = {4.8: -135.814, 7.0: -177.713, 9.8: -170.180}

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

# Fixed tilt pivot (Hz) - see this module's own docstring section on why this replaced the fit's
# own ~4200-4700Hz value and TONAL_PARAMS' removal of "tilt_pivot_hz" in build_curves.py. Same
# hand-measured-constant convention as tiltLowGainBaselineAtHigh0/tiltHighGainBaselineAtHigh0 in
# InhaltParameterMap.cpp (which this value must be kept in sync with).
TILT_PIVOT_HZ = 1500.0

# Split of the single "tilt strength" scalar (see _build_tilt_gain_curves' own docstring on the
# scale gauge-freedom this exists to break) between low-band boost and high-band cut, in dB:
# lowGain_dB = TILT_SPLIT_LOW_FRACTION * s, highGain_dB = -(1 - TILT_SPLIT_LOW_FRACTION) * s.
# Chosen to match this catalog's own earlier onset-band measurement of the real asymmetry
# (+4.5dB low / -9.5dB high at High=-9 - see findings.md and this module's original
# _onset_band_energy_db-based tilt gains, superseded but not contradicted by the LTAS fix below),
# rather than picking an arbitrary 0.5 symmetric split - confirmed numerically that the CHOICE of
# this fraction has ZERO effect on fit quality (the achieved LTAS band levels depend only on the
# ratio lowGain:highGain, i.e. only on `s`, not on how it's split), so this is purely about
# matching a real, previously-measured physical asymmetry, not a fit parameter.
TILT_SPLIT_LOW_FRACTION = 4.5 / (4.5 + 9.5)


def _apply_band_shelf(x: np.ndarray, low_gain: float, high_gain: float, pivot_hz: float, sr: int) -> np.ndarray:
    """Matches plugins/common/dsp/BandShelf.h's processSample() exactly (one-pole low/high split,
    y = low*low_gain + high*high_gain where low is a one-pole lowpass and high = x - low) -
    vectorized via scipy.signal.lfilter since this is calibration-only, not the shipped engine."""
    from scipy.signal import lfilter

    weight = 1.0 - math.exp(-2 * math.pi * pivot_hz / sr)
    low = lfilter([weight], [1.0, -(1.0 - weight)], x)
    high = x - low
    return low * low_gain + high * high_gain


def _ltas_band_levels(x: np.ndarray, sr: int) -> tuple[float, float]:
    """Mean-subtracted (20-120Hz, 6-16kHz) long-term-average-spectrum band levels - see this
    module's own docstring on why LTAS (the WHOLE decay's average spectrum), not
    core.features.gate_envelope_params()'s onset-only window, is the right target for a
    sustained-tone "murky/dark" complaint."""
    from core.features import long_term_average_spectrum

    freqs, spec_db = long_term_average_spectrum(x, sr)
    mean_db = np.mean(spec_db)

    def band_avg(lo: float, hi: float) -> float:
        mask = (freqs >= lo) & (freqs < hi)
        return float(np.mean(spec_db[mask])) - mean_db

    return band_avg(20, 120), band_avg(6000, 16000)


def _build_tilt_gain_curves(features: dict) -> dict:
    """Real tilt measurement, replacing BOTH the fit's own tilt_low_gain/tilt_high_gain/
    tilt_pivot_hz AND this module's own earlier onset-only replacement for the gains (see module
    docstring's "Tilt gain magnitude and pivot" section for the full story - a real, ear-caught
    gap: "Bringing it down to -9dB to match a -9dB IR it is still much brighter and clear", a
    complaint about the SUSTAINED tone the onset-only 20ms window was never representative of).

    Uses the richest available High sweep (same Time setting build_curves.py's own tonal-curve
    logic picks). Gains are solved numerically, not read off a closed-form formula: a one-pole
    shelf's own band-AVERAGED dB shift over a finite band isn't the same as its asymptotic
    low_gain/high_gain endpoint value, so this applies a CANDIDATE shelf (_apply_band_shelf,
    exactly matching BandShelf.h) to the REAL High=0 capture (this Time setting's own measured
    near-neutral reference - isolates the tilt's own transformation from any unrelated tank/gate
    modeling error, unlike calibrating against our own synthesized render) and minimizes the
    difference between the shelved result's own LTAS band levels and each OTHER real capture's own
    measured LTAS band levels.

    A genuine scale gauge-freedom exists (BandShelf's output gets energy-renormalized by
    InhaltIRWorker::normaliseToUnitEnergy() regardless of the tilt's own absolute gain, so these
    mean-subtracted band metrics only constrain the RATIO low_gain:high_gain, not their absolute
    scale - confirmed directly: scaling a fitted (low_gain, high_gain) pair by any constant
    reproduces identical achieved band levels). An earlier version of this function optimized
    (low_gain, high_gain) independently per High point with a naive regularization toward (1, 1) -
    each point exploited the gauge freedom differently and the resulting low_gain CURVE was NOT
    monotonic in High (caught by InhaltParameterMapTests, not by inspection: it failed
    "mapHighKnobToTilt reproduces the two measured endpoints and interpolates monotonically").
    Fixed by collapsing to a SINGLE free parameter per High point - a scalar "tilt strength" s (dB),
    split into low_gain_dB = TILT_SPLIT_LOW_FRACTION*s and high_gain_dB = -(1-TILT_SPLIT_LOW_FRACTION)*s
    - which guarantees both low_gain(High) and high_gain(High) are monotonic BY CONSTRUCTION as
    long as s(High) itself is (verified: s grows with |High| at both real High points, as
    physically expected)."""
    from scipy.optimize import minimize_scalar

    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))

    sweep = sorted(
        (c["params"]["high"], c["filename"]) for c in features["captures"] if c["params"]["time"] == richest_time
    )
    print(f"\nTilt gain curves (LTAS-calibrated, whole-decay, fixed {TILT_PIVOT_HZ:.0f}Hz pivot) at "
          f"Time={richest_time}s: High values {[h for h, _ in sweep]}")

    if not any(h == 0 for h, _ in sweep) or len(sweep) < 2:
        print("  no High=0 anchor or too few points - skipping, keeping tilt curves as previously built")
        return {}

    base_filename = next(f for h, f in sweep if h == 0)
    base_channels, base_sr = load_audio_channels(os.path.join(CAPTURES_DIR, base_filename))
    base_mono = base_channels.mean(axis=0)

    targets = {}
    for high, filename in sweep:
        channels, sr = load_audio_channels(os.path.join(CAPTURES_DIR, filename))
        targets[high] = _ltas_band_levels(channels.mean(axis=0), sr)

    def gains_for_strength(s: float) -> tuple[float, float]:
        return 10 ** (TILT_SPLIT_LOW_FRACTION * s / 20), 10 ** (-(1 - TILT_SPLIT_LOW_FRACTION) * s / 20)

    low_pairs, high_pairs, strengths = [(0, 1.0)], [(0, 1.0)], [(0, 0.0)]
    for high, _ in sweep:
        if high == 0:
            continue
        target_low, target_high = targets[high]

        def loss(s, target_low=target_low, target_high=target_high):
            lg, hg = gains_for_strength(s)
            achieved_low, achieved_high = _ltas_band_levels(
                _apply_band_shelf(base_mono, lg, hg, TILT_PIVOT_HZ, base_sr), base_sr
            )
            return (achieved_low - target_low) ** 2 + (achieved_high - target_high) ** 2

        res = minimize_scalar(loss, bounds=(0.0, 60.0), method="bounded")
        s = res.x
        lg, hg = gains_for_strength(s)
        achieved_low, achieved_high = _ltas_band_levels(
            _apply_band_shelf(base_mono, lg, hg, TILT_PIVOT_HZ, base_sr), base_sr
        )
        print(f"  High={high}: target low={target_low:+.2f} high={target_high:+.2f} -> s={s:.2f}dB -> "
              f"lowGain={lg:.4f} ({20*math.log10(lg):+.2f}dB) highGain={hg:.4f} ({20*math.log10(hg):+.2f}dB) "
              f"(achieved low={achieved_low:+.2f} high={achieved_high:+.2f}, residual={res.fun:.4f})")
        low_pairs.append((high, lg))
        high_pairs.append((high, hg))
        strengths.append((high, s))

    strengths.sort()  # ascending by High; s is bounded >= 0, so it must DECREASE as High -> 0
    if not all(strengths[i][1] >= strengths[i + 1][1] - 1e-6 for i in range(len(strengths) - 1)):
        print("  WARNING: fitted tilt strength is not monotonic in High - check the target LTAS "
              "measurements before trusting the exported curve")

    low_pairs.sort()
    high_pairs.sort()
    baseline_low, baseline_high = 1.0, 1.0
    low_offset_points = [(h, v - baseline_low) for h, v in low_pairs]
    high_offset_points = [(h, v - baseline_high) for h, v in high_pairs]

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


def _curve_value(curves: dict, key: str, x: float) -> float:
    payload = curves[key]
    points = payload["points"]
    if len(points) == 1:
        return points[0][1]
    value, _ = Curve1D([p[0] for p in points], [p[1] for p in points]).evaluate(x)
    return value


def _measure_tank_natural_plateau_droop(time_val: float, curves: dict, num_samples: int = int(_NATURAL_DROOP_SR * 1.0)) -> float:
    """Renders this engine's own tank+diffuser+attack/knee/fall (NOT the explicit
    plateau_droop_db_per_s term - forced to 0 here) at High=0 and the given Time, and measures
    ITS OWN plateau_droop_db_per_s via gate_envelope_params() - the tank's "natural" droop, before
    any explicit compensation is added.

    Exists to fix a real bug, caught by a listening complaint that turned out to be a gate issue
    (see _build_plateau_droop_curves' own docstring): plateauDroopDbPerSec is ADDITIVE to
    whatever droop the tank produces on its own (InhaltIRSynth.cpp's gateEnvelopeDb() sums
    attackDb + plateauDb + kneeDb, where plateauDb = plateauDroopDbPerSec * t is layered on top of
    a tank output that already has its own non-flat decay from feedbackGain < 1). Setting
    plateauDroopDbPerSec directly to the real capture's own MEASURED TOTAL droop - which is what
    an earlier version of this module did - silently double-counts whatever the tank already
    contributes. Verified directly (not just reasoned) at Time=9.8: tank-alone natural droop
    measured -38.5dB/s; real capture's own total measured -10.3dB/s; rendering with the naive
    (uncorrected) explicit value of -10.3dB/s produced a TOTAL of -48.8dB/s (matching
    natural + naive exactly); rendering with the corrected explicit value (target minus natural,
    +28.2dB/s here) produced a total of -11.3dB/s, matching the real target within noise.

    Duplicates model.py/InhaltIRSynth.cpp's tank+diffuser math directly (see the module-level
    constants above) rather than importing model.py, because model.py still uses a single SHARED
    diffuser (one DIFFUSER_DELAY_SAMPLES_AT_44K feeding both channels) - it was never updated to
    the two-independent-diffuser architecture InhaltIRSynth.cpp gained for the direct/early tap
    fix, since diffuser gain fitting doesn't need that distinction. A real, disclosed Python/C++
    sync gap (see model.py's own docstring), worked around here rather than silently ignored -
    fixing it properly means updating model.py's own diffuser architecture and re-fitting, out of
    scope for this specific bug fix."""
    feedback_gain = _curve_value(curves, "time_to_feedback_gain", time_val)
    damping_weight = _curve_value(curves, "time_to_damping_weight_mean", time_val)
    diffuser_gain = _curve_value(curves, "time_to_diffuser_gain", time_val)
    tau_a_ms = _curve_value(curves, "time_to_tau_a_ms", time_val)
    t_knee_ms = _curve_value(curves, "time_to_t_knee_ms", time_val)
    fall_rate = _curve_value(curves, "time_to_fall_rate_db_per_s", time_val)
    tau_k_ms = _curve_value(curves, "time_to_tau_k_ms", time_val)

    H_mix = hadamard_matrix(8).to(torch.complex64)
    omega = rfft_omega(num_samples)

    def render_channel_raw_tap(delay_samples: torch.Tensor, diffuser_delay_samples: torch.Tensor) -> np.ndarray:
        """Inlines render_fdn_impulse_response's own solve rather than calling it, to tap the RAW
        delay-line output (Y itself) instead of that function's damped+mixed Out - matching
        InhaltIRSynth.cpp's Tank::processSample EXACTLY (it sums lineOut[i], the value read at
        the top of each call, before that sample's own damping/mixing - see that function's own
        comment on this being a deliberate, documented Python/C++ tap-convention difference from
        model.py's own _render_tank(), which (like this function very nearly did before being
        fixed) taps the damped+mixed spectrum instead). Verified this fix matters: an earlier
        version of this function using the damped+mixed tap measured natural droop -30.8dB/s at
        Time=9.8, disagreeing with a direct C++ measurement of -38.5dB/s at the same setting;
        this raw-tap version is the one that should be trusted."""
        n_lines = delay_samples.shape[-1]
        Delta = delay_transfer_function(delay_samples, omega)  # [n_freq, L]
        Damp = one_pole_transfer_function(torch.full((n_lines,), damping_weight), omega).transpose(-1, -2)  # [n_freq, L]
        D_diag = torch.diag_embed(Delta)
        Damp_diag = torch.diag_embed(Damp)
        M = feedback_gain * (D_diag @ H_mix @ Damp_diag)
        A = torch.eye(n_lines, dtype=torch.complex64) - M

        diffuser_resp = allpass_chain_transfer_function(diffuser_delay_samples, torch.tensor(diffuser_gain), omega)
        impulse_vec = torch.ones(n_lines, dtype=torch.complex64)
        rhs = (Delta * impulse_vec[None, :] * diffuser_resp[:, None]).unsqueeze(-1)  # [n_freq, L, 1]

        Y = torch.linalg.solve(A, rhs).squeeze(-1)  # [n_freq, L] - the raw delay-line tap
        Out = Y.sum(dim=-1)  # [n_freq]
        return torch.fft.irfft(Out, n=num_samples).numpy() * (1.0 / (8 ** 0.5))

    left = render_channel_raw_tap(_NATURAL_DROOP_LEFT_DELAY_SAMPLES, _NATURAL_DROOP_LEFT_DIFFUSER_SAMPLES)
    right = render_channel_raw_tap(_NATURAL_DROOP_RIGHT_DELAY_SAMPLES, _NATURAL_DROOP_RIGHT_DIFFUSER_SAMPLES)

    t = np.arange(num_samples) / _NATURAL_DROOP_SR
    tau_a = max(tau_a_ms, 0.001) / 1000.0
    tau_k = max(tau_k_ms, 0.001) / 1000.0
    t_knee = t_knee_ms / 1000.0
    attack_db = 20.0 * np.log10(np.clip(1.0 - np.exp(-t / tau_a), 1e-6, None))
    x = (t - t_knee) / tau_k
    softplus = np.where(x > 20.0, x, np.log1p(np.exp(x)))
    knee_db = fall_rate * tau_k * softplus
    gate_lin = 10.0 ** ((attack_db + knee_db) / 20.0)  # deliberately no droop term

    mono = (left * gate_lin + right * gate_lin) / 2.0
    gate = gate_envelope_params(mono, int(_NATURAL_DROOP_SR), onset_idx=0)
    droop = gate.get("plateau_droop_db_per_s")
    if droop is None:
        raise ValueError(f"could not measure natural plateau droop at Time={time_val} - "
                          "gate_envelope_params() found no usable plateau region")

    if time_val in _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE:
        verified = _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE[time_val]
        if abs(verified - droop) > 5.0:
            print(f"  NOTE: Time={time_val} Python natural-droop estimate ({droop:.3f}) disagrees "
                  f"with the C++-verified override ({verified:.3f}) by {abs(verified-droop):.1f}dB/s - "
                  "using the override (see _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE's own comment).")
        return verified
    return droop


def _build_plateau_droop_curves(features: dict, curves: dict) -> dict:
    """plateau_droop_db_per_s's own Time baseline AND High offset, both direct measurement - NOT
    pooled the same way tau_a_ms/t_knee_ms/fall_rate_db_per_s are in main()'s own loop.

    A REAL BUG, caught by a listening complaint, not by inspection: an earlier version of this
    module put plateau_droop_db_per_s in that same pooling dict, which averages EVERY capture at
    a given Time together regardless of High. findings.md's own "High: timing-NEUTRAL overall,
    but NOT damping-neutral" section - and build_curves.py's own H_DEPENDENT_TIMING_PARAMS
    mechanism, which exists SPECIFICALLY for this one parameter - already established that
    plateau_droop_db_per_s is NOT H-neutral, so pooling it across High silently corrupts the H=0
    baseline with whatever other High points happen to exist at that Time. Concretely, at
    Time=9.8: real per-capture droop is -10.3dB/s at High=0 but -43.6/-45.1dB/s at High=-9/-4 -
    averaging all three gave -33.0dB/s, more than 3x too steep for the High=0 case, which rendered
    as an increasingly large broadband level deficit over the plateau (measured directly: the
    render's 100-2000Hz content fell 5-8dB further behind the real capture by 280ms into the
    plateau, in EVERY band checked, not a narrow spectral/EQ issue at all) - the root cause of a
    real "low-mid feels thin" complaint that was initially (and incorrectly) chased as an EQ/tank-
    topology problem before this measurement pinned it on the gate envelope instead.

    Only High=0 captures give a real baseline (Time=2.2/4.8/7.0/9.8 - no coverage below Time=2.2,
    an honest gap: Time=0.1/0.8 only exist at High=-3 in this capture set, and pooling them here
    would reintroduce the exact bug this function exists to avoid). The offset is built from the
    richest available High sweep (Time=9.8: High=-9/-4/0), same convention as
    _build_t_knee_ms_curves. Time=7.0's own independent High=-7 point (offset -48.3dB/s)
    doesn't closely match what Time=9.8's offset curve would predict there (~-34dB/s via linear
    interpolation) - a real, disclosed hint that the offset itself may not be perfectly
    Time-independent, not smoothed over.

    A SECOND bug, found while verifying the first fix rather than assumed fixed: even after
    restricting the baseline to High=0 captures, rendering with plateauDroopDbPerSec set directly
    to the real capture's own MEASURED TOTAL droop still didn't reproduce that total on the
    actual render. Reason: plateauDroopDbPerSec is ADDITIVE to whatever droop the tank ALREADY
    has on its own (InhaltIRSynth.cpp's gateEnvelopeDb() sums attackDb + plateauDb + kneeDb on
    top of a tank output that already decays somewhat from feedbackGain < 1) - setting it to the
    real TOTAL double-counts the tank's own natural contribution. See
    _measure_tank_natural_plateau_droop's own docstring for the direct verification (at
    Time=9.8: naive value produced a rendered total of -48.8dB/s against a -10.3dB/s target;
    the corrected value, target minus the tank's own measured natural droop, produced -11.3dB/s).
    The High offset does NOT need this correction: the tank's natural droop is the same at every
    High (High only touches tilt/gate, never TankParams), so it cancels exactly in the
    High-vs-High=0 subtraction the offset already computes."""
    by_time_h0: dict[float, float] = {}
    for c in features["captures"]:
        if c["params"]["high"] == 0:
            by_time_h0[c["params"]["time"]] = c["gate"]["plateau_droop_db_per_s"]

    print(f"\nplateau_droop_db_per_s Time baseline (High=0 only, direct measurement, "
          f"corrected for the tank's own natural droop):")
    baseline_pairs = []
    for t, measured_total in sorted(by_time_h0.items()):
        natural = _measure_tank_natural_plateau_droop(t, curves)
        corrected = measured_total - natural
        print(f"  Time={t}: measured_total={measured_total:.3f}  tank_natural={natural:.3f}  "
              f"-> corrected_explicit={corrected:.3f}")
        baseline_pairs.append((t, corrected))

    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))
    sweep = sorted(
        (c["params"]["high"], c["gate"]["plateau_droop_db_per_s"])
        for c in features["captures"] if c["params"]["time"] == richest_time
    )
    print(f"plateau_droop_db_per_s High offset (direct measurement) at Time={richest_time}s: {sweep}")

    result = {}
    if len(baseline_pairs) >= 2:
        result["time_to_plateau_droop_db_per_s"] = {
            "points": Curve1D([p[0] for p in baseline_pairs], [p[1] for p in baseline_pairs]).points(),
            "source": "direct_measurement_high0_only_natural_droop_corrected",
        }
    if any(h == 0 for h, _ in sweep) and len(sweep) >= 2:
        baseline = next(v for h, v in sweep if h == 0)
        offset_points = [(h, v - baseline) for h, v in sweep]
        result["high_to_plateau_droop_db_per_s_offset"] = {
            "points": Curve1D([p[0] for p in offset_points], [p[1] for p in offset_points]).points(),
        }
        print(f"  offsets: {[round(v, 3) for _, v in offset_points]} (baseline {baseline:.3f}dB/s)")
    return result


def _isotonic_nondecreasing(pairs: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """Pool-adjacent-violators: the smallest edit (by weighted averaging of adjacent points) that
    makes a sorted-by-x sequence of y values non-decreasing. Used by _build_t_knee_ms_curves to
    fix a real data quirk, not to hide one: Time=7.0's own directly-measured High=0 knee_time_ms
    (190.0ms) is genuinely BELOW Time=4.8's (204.875ms) in the raw 9-capture set - a single-
    capture measurement noise artifact, not a real hardware non-monotonicity, confirmed by the
    ALREADY-VALIDATED, independently-measured gate_length_ms_at_20db table (see analyze.py's own
    EXPECTED_GATE_LENGTH_MS_AT_20DB), which shows a strictly increasing 216.6ms -> 278.1ms across
    the same two Time settings via a more robust threshold-crossing measurement, not the more
    noise-sensitive swept-breakpoint knee fit. The OLD naive pooling accidentally masked this by
    averaging in Time=7.0's own uncorrected High=-7 capture (287.8ms), which happened to be large
    enough to pull the average back above Time=4.8's value - not a real fix, just a coincidence
    that stopped applying once the High=-7 point was correctly converted to its own H=0-equivalent
    value first."""
    xs = [p[0] for p in pairs]
    # Each block is [mean_value, total_weight, [original_indices...]]. Standard PAVA: scan left
    # to right, merging the new point with the previous block whenever it would violate
    # non-decreasing order, then re-checking the merged block against ITS OWN predecessor.
    blocks: list[list] = []
    for y in [p[1] for p in pairs]:
        blocks.append([y, 1.0])
        while len(blocks) > 1 and blocks[-2][0] > blocks[-1][0]:
            prev_val, prev_w = blocks[-2][0], blocks[-2][1]
            cur_val, cur_w = blocks[-1][0], blocks[-1][1]
            merged_val = (prev_val * prev_w + cur_val * cur_w) / (prev_w + cur_w)
            blocks[-2:] = [[merged_val, prev_w + cur_w]]
    result_ys: list[float] = []
    idx = 0
    for value, weight in blocks:
        count = int(round(weight))
        result_ys.extend([value] * count)
        idx += count
    return list(zip(xs, result_ys))


def _build_t_knee_ms_curves(features: dict) -> dict:
    """t_knee_ms's own Time baseline AND High offset - REPLACES an earlier pair of functions
    (one naive pooling-loop entry for the baseline, a separate _build_knee_time_high_offset for
    the offset) that were internally inconsistent with each other in exactly the way
    _build_plateau_droop_curves' own docstring describes for that parameter: the SAME
    double-counting bug pattern, found by tracing through a real, ear-caught "reverb rings out
    too long at High=0" complaint.

    The offset itself (direct measurement at Time=9.8's own High sweep: +87.8ms at High=-4/-9)
    was already correctly built and EXPORTED, but a real, ear-caught complaint led to it being
    tried and REVERTED before this fix (see README.md's own history) - it badly regressed the two
    short-Time captures (knee error ~2-4ms -> ~91-93ms) because the Time-only BASELINE it was
    being added to was never a clean High=0 reference to begin with: Time=0.1s/0.8s only exist at
    High=-3 in this capture set, and the old naive pooling loop folded their raw (H=-3-flavored)
    knee_time_ms directly into the "Time-only" baseline as if it were already at High=0. Adding a
    FURTHER High-dependent offset on top of a baseline that already implicitly contains an H=-3
    effect double-counts it - the exact reason the fix was reverted. The SAME issue, less
    severely, also affects Time=7.0 (its own H=-7 capture was pooled straight in with its H=0
    capture) and Time=9.8 (H=-4/-9 pooled straight in with H=0).

    Fixed by converting EVERY capture to an H=0-EQUIVALENT value before building the Time
    baseline: corrected = knee_time_ms - offset_curve(High), using the offset curve built from
    Time=9.8's own sweep (the same "assume the offset is roughly Time-independent" caveat this
    module's other High-offset curves already carry, not a new assumption). For the four H=0
    captures this is a no-op (offset(0)=0); for Time=0.1/0.8 it recovers an estimated H=0-
    equivalent baseline (125.1ms -> ~59.3ms, 127.1ms -> ~61.3ms - a genuinely large correction,
    not a small tweak, which is exactly why leaving it uncorrected before adding the offset broke
    so badly); for Time=7.0/9.8's own negative-High captures, the correction should (and, checked
    directly, does) bring them very close to their own real High=0 sibling capture's value,
    confirming the offset curve and the correction are self-consistent rather than fighting each
    other."""
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
        print("  no High=0 anchor or too few points - skipping, keeping t_knee_ms Time-only "
              "(naively pooled, the known-imperfect fallback)")
        return {}

    baseline_at_richest_time = next(v for h, v in sweep if h == 0)
    offset_points = [(h, v - baseline_at_richest_time) for h, v in sweep]
    print(f"  offsets: {[round(v, 2) for _, v in offset_points]} (baseline {baseline_at_richest_time:.2f}ms)")
    offset_curve = Curve1D([p[0] for p in offset_points], [p[1] for p in offset_points])

    print("t_knee_ms Time baseline (every capture converted to an H=0-equivalent value first):")
    by_time: dict[float, list[float]] = {}
    for c in features["captures"]:
        t = c["params"]["time"]
        high = c["params"]["high"]
        offset, extrapolated = offset_curve.evaluate(high)
        corrected = c["gate"]["knee_time_ms"] - offset
        flag = " (offset extrapolated)" if extrapolated else ""
        print(f"  Time={t} High={high}: raw={c['gate']['knee_time_ms']:.3f}  "
              f"offset(High)={offset:.3f}  -> corrected={corrected:.3f}{flag}")
        by_time.setdefault(t, []).append(corrected)
    baseline_pairs = sorted((t, sum(vs) / len(vs)) for t, vs in by_time.items())
    print(f"  averaged per Time: {[(t, round(v, 3)) for t, v in baseline_pairs]}")

    monotonic_pairs = _isotonic_nondecreasing(baseline_pairs)
    if monotonic_pairs != baseline_pairs:
        print(f"  NOTE: averaged baseline was not monotonic in Time - applied isotonic regression "
              f"(see _isotonic_nondecreasing's own docstring): "
              f"{[(t, round(v, 3)) for t, v in monotonic_pairs]}")

    return {
        "time_to_t_knee_ms": {
            "points": fit_curve(monotonic_pairs).points(),
            "source": "direct_measurement_h0_equivalent_corrected_isotonic",
        },
        "high_to_t_knee_ms_offset": {
            "points": Curve1D([p[0] for p in offset_points], [p[1] for p in offset_points]).points(),
        },
    }


def _build_fall_rate_curves(features: dict, curves: dict) -> dict:
    """fall_rate_db_per_s's own Time baseline AND High offset - REPLACES its entry in main()'s
    naive pooling loop, which had BOTH bugs already found and fixed for plateau_droop_db_per_s and
    t_knee_ms (see those functions' own docstrings), never re-examined here until a real "the delay
    tail still isn't quite a match" listening complaint prompted it.

    Bug 1 (naive High-pooling): the naive loop averaged fall_rate_db_per_s across EVERY High value
    at a given Time, exactly like plateau_droop_db_per_s's own Bug 1. Confirmed directly:
    Time=9.8's old curve value (-179.557dB/s) is exactly the mean of its three real captures
    (-171.98 at High=0, -179.43 at High=-4, -187.26 at High=-9); Time=7.0's (-177.69) is exactly
    the mean of -173.11 (High=0) and -182.27 (High=-7). Fixed the same way as t_knee_ms: every
    capture is converted to an H=0-equivalent TARGET first (raw - offset_curve(High), offset curve
    built from Time=9.8's own richest sweep), THEN averaged per Time, THEN isotonic-regressed.

    Bug 2 (additive-vs-total double-counting): the SAME architectural bug plateau_droop_db_per_s
    had. InhaltIRSynth.cpp's gateEnvelopeDb() is attackDb + plateauDb(t) + kneeDb(t), where
    plateauDb(t) = plateauDroopDbPerSec * t NEVER turns off at the knee - it keeps contributing
    for the whole render. So the render's own post-knee slope (what gate_envelope_params() calls
    fall_rate_db_per_s when it re-measures the render) is the tank's implicit natural decay PLUS
    plateauDroopDbPerSec's own (already natural-droop-corrected) explicit contribution PLUS
    fallRateDbPerSec - and by construction of the earlier plateau_droop_db_per_s fix, the first two
    terms already sum to exactly this Time's real target_plateau_droop_total. Setting
    fallRateDbPerSec directly to the real measured target_fall_total therefore double-counts the
    plateau's own ongoing contribution a second time. Verified directly (same procedure as
    _measure_tank_natural_plateau_droop, see _FALL_RATE_CPP_VERIFIED_OVERRIDE's own docstring for
    the exact steps): at Time=9.8/High=0, writing the raw target (-171.98) produced a rendered
    total of roughly -217dB/s (extrapolating from the -143.65 to -164.83 range measured at smaller
    magnitudes); the corrected value (target_fall_total minus this Time's own target_plateau_droop_
    total, then empirically refined - see the override dict) produced -172.65dB/s, a 0.67dB/s
    residual.

    The analytical estimate (target_fall - plateau_target) undershoots the needed magnitude by a
    fairly consistent ~5-12% - not the tank's natural droop (already accounted for), but
    kneeDb(t)'s own softplus term not being fully in its linear asymptotic regime across the whole
    post-knee fit window. _FALL_RATE_CPP_VERIFIED_OVERRIDE supplies the empirically-converged
    value per Time; the analytical estimate is printed alongside it as a sanity check (and used
    as-is for any Time not in the override, same fallback convention as
    _measure_tank_natural_plateau_droop).

    The WRITTEN High offset (added on top of the Time baseline by InhaltParameterMap.cpp, mirroring
    t_knee_ms's own architecture) is target_fall_offset(High) - target_plateau_offset(High): both
    already-measured TARGET offsets (this function's own sweep; plateau_droop_db_per_s's own
    high_to_plateau_droop_db_per_s_offset, already in curves by the time this runs). The tank's
    natural-droop term cancels in a High-vs-High=0 subtraction the same way it does for
    plateau_droop's own offset (see that function's docstring), so this doesn't need its own
    override table - verified directly at Time=9.8/High=-4 (predicted written value -142.878dB/s,
    measured render total -175.34 vs. the real target -179.43, a 4.09dB/s residual - a real but
    modest gap, consistent with the offset's own "assumed roughly Time-independent" caveat every
    other High-offset curve in this module already carries)."""
    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))

    sweep = sorted(
        (c["params"]["high"], c["gate"]["fall_rate_db_per_s"])
        for c in features["captures"] if c["params"]["time"] == richest_time
    )
    print(f"\nfall_rate_db_per_s High offset (direct measurement, TARGET total) at "
          f"Time={richest_time}s: {sweep}")

    if not any(h == 0 for h, _ in sweep) or len(sweep) < 2:
        print("  no High=0 anchor or too few points - skipping, keeping fall_rate_db_per_s "
              "Time-only (naively pooled, the known-imperfect fallback)")
        return {}

    baseline_at_richest_time = next(v for h, v in sweep if h == 0)
    target_offset_points = [(h, v - baseline_at_richest_time) for h, v in sweep]
    print(f"  target offsets: {[round(v, 2) for _, v in target_offset_points]} "
          f"(baseline {baseline_at_richest_time:.2f}dB/s)")
    target_offset_curve = Curve1D([p[0] for p in target_offset_points], [p[1] for p in target_offset_points])

    print("fall_rate_db_per_s TARGET Time baseline (every capture converted to an H0-equivalent "
          "target first):")
    by_time: dict[float, list[float]] = {}
    for c in features["captures"]:
        t = c["params"]["time"]
        high = c["params"]["high"]
        offset, extrapolated = target_offset_curve.evaluate(high)
        corrected_target = c["gate"]["fall_rate_db_per_s"] - offset
        flag = " (offset extrapolated)" if extrapolated else ""
        print(f"  Time={t} High={high}: raw={c['gate']['fall_rate_db_per_s']:.3f}  "
              f"offset(High)={offset:.3f}  -> H0_target={corrected_target:.3f}{flag}")
        by_time.setdefault(t, []).append(corrected_target)
    target_baseline_pairs = sorted((t, sum(vs) / len(vs)) for t, vs in by_time.items())
    print(f"  target averaged per Time: {[(t, round(v, 3)) for t, v in target_baseline_pairs]}")

    # fall_rate_db_per_s gets MORE NEGATIVE (steeper) as Time increases - the OPPOSITE direction
    # from t_knee_ms's own non-decreasing trend - so isotonic regression is applied to the negated
    # sequence (enforcing non-decreasing there = non-increasing in the real, signed values), then
    # negated back. Applying _isotonic_nondecreasing directly here was a real bug caught while
    # verifying this fix, not assumed correct from the t_knee_ms precedent: it collapsed all 6
    # points to a single flat value, because a large early violation (Time=0.1 -> 0.8 going from
    # -145.5 to -137.5, less steep) chained through the whole non-decreasing constraint,
    # overriding the real Time=4.8/7.0/9.8 trend it should never have touched.
    negated_monotonic = _isotonic_nondecreasing([(t, -v) for t, v in target_baseline_pairs])
    monotonic_target_pairs = [(t, -v) for t, v in negated_monotonic]
    if monotonic_target_pairs != target_baseline_pairs:
        print(f"  NOTE: target baseline was not monotonic in Time - applied isotonic regression "
              f"(negated, since this parameter decreases with Time): "
              f"{[(t, round(v, 3)) for t, v in monotonic_target_pairs]}")

    print("fall_rate_db_per_s WRITTEN correction (subtracting this Time's own target plateau "
          "droop total, since plateauDb(t) keeps contributing after the knee - see docstring):")
    written_pairs = []
    for t, target_fall in monotonic_target_pairs:
        plateau_target, plateau_extrapolated = Curve1D(
            [p[0] for p in curves["time_to_plateau_droop_db_per_s"]["points"]],
            [p[1] for p in curves["time_to_plateau_droop_db_per_s"]["points"]],
        ).evaluate(t)
        analytical = target_fall - plateau_target
        override = _FALL_RATE_CPP_VERIFIED_OVERRIDE.get(t)
        written = override if override is not None else analytical
        flag = " (plateau extrapolated)" if plateau_extrapolated else ""
        note = "" if override is None else f"  [C++-verified override, analytical was {analytical:.3f}]"
        print(f"  Time={t}: target_fall={target_fall:.3f}  plateau_target={plateau_target:.3f}{flag}  "
              f"-> written={written:.3f}{note}")
        written_pairs.append((t, written))

    plateau_offset_points = curves.get("high_to_plateau_droop_db_per_s_offset", {}).get("points", [])
    written_offset_points = target_offset_points
    if len(plateau_offset_points) >= 2:
        plateau_offset_curve = Curve1D([p[0] for p in plateau_offset_points], [p[1] for p in plateau_offset_points])
        written_offset_points = []
        for h, fall_offset in target_offset_points:
            plateau_offset, _ = plateau_offset_curve.evaluate(h)
            written_offset_points.append((h, fall_offset - plateau_offset))
    print(f"  written offsets: {[round(v, 3) for _, v in written_offset_points]}")

    return {
        "time_to_fall_rate_db_per_s": {
            "points": fit_curve(written_pairs).points(),
            "source": "direct_measurement_h0_equivalent_additive_corrected",
        },
        "high_to_fall_rate_db_per_s_offset": {
            "points": Curve1D([p[0] for p in written_offset_points], [p[1] for p in written_offset_points]).points(),
        },
    }


# Hand-verified: InhaltIRSynth's OWN existing knee transition (kneeSoftnessMs/softplus, with
# earlyExcessDb at its neutral default 0.0) already produces a small amount of curvature near the
# knee on its own, before any new correction is added - this is that render's own "natural" post-
# knee excess (core.features.post_knee_excess_db, measured directly via InhaltRenderIR at
# earlyExcessDb=0.0f) at each of the 9 real capture settings, needed to correctly compute the
# additive correction the same way _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE/
# _FALL_RATE_CPP_VERIFIED_OVERRIDE do. Unlike those two, this isn't from an ongoing tank/gate
# process (feedbackGain<1 or plateauDb(t) never turning off) - it's just kneeSoftnessMs's own
# residual curvature - so it varies more with Time (0.22-6.07dB) than a "there's always some
# amount of this" constant would suggest, tracking kneeSoftnessMs's own per-Time swings (Time=4.8
# has an outlier 29.7ms tau_k_ms, for instance). Keyed by (time, high) rather than time alone,
# since (unlike plateau_droop's own natural droop) this was measured at every one of the 9 real
# capture settings directly, not needing per-Time interpolation.
_EARLY_EXCESS_NATURAL_CPP_MEASURED = {
    (0.1, -3): 5.58, (0.8, -3): 6.07, (2.2, 0): 0.22, (4.8, 0): 0.78,
    (7.0, -7): 0.34, (7.0, 0): 0.60, (9.8, -4): 1.19, (9.8, -9): 1.21, (9.8, 0): 1.21,
}


def _build_early_excess_curves(features: dict) -> dict:
    """earlyExcessDb's own Time baseline AND High offset - a NEW gate-shape term (see
    InhaltIRSynth.h's own comment on the field), not a replacement for an existing bug, added
    after a real "let's go back to trying to get it to have the same decay and timing as the
    convolution" complaint (the Time-knob/gate-length alignment issue first found, then
    deliberately deferred in favor of investigating a separate smear complaint - see README.md's
    own history).

    Root cause (found by comparing a real capture's envelope directly against what a straight-
    line extrapolation of its own fall_rate_db_per_s would predict, not assumed): real NonLin
    captures' post-knee fall is CURVED, not a single constant dB/s rate.
    core.features.post_knee_excess_db quantifies this directly: 20ms after the knee, 7 of 9 real
    captures sit -1 to -3.6dB BELOW what fall_rate_db_per_s's own straight-line average predicts
    (a steeper-than-average initial drop) - but the two captures with the shallowest knee (closest
    to the peak: Time=7.0/9.8 at High=0) show the OPPOSITE, +1.8 to +2.1dB ABOVE the line. A real,
    heterogeneous property of the hardware (not noise - both same-Time/different-High pairs at
    Time=7.0 and Time=9.8 show the same High=0-is-different pattern), which is exactly why this
    needs a directly-measured, per-Time/High-calibrated correction rather than one constant.

    Same additive-vs-total shape as plateau_droop_db_per_s/fall_rate_db_per_s: InhaltIRSynth's own
    kneeSoftnessMs/softplus transition already contributes SOME curvature on its own (measured
    directly per setting - see _EARLY_EXCESS_NATURAL_CPP_MEASURED's own comment), so what's
    written must be target_excess minus that Time/High's own natural excess, not the raw target.

    Same H0-equivalent-baseline-plus-offset architecture as _build_fall_rate_curves/
    _build_t_knee_ms_curves (offset built from Time=9.8's own richest sweep; every capture
    converted to an H0-equivalent WRITTEN value before averaging per Time) - see those functions'
    own docstrings for why naive pooling across High would be wrong here too. No isotonic
    regression applied (unlike t_knee_ms/fall_rate_db_per_s): this parameter has no physical
    reason to be monotonic in Time, and the measured baseline (-6.6 to +0.9dB) isn't close to
    monotonic, matching tau_a_ms's own simpler (no-monotonicity-assumed) convention instead."""
    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))

    written_by_time_high: dict[tuple[float, float], float] = {}
    for c in features["captures"]:
        t, h = c["params"]["time"], c["params"]["high"]
        target = c["gate"]["post_knee_excess_db"]
        natural = _EARLY_EXCESS_NATURAL_CPP_MEASURED.get((t, h))
        if target is None or natural is None:
            print(f"  Time={t} High={h}: no post_knee_excess_db target or natural override - skipping")
            continue
        written_by_time_high[(t, h)] = target - natural

    sweep = sorted((h, v) for (t, h), v in written_by_time_high.items() if t == richest_time)
    print(f"\nearly_excess_db High offset (written, target minus natural) at Time={richest_time}s: {sweep}")
    if not any(h == 0 for h, _ in sweep) or len(sweep) < 2:
        print("  no High=0 anchor or too few points - skipping early_excess_db entirely")
        return {}

    baseline_at_richest_time = next(v for h, v in sweep if h == 0)
    offset_points = [(h, v - baseline_at_richest_time) for h, v in sweep]
    print(f"  offsets: {[round(v, 3) for _, v in offset_points]} (baseline {baseline_at_richest_time:.3f}dB)")
    offset_curve = Curve1D([p[0] for p in offset_points], [p[1] for p in offset_points])

    print("early_excess_db Time baseline (every capture converted to an H0-equivalent written value first):")
    by_time: dict[float, list[float]] = {}
    for (t, h), written in written_by_time_high.items():
        offset, extrapolated = offset_curve.evaluate(h)
        corrected = written - offset
        flag = " (offset extrapolated)" if extrapolated else ""
        print(f"  Time={t} High={h}: written={written:.3f}  offset(High)={offset:.3f}  "
              f"-> H0_equivalent={corrected:.3f}{flag}")
        by_time.setdefault(t, []).append(corrected)
    baseline_pairs = sorted((t, sum(vs) / len(vs)) for t, vs in by_time.items())
    print(f"  averaged per Time: {[(t, round(v, 3)) for t, v in baseline_pairs]}")

    return {
        "time_to_early_excess_db": {
            "points": fit_curve(baseline_pairs).points(),
            "source": "direct_measurement_h0_equivalent_additive_corrected",
        },
        "high_to_early_excess_db_offset": {
            "points": Curve1D([p[0] for p in offset_points], [p[1] for p in offset_points]).points(),
        },
    }


_PER_BAND_GROUPS = {
    "low": ["44-89Hz", "88-177Hz"],
    "mid": ["177-354Hz", "354-707Hz", "707-1414Hz", "1414-2828Hz", "2828-5657Hz", "5657-11314Hz"],
    "high": ["11314-22049Hz"],
}

# Hand-verified via direct C++ measurement (same procedure as _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE):
# temporarily force all three plateauDroop{Low,Mid,High}DbPerSec fields to 0.0f in
# InhaltIRWorker.cpp, build InhaltRenderIR, render at High=0, measure core.features.
# band_gate_params() per analysis band, aggregate into low/mid/high via _PER_BAND_GROUPS, revert.
# Specific to InhaltIRSynth.cpp's own splitPoleStages=2 cutoff-compensated cascaded crossover split
# (see that file's own comment on splitLowL/splitHighL for the two earlier crossover designs this
# superseded) - these values would need re-measuring if the crossover ever changes again.
#
# Time=2.2's own LOW band was a genuine outlier under the FIRST (single, uncompensated one-pole
# stage) crossover - real mode-beating, not a fit bug: a gentle 6dB/octave split let in enough
# adjacent-band energy that the tank's own widely-spaced low-frequency modes beat against each
# other within Time=2.2's own short plateau, producing a genuinely oscillating envelope
# gate_envelope_params()'s swept-breakpoint fit couldn't sensibly fit (-542.7dB/s). The current,
# steeper crossover resolved this as a side effect, same as it did under the (since-reverted)
# 4-stage attempt - measured cleanly with the standard method now, no special-casing needed for
# this entry. The REAL capture's own target at this same band/Time still needs its own
# robust-regression override (see _TARGET_DROOP_ROBUST_OVERRIDE below) - that side is unrelated to
# this engine's own crossover and unaffected by any of these changes.
_NATURAL_DROOP_PER_BAND_CPP_MEASURED = {
    (2.2, "low"): -62.808, (2.2, "mid"): -28.676, (2.2, "high"): -120.876,
    (4.8, "low"): -53.481, (4.8, "mid"): -46.669, (4.8, "high"): -110.615,
    (7.0, "low"): -28.633, (7.0, "mid"): -23.453, (7.0, "high"): -103.504,
    (9.8, "low"): -15.994, (9.8, "mid"): -21.232, (9.8, "high"): -102.926,
}

# Time=2.2's own low-band TARGET (the real capture's own measured value, not this engine's) needed
# the same robust-regression treatment - see _NATURAL_DROOP_PER_BAND_CPP_MEASURED's own comment
# for the full story and the direct verification that this is real mode-beating, not a fit bug.
_TARGET_DROOP_ROBUST_OVERRIDE = {
    (2.2, "low"): -1.966,
}


def _build_per_band_gate_curves(features: dict) -> dict:
    """Builds Time baseline + High offset curves for the SIX parameters that replaced the single
    broadband plateau_droop_db_per_s/fall_rate_db_per_s after a real "huffy, low-mid resonance...
    rings out longer than the IR's" complaint (see InhaltIRSynth.h's own comment on
    Params::plateauDroopLowDbPerSec for the full architectural story - real hardware's decay rate
    varies dramatically by octave band in a way one broadband rate structurally cannot represent,
    and no single tank dampingWeight value can reproduce it either).

    Same architecture as _build_fall_rate_curves/_build_plateau_droop_curves, generalized across
    three bands (_PER_BAND_GROUPS: low = 44-177Hz, mid = 177Hz-11.3kHz, high = 11.3-22kHz -
    matching InhaltIRSynth.h's own lowMidCrossoverHz/midHighCrossoverHz exactly) instead of
    duplicated three times:

    - droop: H0-equivalent Time baseline (High=0 captures only - Time=0.1/0.8 have no H=0 capture
      at all and are left to the curve's own extrapolation, same honest gap as the broadband
      version), corrected for this engine's own natural per-band droop (the SAME additive-vs-total
      shape plateau_droop_db_per_s's own fix had - InhaltIRSynth's plateauDb(t) terms are layered
      on top of whatever the tank naturally does, not a replacement for it), using
      _NATURAL_DROOP_PER_BAND_CPP_MEASURED as the direct-measurement override.
    - fall_rate: covers all 9 captures (unlike droop), using the SAME analytical relationship
      _build_fall_rate_curves established: since plateauDb(t) never turns off at the knee, what's
      written must be target_fall_total minus this Time's own TARGET droop total (not the natural
      droop, and not the corrected/written droop - the real target, since the written droop
      combined with the tank's own natural contribution is what reproduces that real target on the
      actual render).
    - High offset for both: built from Time=9.8's own richest sweep, found to be small (<2dB/s
      across High=-9/-4/0 in every band) - MUCH smaller than the single broadband droop's own
      offset (-33 to -35dB/s) was, a real finding worth noting: the broadband number's large
      High-dependence looks like it was substantially an artifact of the tilt filter shifting
      which frequency band dominates a single whole-signal measurement, not a genuine change in
      any one band's own decay character - further evidence the per-band model is the more
      physically correct one."""
    def band_droop_target(capture, band_key):
        override = _TARGET_DROOP_ROBUST_OVERRIDE.get((capture["params"]["time"], band_key))
        if override is not None and capture["params"]["high"] == 0:
            return override
        keys = _PER_BAND_GROUPS[band_key]
        bg = capture["band_gate"]
        vals = [bg[k]["plateau_droop_db_per_s"] for k in keys if k in bg and bg[k]["plateau_droop_db_per_s"] is not None]
        return sum(vals) / len(vals) if vals else None

    def band_fall_target(capture, band_key):
        keys = _PER_BAND_GROUPS[band_key]
        bg = capture["band_gate"]
        vals = [bg[k]["fall_rate_db_per_s"] for k in keys if k in bg and bg[k]["fall_rate_db_per_s"] is not None]
        return sum(vals) / len(vals) if vals else None

    by_time_high_count: dict[float, set] = {}
    for c in features["captures"]:
        by_time_high_count.setdefault(c["params"]["time"], set()).add(c["params"]["high"])
    richest_time = max(by_time_high_count, key=lambda t: len(by_time_high_count[t]))

    result = {}
    for band_key in ("low", "mid", "high"):
        print(f"\n--- per-band gate curves: {band_key} ({_PER_BAND_GROUPS[band_key]}) ---")

        # ---------------- droop (High=0 captures only, natural-corrected) ----------------
        droop_h0 = [
            (c["params"]["time"], band_droop_target(c, band_key))
            for c in features["captures"] if c["params"]["high"] == 0
        ]
        droop_written_pairs = []
        for t, target in sorted(droop_h0):
            natural = _NATURAL_DROOP_PER_BAND_CPP_MEASURED.get((t, band_key))
            if natural is None or target is None:
                print(f"  droop Time={t}: no natural-droop override or target - skipping")
                continue
            written = target - natural
            print(f"  droop Time={t}: target={target:.3f}  natural={natural:.3f}  -> written={written:.3f}")
            droop_written_pairs.append((t, written))

        droop_sweep = sorted(
            (c["params"]["high"], band_droop_target(c, band_key))
            for c in features["captures"] if c["params"]["time"] == richest_time
        )
        droop_offset_points = []
        if any(h == 0 for h, _ in droop_sweep) and len(droop_sweep) >= 2:
            baseline = next(v for h, v in droop_sweep if h == 0)
            droop_offset_points = [(h, v - baseline) for h, v in droop_sweep]
            print(f"  droop offsets (Time={richest_time}): {[round(v, 3) for _, v in droop_offset_points]}")

        if len(droop_written_pairs) >= 2:
            result[f"time_to_plateau_droop_{band_key}_db_per_s"] = {
                "points": fit_curve(droop_written_pairs).points(),
                "source": "direct_measurement_h0_only_natural_droop_corrected",
            }
        if len(droop_offset_points) >= 2:
            result[f"high_to_plateau_droop_{band_key}_db_per_s_offset"] = {
                "points": Curve1D([p[0] for p in droop_offset_points], [p[1] for p in droop_offset_points]).points(),
            }

        # ---------------- fall rate (all captures, analytical correction) ----------------
        fall_sweep = sorted(
            (c["params"]["high"], band_fall_target(c, band_key))
            for c in features["captures"] if c["params"]["time"] == richest_time
        )
        fall_offset_points = []
        fall_offset_curve = None
        if any(h == 0 for h, _ in fall_sweep) and len(fall_sweep) >= 2:
            baseline = next(v for h, v in fall_sweep if h == 0)
            fall_offset_points = [(h, v - baseline) for h, v in fall_sweep]
            fall_offset_curve = Curve1D([p[0] for p in fall_offset_points], [p[1] for p in fall_offset_points])
            print(f"  fall_rate offsets (Time={richest_time}): {[round(v, 3) for _, v in fall_offset_points]}")

        by_time: dict[float, list[float]] = {}
        for c in features["captures"]:
            t, h = c["params"]["time"], c["params"]["high"]
            fall_target = band_fall_target(c, band_key)
            if fall_target is None or fall_offset_curve is None:
                continue
            offset, _ = fall_offset_curve.evaluate(h)
            fall_target_h0 = fall_target - offset
            by_time.setdefault(t, []).append(fall_target_h0)
        fall_target_baseline = sorted((t, sum(vs) / len(vs)) for t, vs in by_time.items())

        droop_target_by_time = dict(droop_h0)  # H0-only droop targets, keyed by Time (real totals)
        droop_written_curve = (
            Curve1D([p[0] for p in droop_written_pairs], [p[1] for p in droop_written_pairs])
            if len(droop_written_pairs) >= 2 else None
        )
        fall_written_pairs = []
        for t, fall_target_h0 in fall_target_baseline:
            # Which formula applies depends on the WRITTEN droop's own sign, not the target's -
            # InhaltIRSynth.cpp's plateauDb(t) only keeps contributing past the knee (making the
            # target_fall - target_droop subtraction correct) when the value actually WRITTEN into
            # plateauDroopDbPerSec is negative; a positive written value is clamped at the knee
            # instead (see that file's own gateEnvelopeDb comment - a real bug found and fixed:
            # this engine's own per-band tank damping is sometimes so far from real hardware's own
            # that the correction needed is positive, and letting plateauDb(t) grow without bound
            # for the whole render was silently swamping fallRateDbPerSec's own decay). When
            # clamped, the post-knee slope is fallRateDbPerSec alone - target_fall_h0 directly, no
            # subtraction.
            droop_written_h0 = droop_written_curve.evaluate(t)[0] if droop_written_curve is not None else None
            if droop_written_h0 is not None and droop_written_h0 >= 0.0:
                written = fall_target_h0
                print(f"  fall_rate Time={t}: fall_target_h0={fall_target_h0:.3f}  "
                      f"droop_written_h0={droop_written_h0:.3f} (clamped/positive, no subtraction)  "
                      f"-> written={written:.3f}")
                fall_written_pairs.append((t, written))
                continue

            droop_target_h0 = droop_target_by_time.get(t)
            if droop_target_h0 is None:
                # Time=0.1/0.8 have no H=0 droop capture at all - extrapolate the droop curve
                # we're about to write, the same way InhaltParameterMap.cpp itself will at runtime.
                if droop_written_curve is not None:
                    # This extrapolates the WRITTEN (corrected) droop curve, not the target - close
                    # enough for this analytical relationship at Time settings this far outside the
                    # measured range, and consistent with how the real render will behave (it reads
                    # the written curve directly, not a separately-tracked target curve).
                    droop_target_h0, _ = droop_written_curve.evaluate(t)
                else:
                    continue
            written = fall_target_h0 - droop_target_h0
            print(f"  fall_rate Time={t}: fall_target_h0={fall_target_h0:.3f}  droop_target_h0={droop_target_h0:.3f}  -> written={written:.3f}")
            fall_written_pairs.append((t, written))

        if len(fall_written_pairs) >= 2:
            result[f"time_to_fall_rate_{band_key}_db_per_s"] = {
                "points": fit_curve(fall_written_pairs).points(),
                "source": "direct_measurement_h0_equivalent_additive_corrected",
            }
        if len(fall_offset_points) >= 2:
            result[f"high_to_fall_rate_{band_key}_db_per_s_offset"] = {
                "points": Curve1D([p[0] for p in fall_offset_points], [p[1] for p in fall_offset_points]).points(),
            }

    return result


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
    # plateau_droop_db_per_s, t_knee_ms, and fall_rate_db_per_s are deliberately NOT in this dict -
    # see _build_plateau_droop_curves', _build_t_knee_ms_curves', and _build_fall_rate_curves' own
    # docstrings for why pooling them the same naive way as tau_a_ms (which genuinely is close
    # enough to H-neutral for this to be an honest compromise) was a real bug, not a stylistic
    # choice, for each of the other three.
    points = {"tau_a_ms": []}
    for c in features["captures"]:
        t = c["params"]["time"]
        g = c["gate"]
        points["tau_a_ms"].append((t, g["build_up_ms"] * BUILD_UP_TO_TAU_A_FACTOR))

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
    curves.update(_build_t_knee_ms_curves(features))
    curves.update(_build_plateau_droop_curves(features, curves))
    curves.update(_build_fall_rate_curves(features, curves))
    curves.update(_build_early_excess_curves(features))
    curves.update(_build_per_band_gate_curves(features))

    curves["_notes"]["gate_timing_source_correction"] = (
        "time_to_tau_a_ms / time_to_t_knee_ms / time_to_fall_rate_db_per_s "
        "/ time_to_plateau_droop_db_per_s were REPLACED after this module's own build (see "
        "build_measured_gate_curves.py) - the fit's own values for these were compared per-capture "
        "against features.json's direct measurement and found systematically wrong (e.g. Time=0.1: "
        "measured knee_time_ms=125.1ms, fitted 366.2ms; fall_rate ranged -101 to -1614dB/s fitted vs "
        "a real -141 to -187dB/s measured range). Replaced with curves built directly from "
        "features.json instead - the exact 'fit numbers don't hold up, use direct measurement' "
        "pattern this catalog's AuraDecayGainData.h already established. tilt_low_gain, "
        "tilt_high_gain, and tilt_pivot_hz are ALSO replaced (see module docstring - the gains for "
        "the same TILT_REGULARIZATION_WEIGHT reason as before; the pivot after a real ear-caught "
        "complaint found the fit's ~4200-4700Hz value structurally caps the achievable HF cut well "
        "below the real captures' own measured spread, fixed by lowering it to 1500Hz and "
        "recalibrating gains against the whole-decay LTAS instead of the 20ms onset window). Only "
        "feedback_gain, damping_weight_mean, and diffuser_gain are UNCHANGED from the fit - see "
        "build_measured_gate_curves.py's own module docstring for why those three were kept. The "
        "two shortest Time settings (0.1s, 0.8s) exist only at High=-3 in the "
        "real capture set - tau_a_ms pools them straight into its Time-only curve alongside the "
        "High=0 points at longer Time, an honest compromise (documented, not hidden), since "
        "findings.md found the internal gate-shape breakdown is NOT as cleanly High-neutral as the "
        "overall gate_length_ms_at_20db is. t_knee_ms/fall_rate_db_per_s/plateau_droop_db_per_s "
        "instead correct those two Time settings via each parameter's own High-offset curve - see "
        "their own _notes entries."
    )
    curves["_notes"]["plateau_droop_db_per_s_h0_only"] = (
        "time_to_plateau_droop_db_per_s / high_to_plateau_droop_db_per_s_offset are built by "
        "_build_plateau_droop_curves, NOT pooled the way tau_a_ms is in this module's own main() "
        "loop - a real bug found and fixed after a listening complaint "
        "that turned out to be a gate-envelope issue, not the EQ/tank-topology issue it first "
        "looked like: pooling plateau_droop_db_per_s across ALL High values at a given Time (as an "
        "earlier version of this script did) corrupted the Time=9.8 High=0 baseline from its real "
        "-10.3dB/s to a blended -33.0dB/s (averaged in with High=-4/-9's own -43.6/-45.1dB/s), "
        "since findings.md's own 'High: NOT damping-neutral' finding means this parameter can't be "
        "pooled across High the way the other three (an honest, small compromise for those) can. "
        "Only High=0 captures form the baseline here - no coverage below Time=2.2s, an honest gap "
        "(Time=0.1/0.8 exist only at High=-3, and pooling them in would reintroduce this exact bug)."
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
    curves["_notes"]["fall_rate_db_per_s_h0_only_additive_corrected"] = (
        "time_to_fall_rate_db_per_s / high_to_fall_rate_db_per_s_offset are built by "
        "_build_fall_rate_curves, NOT pooled the way tau_a_ms is in this module's own main() loop "
        "- the same two bugs plateau_droop_db_per_s and t_knee_ms already had, never re-examined "
        "for this parameter until a real 'the delay tail still isn't quite a match' listening "
        "complaint prompted it. Bug 1 (naive High-pooling): Time=9.8's old curve value (-179.557dB/"
        "s) was exactly the mean of its three real captures at High=0/-4/-9; Time=7.0's (-177.69) "
        "was exactly the mean of its two. Bug 2 (additive-vs-total): fallRateDbPerSec is layered on "
        "top of plateauDb(t), which never turns off at the knee, so what's written must be "
        "target_fall_total minus this Time's own target_plateau_droop_total, not the raw target - "
        "see _build_fall_rate_curves' and _FALL_RATE_CPP_VERIFIED_OVERRIDE's own docstrings for the "
        "direct-measurement verification and the one known-unstable case (Time=2.2, kept at its "
        "analytical estimate rather than chased further)."
    )
    curves["_notes"]["early_excess_db_new_gate_term"] = (
        "time_to_early_excess_db / high_to_early_excess_db_offset are a NEW gate-shape term (see "
        "InhaltIRSynth.h's own earlyExcessDb comment), not a bug fix - added after a real 'let's "
        "go back to trying to get it to have the same decay and timing as the convolution' "
        "complaint. Root cause: real captures' post-knee fall is CURVED (core.features."
        "post_knee_excess_db measures this directly), not the single constant dB/s rate "
        "fall_rate_db_per_s alone can represent - 7 of 9 real captures fall measurably STEEPER "
        "right at the knee than their own long-run average, but the two captures with the "
        "shallowest knee (Time=7.0/9.8 at High=0) show the OPPOSITE. Built with the same "
        "H0-equivalent-baseline-plus-offset architecture as fall_rate_db_per_s/t_knee_ms and the "
        "same additive-vs-total correction (InhaltIRSynth's own kneeSoftnessMs transition already "
        "contributes some curvature on its own - see _EARLY_EXCESS_NATURAL_CPP_MEASURED's own "
        "comment for the direct C++ measurement this is corrected against). No isotonic "
        "regression - unlike t_knee_ms/fall_rate_db_per_s, this parameter has no physical reason "
        "to be monotonic in Time."
    )
    curves["_notes"]["per_band_gate_replaces_broadband"] = (
        "time_to_{plateau_droop,fall_rate}_{low,mid,high}_db_per_s / high_to_..._offset REPLACE "
        "the single broadband time_to_plateau_droop_db_per_s/time_to_fall_rate_db_per_s (kept in "
        "curves.json for reference/comparison, but InhaltParameterMap.cpp no longer reads them) - "
        "see _build_per_band_gate_curves' own docstring and InhaltIRSynth.h's own comment on "
        "Params::plateauDroopLowDbPerSec for the full story. Added after a real 'huffy, low-mid "
        "resonance... rings out longer than the IR's' complaint traced to a genuine architectural "
        "gap (one broadband decay rate structurally cannot represent real hardware's own dramatic "
        "per-band decay variation) confirmed NOT fixable by reshaping the tank's single "
        "dampingWeight (tested directly: fixing the under-decaying low-mids over-damps the highs "
        "by 3-6x, a structural ceiling of the one-pole-per-line filter). A real finding worth "
        "flagging: each band's own High offset is small (<2dB/s across the full High=-9..0 sweep, "
        "every band) - MUCH smaller than the broadband droop's own -33 to -35dB/s offset was, "
        "suggesting that large broadband number was substantially an artifact of the tilt filter "
        "shifting which band dominates a single whole-signal measurement, not a genuine per-band "
        "effect - further evidence the per-band model is the physically correct one."
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
