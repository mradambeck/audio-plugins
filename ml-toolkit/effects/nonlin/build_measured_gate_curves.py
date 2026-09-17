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
    # plateau_droop_db_per_s and t_knee_ms are deliberately NOT in this dict - see
    # _build_plateau_droop_curves' and _build_t_knee_ms_curves' own docstrings for why pooling
    # them the same naive way as tau_a_ms/fall_rate_db_per_s (which genuinely are close enough to
    # H-neutral for this to be an honest compromise) was a real bug, not a stylistic choice.
    points = {"tau_a_ms": [], "fall_rate_db_per_s": []}
    for c in features["captures"]:
        t = c["params"]["time"]
        g = c["gate"]
        points["tau_a_ms"].append((t, g["build_up_ms"] * BUILD_UP_TO_TAU_A_FACTOR))
        points["fall_rate_db_per_s"].append((t, g["fall_rate_db_per_s"]))

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
        "real capture set and are pooled into these Time-only curves alongside the High=0 points at "
        "longer Time - an honest compromise (documented, not hidden), since findings.md found the "
        "internal gate-shape breakdown is NOT as cleanly High-neutral as the overall "
        "gate_length_ms_at_20db is."
    )
    curves["_notes"]["plateau_droop_db_per_s_h0_only"] = (
        "time_to_plateau_droop_db_per_s / high_to_plateau_droop_db_per_s_offset are built by "
        "_build_plateau_droop_curves, NOT pooled the way tau_a_ms/t_knee_ms/fall_rate_db_per_s are "
        "in this module's own main() loop - a real bug found and fixed after a listening complaint "
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
