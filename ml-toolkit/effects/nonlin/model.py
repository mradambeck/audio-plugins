"""NonLinGatedFDN: composes core/dsp_primitives.py's building blocks into TWO independent,
mutually-disjoint feedback delay networks (one per channel - see the class docstring on why a
single shared tank, split the way AmbienceFDN's Aura port split one Hadamard tank across L/R,
would NOT reproduce the measured decorrelation), each shaped by an EXPLICIT, directly-measurable
dB-domain gate envelope applied after the tank render.

## Central design decision: what's directly measurable vs. what needs gradient fitting

Unlike AmbienceFDN (where Time/High's audible effect IS the tank's own feedback gain and damping),
this module treats the gate envelope shape (build-up, plateau, knee, fall) and the input tilt as
things ALREADY measured directly from every capture by core.features.gate_envelope_params() and
core.features.tilt_fit() - see effects/nonlin/findings.md. The tank's own feedback_gain and
damping_weight are only responsible for the tank's DENSITY/DIFFUSENESS and any genuine per-band
(frequency-dependent) decay differences; they are NOT responsible for reproducing the overall
audible amplitude shape over time - that's the explicit gate multiplier's job. This still gets
fit by gradient descent (rather than hardcoded from the direct measurement) so the whole system
is jointly consistent, but every fitted gate parameter has a directly-measured twin to check
against in build_curves.py, matching Aura's precedent of falling back to a directly-measured,
hand-authored table when a fit's own numbers don't hold up (see AuraDecayGainData.h).

## Feedback gain ceiling: 0.95, NOT Ambience's 0.985 - empirically derived, not inherited

This is the one place this module deliberately deviates from AmbienceFDN's own constants, and the
reasoning is worth keeping: a first attempt at this module inherited Ambience's _MAX_FEEDBACK_GAIN
= 0.985 on the assumption that the tank's own feedback gain needs to be near-unity to produce a
flat, non-drooping plateau (matching Phase 3a's original reasoning in the project plan). Direct
measurement disproved that assumption on two fronts:

  1. It's the wrong premise. Since the audible plateau shape comes from the EXPLICIT gate
     multiplier above (not from the tank's own natural decay), the tank does not need a near-
     unity gain to keep the plateau flat - the gate's own plateau_droop_db_per_s parameter (fit
     independently, directly measurable) already provides that flatness.
  2. It actively hurts frequency-sampling accuracy. render_fdn_impulse_response's docstring warns
     the method computes a circularly-ALIASED periodic extension, exact only once the true IR has
     decayed within the render window. A one-pole-damped feedback loop always has UNITY gain at
     DC (one_pole_transfer_function's LP(z=1) = weight/weight = 1 regardless of damping_weight),
     so a near-unity feedback_gain leaves a near-DC mode that decays at essentially the FLAT
     feedback_gain rate no matter how aggressively the rest of the spectrum is damped - measured
     directly: at gain=0.985 with this module's ~16ms mean delay lines and a realistic damping
     weight, doubling the render window from 1.2s to 2.4s still moved the render by 1.42% of peak
     (the aliasing hadn't converged); at gain<=0.95, the same doubling check moved output by under
     0.1% of peak - two full orders of magnitude better, at a render window (1.2-1.5s) this
     project can actually afford. See tests/test_effects_nonlin_model.py's
     test_render_is_converged_at_the_chosen_window for the permanent regression guard - if the
     gain ceiling or delay set is ever changed, that test is what catches a silent return to the
     under-converged regime.

Practically: 0.90-0.96 gain at ~16-17ms mean delay gives a tank RT60 of roughly 1-3.5s (see the
module's own rt60_estimate() helper) - short enough to converge cleanly in a ~1.5s render window,
and still comfortably audible/dense through the ~300-400ms window the gate ever leaves un-crushed.
"""
from __future__ import annotations

import math

import torch
import torch.nn.functional as F_torch

from core.dsp_primitives import (
    delay_transfer_function,
    hadamard_matrix,
    one_pole_transfer_function,
    rfft_omega,
    shelf_transfer_function,
)

NUM_LINES = 8

