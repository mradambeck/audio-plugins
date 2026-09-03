"""Differentiable DSP building blocks, scoped to exactly what Ambience needs: a fixed-topology
feedback delay network (delay lines + Hadamard feedback matrix + one-pole damping/shelf tone
shaping). No fractional/interpolated delay, waveshaper, envelope follower, or LFO yet - those stay
known gaps until a second, non-reverb effect actually needs them.

## Why frequency-domain rendering, not a per-sample time-domain simulation (Plan Phase A0)

A naive PyTorch loop that simulates the FDN sample-by-sample (read delay lines, mix through the
feedback matrix, damp, write back) is the obvious first implementation - and a real trap: timed at
~9.8s for just 5000 samples (0.11s of audio) at batch=65, which extrapolates to *hours* per Adam
step at a real capture's length. The Python-level loop overhead, paid once per sample per step,
dominates; neither more batching nor an MPS device fixes it (MPS measured slightly *slower*, since
the bottleneck is per-step dispatch, not compute).

The fix: within a single fit, feedback_gain/damping/shelf gains are CONSTANT over time (not
automated mid-capture), so the whole FDN is a fixed linear time-invariant system for that forward
pass. Its impulse response can be computed via the frequency-sampling method instead of sequential
simulation - evaluate the closed-form transfer function

    Y(z) = (I - g * Delta(z) @ H @ Damp(z))^-1 @ Delta(z) @ ones

at z_k = exp(j*2*pi*k/N) for every rfft bin k (a batched complex 8x8 `torch.linalg.solve`, no
Python-level per-sample loop at all), then `torch.fft.irfft` back to the time domain. Verified
numerically against the slow time-domain reference (on short test-only delay lengths, so the
reference sim finishes in a reasonable time - see the frequency-sampling caveat below).

Measured cost at full 44.1kHz resolution with all 65 captures batched into one solve: memory
pressure (each intermediate tensor is ~1.8GB) causes swapping/thrashing rather than a clean
linear slowdown - impractical as-is. At a 4x-reduced internal fit rate (11025Hz) it's ~5s/step for
all 65 captures at once (~2000 steps in well under 3 hours), or ~0.3s/step at a batch of 4 -
practical, especially since a per-capture fit only has a handful of learnable scalars and likely
doesn't need thousands of steps to converge. Recommendation: fit at a reduced internal sample
rate (see `real_rate_damping_weight` below for converting a fitted damping weight back to 44.1kHz
before export) and/or in modest-sized capture batches (8-16) rather than all 65 at once, to keep
peak memory bounded. Revisit only if evidence (not assumption) shows it's still too slow.

**Frequency-sampling caveat**: this computes the periodic (circularly-aliased) extension of the
true impulse response, sampled at N equally-spaced frequency points. It's exact for a
sufficiently-decayed capture (the standard, accepted assumption in filter/reverb frequency-
sampling design) - choose the fit window long enough that the target capture (and the model's own
render) has genuinely decayed within it, not just "looks about the right length."
"""
from __future__ import annotations

import math

import torch


def hadamard_matrix(n: int) -> torch.Tensor:
    """Fixed (non-learnable) Hadamard feedback matrix, Sylvester construction, normalized by
    1/sqrt(n) - matches ShieldsFDNEngine's/IntruderFDNEngine's own fixed matrix. n must be a
    power of 2."""
    if n & (n - 1) != 0:
        raise ValueError(f"hadamard_matrix(n) requires a power of 2, got {n}")
    h = torch.tensor([[1.0]])
    while h.shape[0] < n:
        h = torch.cat([torch.cat([h, h], dim=1), torch.cat([h, -h], dim=1)], dim=0)
    return h / (n ** 0.5)


