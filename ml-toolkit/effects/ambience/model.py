"""Phase B4: AmbienceFDN - composes core/dsp_primitives.py's building blocks into a fixed-
topology FDN for the AMS RMX16 "Ambience" program, shaped by findings.md's Phase B3 conclusions:

  - Time drives decay length via the overall feedback gain (Time is close to literal seconds -
    see findings.md - so no special nonlinear remapping is needed at the model level; that
    remapping, if any, belongs in the knob-to-parameter curve fit in build_curves.py, not here).
  - High drives a broadband output tilt (bass up/treble down as it decreases, present at onset)
    AND measurably shortens decay - modeled as an output-stage shelf (reusing
    core.dsp_primitives.shelf_transfer_function) plus its own contribution to the fitted per-line
    damping, exactly like Intruder's H (findings.md explicitly draws this parallel).
  - Low has NO onset-tone effect and no measurable decay effect when High=0, but does lengthen
    decay when High is very negative (more low-frequency energy already circulating) - findings.md
    reads this as a low-band-specific feedback multiplier, not a second independent shelf.
    Modeled here as a shelf applied INSIDE the feedback loop (low and high bands each get their
    own feedback gain) - the same shelf_transfer_function primitive, used differently than High's
    output-stage shelf.

Per-capture learnable parameters (fit independently per capture in fit_ambience.py): a low-band
and a high-band in-loop feedback gain (Time and Low's joint effect), per-line HF damping (High's
contribution to decay), and an output-stage tilt (High's onset/tail tone). Delay lengths and the
Hadamard mixing matrix are fixed, non-learnable topology (per the plan's fixed-topology design
decision) - see core/dsp_primitives.py's module docstring for why fitting is done via frequency-
sampling rather than a time-domain simulation.

All in-loop feedback-path quantities (the two band gains, the per-line damping weight) are bounded
via a sigmoid reparameterization rather than left as raw unconstrained parameters. This is not
optional: an early version left them unconstrained, and a low-band gain the optimizer pushed past
1.0 stopped decaying entirely (full_decay_rt60 correctly returned None - the signal had genuinely
stopped decaying, not a measurement bug) - the same class of instability
ShieldsFDNEngine.cpp documents hitting and fixing with a hard 0.985 gain ceiling. The bound here
plays the same role, just expressed as a differentiable reparameterization Adam can optimize
through instead of a runtime clamp.
"""
from __future__ import annotations

import math

import torch

from core.dsp_primitives import (
    delay_transfer_function,
    hadamard_matrix,
    one_pole_transfer_function,
    rfft_omega,
    shelf_transfer_function,
)

NUM_LINES = 8

# Fixed delay-line lengths (samples at 44.1kHz; scaled by the actual fit sample rate at
# construction time so the real-ms spacing stays put when fitting at a reduced rate - see
# core/dsp_primitives.py's frequency-sampling docstring on why a reduced fit rate is used).
# Same geometrically-spaced convention ShieldsFDNEngine/IntruderFDNEngine use, not re-derived
# from scratch - Ambience doesn't need a topology different from the catalog's existing FDNs.
_BASE_DELAY_SAMPLES_AT_44K = torch.tensor(
    [1327.0, 1559.0, 1811.0, 2099.0, 2437.0, 2683.0, 2999.0, 3343.0]
)

# Fixed pivot for both shelves (~1kHz) - findings.md's onset 4-band breakdown showed High's tilt
# pivoting somewhere in the low-mid, consistent with Intruder's ~1-4kHz TiltFilter pivot; not
# fit per-capture (a pivot frequency isn't what either knob's measured effect varies).
_SHELF_PIVOT_HZ = 1000.0

# Matches ShieldsFDNEngine.cpp's own documented feedback-gain ceiling (comment there: "100%
# damping was found to be a kill switch" - same reasoning applies to feedback gain exceeding
# unity, which stops the signal decaying at all rather than just decaying slowly).
_MAX_FEEDBACK_GAIN = 0.985
_MAX_DAMPING_WEIGHT = 0.99


def _bounded(raw: torch.Tensor, max_value: float) -> torch.Tensor:
    # Clamp away from an exact 0.0 (float32 sigmoid saturates to exactly 0 for very negative raw
    # values, e.g. raw <= ~-90) - a damping/gain weight of exactly 0.0 makes
    # one_pole_transfer_function evaluate a literal 0/0 at the DC bin (weight/weight, both
    # exactly zero), producing NaN. A tiny floor keeps that ratio well-defined without measurably
    # changing behavior anywhere Adam would actually want to be.
    return max_value * torch.sigmoid(raw).clamp(min=1e-6)