# Two independent, mutually disjoint delay sets (samples at 44.1kHz; scaled by the fit sample
# rate at construction, same convention as AmbienceFDN's _BASE_DELAY_SAMPLES_AT_44K). NOT a split
# of one shared Hadamard tank the way Aura's C++ port of AmbienceFDN divides 8 lines 4/4 across
# L/R (that still cross-feeds all 8 lines through one matrix, so the two halves share every mode -
# not decorrelation). The measured hardware (effects/nonlin/findings.md) shows genuinely
# independent channels: IACC over +-1ms of 0.006-0.037, not a fixed inter-channel delay masquer-
# ading as decorrelation (checked explicitly with iacc(), not just zero-lag correlation).
#
# Odd sample counts, no shared factor within each set (gcd=1) and no overlap between the two sets
# - the usual FDN convention against shared resonances (matches ShieldsFDNEngine/IntruderFDNEngine/
# AmbienceFDN's own non-whole-millisecond, geometrically-spaced delays). Mean ~16-17ms - much
# shorter than Ambience's 30-76ms, chosen for a short/tight gated program that needs to build
# diffuse density within ~100-200ms rather than Ambience's multi-second room-sized decay.
LEFT_DELAY_SAMPLES_AT_44K = torch.tensor(
    [471.0, 503.0, 589.0, 639.0, 735.0, 817.0, 941.0, 1067.0]
)
RIGHT_DELAY_SAMPLES_AT_44K = torch.tensor(
    [483.0, 549.0, 607.0, 699.0, 781.0, 867.0, 979.0, 1133.0]
)

# See module docstring's "Feedback gain ceiling" section - empirically derived, not inherited
# from AmbienceFDN's 0.985.
MAX_FEEDBACK_GAIN = 0.95
MAX_DAMPING_WEIGHT = 0.99

# Input tilt pivot bounds - unlike AmbienceFDN's fixed 1kHz pivot, this is LEARNABLE (bounded to
# this range) so the fit recovers the measured ~1-2kHz pivot from the data rather than being told
# it. Range chosen generously around that measurement (200Hz-6kHz) so the bound itself isn't the
# thing deciding the answer.
TILT_PIVOT_MIN_HZ = 200.0
TILT_PIVOT_MAX_HZ = 6000.0

# Gate envelope parameter bounds - every one of these has a directly-measured twin from
# core.features.gate_envelope_params(); see build_curves.py's cross-check against it.
#
# PLATEAU_DROOP's original range (-60..10 dB/s) was a placeholder guess made before any real
# capture existed. The real fit against effects/nonlin/findings.md's 9 captures pinned it exactly
# at -60.0 (the boundary) for several captures and diverged shortly after - findings.md's own
# direct measurement of the real captures found droop as steep as -215dB/s at short Time, so the
# original bound wasn't just tight, it was inside the range the data actually needs. Widened with
# real headroom, not re-guessed narrowly. TAU_A_MAX_MS similarly got pinned at exactly 60.0ms for
# several captures (real measured build_up_ms only ever reached ~35ms - see findings.md - but the
# fit wanted more, plausibly to compensate for the tank's own slow density buildup findings.md
# found via normalized_echo_density, not just the amplitude envelope); widened for the same reason.
TAU_A_MIN_MS, TAU_A_MAX_MS = 0.5, 150.0
PLATEAU_DROOP_MIN_DB_S, PLATEAU_DROOP_MAX_DB_S = -400.0, 10.0
T_KNEE_MIN_MS, T_KNEE_MAX_MS = 5.0, 400.0
FALL_RATE_MIN_DB_S, FALL_RATE_MAX_DB_S = -2000.0, -20.0
TAU_K_MIN_MS, TAU_K_MAX_MS = 0.2, 30.0


def rt60_estimate_s(feedback_gain: float, mean_delay_ms: float) -> float:
    """-60dB decay time implied by a flat feedback_gain and a mean round-trip delay, ignoring
    damping (an upper bound on how long the tank actually rings, since real damping only shortens
    it further) - used to sanity-check the render window choice, not part of the model itself."""
    return math.log(0.001) / math.log(feedback_gain) * mean_delay_ms / 1000.0


def _bounded(raw: torch.Tensor, max_value: float) -> torch.Tensor:
    """[0, max_value], matching AmbienceFDN's own _bounded() exactly (including its 1e-6 floor -
    same DC 0/0 NaN risk in one_pole_transfer_function applies here)."""
    return max_value * torch.sigmoid(raw).clamp(min=1e-6)


def _bounded_range(raw: torch.Tensor, lo: float, hi: float) -> torch.Tensor:
    """[lo, hi] for two-sided or negative ranges (tilt pivot, gate shape parameters) that
    _bounded()'s [0, max] form can't express."""
    return lo + (hi - lo) * torch.sigmoid(raw)