def rfft_omega(num_samples: int, dtype: torch.dtype = torch.float64, device=None) -> torch.Tensor:
    """Angular frequency (radians/sample) at each rfft bin: omega_k = 2*pi*k/N for
    k = 0..N//2. float64 by default for phase-accumulation precision at high k*delay products
    (MPS doesn't support float64 - cast down to float32 explicitly if running fitting on MPS,
    trading some high-frequency phase precision for device support)."""
    n_freq = num_samples // 2 + 1
    k = torch.arange(n_freq, dtype=dtype, device=device)
    return 2 * math.pi * k / num_samples


def delay_transfer_function(delay_samples: torch.Tensor, omega: torch.Tensor) -> torch.Tensor:
    """Delta(z) = z^-D for each line's fixed delay D, evaluated at each frequency in omega.
    delay_samples: [..., num_lines] (real, fixed topology - not learnable). omega: [n_freq].
    Returns complex [..., n_freq, num_lines]."""
    phase = omega[..., :, None].to(torch.complex64) * delay_samples[..., None, :].to(torch.complex64)
    return torch.exp(-1j * phase)


def one_pole_transfer_function(weight: torch.Tensor, omega: torch.Tensor) -> torch.Tensor:
    """Frequency response of the one-pole leaky-integrator lowpass (mirrors
    common/dsp/OnePoleFilter.h's processSample() exactly: state += weight*(x-state)):
    LP(z) = weight / (1 - (1-weight) * z^-1). weight: [...] (broadcastable, e.g. [batch, lines]).
    omega: [n_freq]. Returns complex [..., n_freq] (weight's shape with an n_freq axis inserted
    before its last dim, matching how damping/shelf weights are usually shaped [batch, lines])."""
    z_inv = torch.exp(-1j * omega).to(torch.complex64)  # [n_freq]
    w = weight.to(torch.complex64)
    numerator = w[..., None]  # [..., 1] broadcasts over n_freq
    denominator = 1.0 - (1.0 - w[..., None]) * z_inv  # [..., n_freq]
    return numerator / denominator


def shelf_transfer_function(low_gain: torch.Tensor, high_gain: torch.Tensor, pivot_weight: torch.Tensor, omega: torch.Tensor) -> torch.Tensor:
    """Frequency response of a low/high shelf built the same way as common/dsp/TiltFilter.h
    (split via a one-pole lowpass at the pivot, recombine with independent low/high gains):
    Shelf(z) = low_gain*LP(z) + high_gain*(1-LP(z)). pivot_weight is the one-pole weight at the
    shelf's pivot frequency (see real_rate_damping_weight/cutoff_hz_from_weight to convert to/
    from Hz). low_gain/high_gain/pivot_weight: [...] broadcastable. Returns complex [..., n_freq].
    """
    lp = one_pole_transfer_function(pivot_weight, omega)  # [..., n_freq]
    return low_gain[..., None] * lp + high_gain[..., None] * (1.0 - lp)