class AmbienceFDN(torch.nn.Module):
    def __init__(self, batch: int, num_samples: int, sample_rate: float):
        super().__init__()
        self.num_samples = num_samples
        self.sample_rate = sample_rate
        self.delay_samples = _BASE_DELAY_SAMPLES_AT_44K * (sample_rate / 44100.0)
        self.mixing_matrix = hadamard_matrix(NUM_LINES)

        # Raw (unconstrained) learnable parameters - see effective_*() for the bounded values
        # actually used in forward(). Initialized at 0 (sigmoid(0) = 0.5, a moderate, safe
        # starting point for every in-loop gain/weight; Adam moves away from it during fitting).
        self.high_band_gain_raw = torch.nn.Parameter(torch.zeros(batch, 1))
        self.low_band_gain_raw = torch.nn.Parameter(torch.zeros(batch, 1))
        self.damping_weight_raw = torch.nn.Parameter(torch.zeros(batch, NUM_LINES))
        # Output-stage tilt gains are not in the feedback loop (they scale the final output
        # once, not recirculated), so they're not a stability concern and stay unconstrained.
        self.tilt_low_gain = torch.nn.Parameter(torch.ones(batch, 1))
        self.tilt_high_gain = torch.nn.Parameter(torch.ones(batch, 1))
        # Free overall output level. The model always renders from a unit impulse, so without
        # this the in-loop gains/damping would be forced to also chase each target capture's
        # arbitrary absolute recording level, contaminating what's meant to be a pure decay-
        # rate/tone fit. Not in the feedback loop, so no stability concern.
        self.output_gain = torch.nn.Parameter(torch.ones(batch, 1))

    def effective_high_band_gain(self) -> torch.Tensor:
        return _bounded(self.high_band_gain_raw, _MAX_FEEDBACK_GAIN)

    def effective_low_band_gain(self) -> torch.Tensor:
        return _bounded(self.low_band_gain_raw, _MAX_FEEDBACK_GAIN)

    def effective_damping_weight(self) -> torch.Tensor:
        return _bounded(self.damping_weight_raw, _MAX_DAMPING_WEIGHT)

    def forward(self) -> torch.Tensor:
        device = self.high_band_gain_raw.device
        batch = self.high_band_gain_raw.shape[0]

        omega = rfft_omega(self.num_samples, device=device)  # [n_freq], float64
        pivot_weight = self._pivot_weight()

        Delta = delay_transfer_function(self.delay_samples.to(device), omega)  # [n_freq, L]
        Delta_b = Delta[None, :, :].expand(batch, -1, -1)  # [B, n_freq, L]

        damping_weight = self.effective_damping_weight()
        Damp = one_pole_transfer_function(damping_weight, omega).transpose(-1, -2)  # [B, n_freq, L]

        # In-loop low/high-band feedback shelf (Time+Low's joint effect, per findings.md): each
        # band has its own bounded feedback gain. Same frequency response applied identically to
        # every line (not a per-line diagonal), so it factors as a single scalar-per-frequency
        # multiplier, exactly like the plain scalar feedback_gain in
        # core.dsp_primitives.render_fdn_impulse_response - just frequency-dependent here instead
        # of frequency-flat.
        low_gain = self.effective_low_band_gain().squeeze(-1)  # [B]
        high_gain = self.effective_high_band_gain().squeeze(-1)  # [B]
        feedback_response = shelf_transfer_function(low_gain, high_gain, pivot_weight, omega)  # [B, n_freq]

        D_diag = torch.diag_embed(Delta_b)  # [B, n_freq, L, L]
        Damp_diag = torch.diag_embed(Damp)  # [B, n_freq, L, L]
        H = self.mixing_matrix.to(device=device, dtype=torch.complex64)

        M = feedback_response.to(torch.complex64).reshape(batch, -1, 1, 1) * (D_diag @ H @ Damp_diag)
        eye = torch.eye(NUM_LINES, device=device, dtype=torch.complex64)
        A = eye - M

        ones_vec = torch.ones(batch, NUM_LINES, device=device, dtype=torch.complex64)
        rhs = (Delta_b * ones_vec[:, None, :]).unsqueeze(-1)  # [B, n_freq, L, 1]

        Y = torch.linalg.solve(A, rhs)
        DampY = Damp_diag @ Y
        tank_out = (H @ DampY).sum(dim=-2).squeeze(-1)  # [B, n_freq] complex

        # Output-stage broadband tilt (High's effect, per findings.md): shapes the aggregate
        # output spectrum (all recirculation depths, since tank_out already reflects the full
        # closed-form feedback solution) - equivalent to a tilt filter at the reverb's final
        # output tap, so it affects both the initial onset and the decaying tail's tone alike.
        output_tilt = shelf_transfer_function(
            self.tilt_low_gain.squeeze(-1), self.tilt_high_gain.squeeze(-1), pivot_weight, omega
        ).to(torch.complex64)
        out_spectrum = tank_out * output_tilt

        rendered = torch.fft.irfft(out_spectrum, n=self.num_samples, dim=-1)
        return rendered * self.output_gain

    def _pivot_weight(self) -> torch.Tensor:
        return torch.tensor(
            1.0 - math.exp(-2 * math.pi * _SHELF_PIVOT_HZ / self.sample_rate)
        )