class NonLinGatedFDN(torch.nn.Module):
    """Two independent 8-line FDNs (channel folded into the render, not the batch - see forward())
    with a SHARED feedback_gain/damping_weight/tilt/gate envelope across both channels (one
    hardware program, one set of knobs; only the two delay sets differ) - promote any of these to
    per-channel only if core.features.stereo_gate_alignment() shows real per-channel drift in the
    captures (see effects/nonlin/findings.md)."""

    def __init__(self, batch: int, num_samples: int, sample_rate: float):
        super().__init__()
        self.num_samples = num_samples
        self.sample_rate = sample_rate
        self.left_delay_samples = LEFT_DELAY_SAMPLES_AT_44K * (sample_rate / 44100.0)
        self.right_delay_samples = RIGHT_DELAY_SAMPLES_AT_44K * (sample_rate / 44100.0)
        self.mixing_matrix = hadamard_matrix(NUM_LINES)

        # Tank (in feedback loop - bounded/sigmoid-reparameterized, per AmbienceFDN's own hard-
        # won lesson: an unbounded gain the optimizer pushes past 1.0 stops decaying entirely).
        # Initialized around raw=1.5 (sigmoid(1.5)=0.82, so effective gain ~0.82*0.95=0.78) rather
        # than AmbienceFDN's zero-init (sigmoid(0)=0.5, effective ~0.475) - unlike Ambience, this
        # tank's gain doesn't need to climb from a low starting point to do its job (the explicit
        # gate multiplier handles audible shaping), so there's no risk of a flat-plateau-shaped
        # loss surface stranding the optimizer at a too-low gain the way an unshaped decay-rate
        # loss could.
        self.feedback_gain_raw = torch.nn.Parameter(torch.full((batch, 1), 1.5))
        self.damping_weight_raw = torch.nn.Parameter(torch.zeros(batch, NUM_LINES))

        # Input-stage tilt (NOT in the feedback loop - findings.md establishes H doesn't move
        # envelope timing, which is direct evidence the tilt sits outside the loop; an in-loop
        # placement is the exact bug AuraFDNEngine.h documents hitting: it produced literally zero
        # onset-tilt difference because dry input's first pass is unshelved). Independent low/high
        # gains (unconstrained - not a stability concern outside the loop) because the measured
        # +4.2dB/-9.1dB asymmetry is unrepresentable by a symmetric tilt; common/dsp/TiltFilter.h
        # can't express this either, hence promoting BandShelf out of AuraFDNEngine.h for the C++
        # side (see plugins/common/dsp/BandShelf.h).
        self.tilt_low_gain = torch.nn.Parameter(torch.ones(batch, 1))
        self.tilt_high_gain = torch.nn.Parameter(torch.ones(batch, 1))
        self.tilt_pivot_raw = torch.nn.Parameter(torch.zeros(batch, 1))  # sigmoid(0)=0.5 -> mid-range pivot

        # Gate envelope (explicit, dB-domain, applied in forward() after the tank render) - see
        # module docstring. Initialized to plausible mid-range values; fit_nonlin.py's onset
        # alignment and per-capture initialization from the DIRECT measurement (not blind zeros)
        # is what actually matters for convergence speed, done at construction time by the caller.
        self.tau_a_raw = torch.nn.Parameter(torch.zeros(batch, 1))
        self.plateau_droop_raw = torch.nn.Parameter(torch.zeros(batch, 1))
        self.t_knee_raw = torch.nn.Parameter(torch.zeros(batch, 1))
        self.fall_rate_raw = torch.nn.Parameter(torch.zeros(batch, 1))
        self.tau_k_raw = torch.nn.Parameter(torch.zeros(batch, 1))

        # Free per-capture output level (not in the loop) - same role as AmbienceFDN's
        # output_gain: without it, the in-loop/gate parameters would be forced to also chase each
        # capture's arbitrary absolute recording level.
        self.output_gain = torch.nn.Parameter(torch.ones(batch, 1))

    # -- bounded accessors -------------------------------------------------------------------
    def effective_feedback_gain(self) -> torch.Tensor:
        return _bounded(self.feedback_gain_raw, MAX_FEEDBACK_GAIN)

    def effective_damping_weight(self) -> torch.Tensor:
        return _bounded(self.damping_weight_raw, MAX_DAMPING_WEIGHT)

    def tilt_pivot_hz(self) -> torch.Tensor:
        return _bounded_range(self.tilt_pivot_raw, TILT_PIVOT_MIN_HZ, TILT_PIVOT_MAX_HZ)

    def tau_a_s(self) -> torch.Tensor:
        return _bounded_range(self.tau_a_raw, TAU_A_MIN_MS, TAU_A_MAX_MS) / 1000.0

    def plateau_droop_db_per_s(self) -> torch.Tensor:
        return _bounded_range(self.plateau_droop_raw, PLATEAU_DROOP_MIN_DB_S, PLATEAU_DROOP_MAX_DB_S)

    def t_knee_s(self) -> torch.Tensor:
        return _bounded_range(self.t_knee_raw, T_KNEE_MIN_MS, T_KNEE_MAX_MS) / 1000.0

    def fall_rate_db_per_s(self) -> torch.Tensor:
        return _bounded_range(self.fall_rate_raw, FALL_RATE_MIN_DB_S, FALL_RATE_MAX_DB_S)

    def tau_k_s(self) -> torch.Tensor:
        return _bounded_range(self.tau_k_raw, TAU_K_MIN_MS, TAU_K_MAX_MS) / 1000.0

    # -- rendering -----------------------------------------------------------------------------
    def _pivot_weight(self) -> torch.Tensor:
        pivot_hz = self.tilt_pivot_hz().squeeze(-1)  # [B]
        return 1.0 - torch.exp(-2 * math.pi * pivot_hz / self.sample_rate)

    def _render_tank(self, delay_samples: torch.Tensor) -> torch.Tensor:
        """One channel's raw tank output spectrum (pre-tilt, pre-gate), via the same closed-form
        solve core.dsp_primitives.render_fdn_impulse_response uses, inlined (not called directly)
        so the input-stage tilt below can be applied to the complex spectrum before irfft rather
        than as a second irfft/rfft round trip - matches AmbienceFDN's own reason for inlining
        rather than composing the all-in-one helper."""
        device = self.feedback_gain_raw.device
        batch = self.feedback_gain_raw.shape[0]

        omega = rfft_omega(self.num_samples, device=device)
        Delta = delay_transfer_function(delay_samples.to(device), omega)  # [n_freq, L]
        Delta_b = Delta[None, :, :].expand(batch, -1, -1)

        damping_weight = self.effective_damping_weight()
        Damp = one_pole_transfer_function(damping_weight, omega).transpose(-1, -2)  # [B, n_freq, L]

        feedback_gain = self.effective_feedback_gain().reshape(batch, 1, 1, 1)
        D_diag = torch.diag_embed(Delta_b)
        Damp_diag = torch.diag_embed(Damp)
        Hmix = self.mixing_matrix.to(device=device, dtype=torch.complex64)

        M = feedback_gain.to(torch.complex64) * (D_diag @ Hmix @ Damp_diag)
        eye = torch.eye(NUM_LINES, device=device, dtype=torch.complex64)
        A = eye - M

        ones_vec = torch.ones(batch, NUM_LINES, device=device, dtype=torch.complex64)
        rhs = (Delta_b * ones_vec[:, None, :]).unsqueeze(-1)

        Y = torch.linalg.solve(A, rhs)
        DampY = Damp_diag @ Y
        return (Hmix @ DampY).sum(dim=-2).squeeze(-1)  # [B, n_freq] complex

    def _gate_envelope_db(self) -> torch.Tensor:
        """[B, num_samples], t=0 at the render's own impulse feed (approximately the same
        reference point as a real capture's onset_idx once fit_nonlin.py onset-aligns the
        target - see that module's docstring on why onset alignment is mandatory here, unlike
        AmbienceFDN's plain exponential decay)."""
        device = self.feedback_gain_raw.device
        t = torch.arange(self.num_samples, device=device, dtype=torch.float32) / self.sample_rate
        t = t[None, :]  # [1, N], broadcasts against [B, 1] parameters

        tau_a = self.tau_a_s()
        attack_db = 20.0 * torch.log10(torch.clamp(1.0 - torch.exp(-t / tau_a), min=1e-6))
        plateau_db = self.plateau_droop_db_per_s() * t
        knee_db = self.fall_rate_db_per_s() * self.tau_k_s() * F_torch.softplus(
            (t - self.t_knee_s()) / self.tau_k_s()
        )
        return attack_db + plateau_db + knee_db

    def forward(self) -> torch.Tensor:
        """Returns [B, 2, num_samples] (channel axis explicit, NOT folded into batch - the caller
        compares each channel against its own target directly)."""
        pivot_weight = self._pivot_weight()
        tilt = shelf_transfer_function(
            self.tilt_low_gain.squeeze(-1), self.tilt_high_gain.squeeze(-1), pivot_weight,
            rfft_omega(self.num_samples, device=self.feedback_gain_raw.device),
        ).to(torch.complex64)

        left_spectrum = self._render_tank(self.left_delay_samples) * tilt
        right_spectrum = self._render_tank(self.right_delay_samples) * tilt

        left = torch.fft.irfft(left_spectrum, n=self.num_samples, dim=-1)
        right = torch.fft.irfft(right_spectrum, n=self.num_samples, dim=-1)

        gate_lin = 10.0 ** (self._gate_envelope_db() / 20.0)  # [B, N]
        left = left * gate_lin * self.output_gain
        right = right * gate_lin * self.output_gain

        return torch.stack([left, right], dim=1)