def render_fdn_impulse_response(
    delay_samples: torch.Tensor,
    feedback_gain: torch.Tensor,
    damping_weight: torch.Tensor,
    mixing_matrix: torch.Tensor,
    num_samples: int,
    input_gain: torch.Tensor | None = None,
) -> torch.Tensor:
    """Renders a fixed-topology FDN's impulse response via the frequency-sampling method (see
    module docstring) - no per-sample Python loop.

    Topology (delay_samples, mixing_matrix) is fixed/non-learnable; feedback_gain and
    damping_weight are the per-capture learnable parameters this is meant to be fit against.

    delay_samples: [num_lines] (fixed, real-valued sample counts - shared across the batch).
    feedback_gain: [batch, 1] or [batch] (scalar per capture).
    damping_weight: [batch, num_lines] (one-pole weight per line per capture - see
        common/dsp/OnePoleFilter.h's setWeight()/setCutoffHz() for the real-rate meaning).
    mixing_matrix: [num_lines, num_lines] (fixed, e.g. hadamard_matrix(num_lines)).
    input_gain: optional [batch, num_lines] or [num_lines] gain applied to the impulse fed into
        each line (default: 1.0 into every line, as in ShieldsFDNEngine/IntruderFDNEngine).

    Returns: real [batch, num_samples] impulse response.
    """
    device = feedback_gain.device
    batch = feedback_gain.shape[0]
    num_lines = delay_samples.shape[-1]
    feedback_gain = feedback_gain.reshape(batch, 1, 1, 1)

    omega = rfft_omega(num_samples, device=device)  # [n_freq], float64
    n_freq = omega.shape[0]

    Delta = delay_transfer_function(delay_samples.to(device), omega)  # [n_freq, num_lines]
    Delta_b = Delta[None, :, :].expand(batch, -1, -1)  # [batch, n_freq, num_lines]
    Damp = one_pole_transfer_function(damping_weight, omega)  # [batch, n_freq_from_weight?, ...]
    # one_pole_transfer_function broadcasts weight's shape [batch, num_lines] with omega [n_freq]
    # into [batch, num_lines, n_freq] - transpose the last two dims to match Delta_b's
    # [batch, n_freq, num_lines] layout used throughout the rest of this function.
    Damp = Damp.transpose(-1, -2)  # [batch, n_freq, num_lines]

    D_diag = torch.diag_embed(Delta_b)  # [batch, n_freq, L, L]
    Damp_diag = torch.diag_embed(Damp)  # [batch, n_freq, L, L]
    H = mixing_matrix.to(device=device, dtype=torch.complex64)  # [L, L]

    M = feedback_gain.to(torch.complex64) * (D_diag @ H @ Damp_diag)  # [batch, n_freq, L, L]
    eye = torch.eye(num_lines, device=device, dtype=torch.complex64)
    A = eye - M

    if input_gain is None:
        impulse_vec = torch.ones(batch, num_lines, device=device, dtype=torch.complex64)
    else:
        impulse_vec = input_gain.to(device=device, dtype=torch.complex64)
        if impulse_vec.ndim == 1:
            impulse_vec = impulse_vec[None, :].expand(batch, -1)
    # D(z) @ impulse == elementwise Delta * impulse (D(z) is diagonal).
    rhs = (Delta_b * impulse_vec[:, None, :]).unsqueeze(-1)  # [batch, n_freq, L, 1]

    Y = torch.linalg.solve(A, rhs)  # [batch, n_freq, L, 1]
    DampY = Damp_diag @ Y  # [batch, n_freq, L, 1]
    Out = (H @ DampY).sum(dim=-2).squeeze(-1)  # [batch, n_freq] complex

    return torch.fft.irfft(Out, n=num_samples, dim=-1)


def cutoff_hz_from_weight(weight: torch.Tensor, sample_rate: float) -> torch.Tensor:
    """Inverse of common/dsp/OnePoleFilter.h's setCutoffHz(): recovers the -3dB cutoff frequency
    implied by a fitted one-pole weight at the given sample rate. Used to carry a weight fit at a
    reduced internal sample rate over to the real 44.1kHz runtime rate (see
    real_rate_damping_weight) - feedback_gain and shelf gains are sample-rate-independent scalars
    and transfer as-is; only a one-pole weight needs this conversion."""
    return -torch.log(torch.clamp(1.0 - weight, min=1e-8)) * sample_rate / (2 * math.pi)


def real_rate_damping_weight(weight_at_fit_rate: torch.Tensor, fit_sample_rate: float, real_sample_rate: float) -> torch.Tensor:
    """Converts a one-pole weight fit at a reduced internal sample rate to the equivalent weight
    at the real runtime sample rate, via the implied cutoff frequency (which is what's actually
    physically meaningful, not the raw weight - the same weight value means a different cutoff at
    a different sample rate)."""
    cutoff_hz = cutoff_hz_from_weight(weight_at_fit_rate, fit_sample_rate)
    return 1.0 - torch.exp(-2 * math.pi * cutoff_hz / real_sample_rate)
