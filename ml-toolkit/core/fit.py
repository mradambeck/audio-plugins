"""Effect-agnostic Adam fitting harness: composed model + loss + target capture -> fitted params.

`core/features.py`'s functions (numpy, `scipy.polyfit`-based) are NOT autograd-differentiable and
are not used as the fitting loss directly - they stay the human-facing analysis/validation
language (used identically for Phase B hand-review and Phase D empirical validation, so a fitted
result is judged by the same yardstick used to characterize the real captures). The loss here is a
separate, simpler, torch-native surrogate (multi-resolution STFT magnitude + a short-time energy
envelope term) that correlates with, but is not literally, those analysis metrics.
"""
from __future__ import annotations

import random
from dataclasses import dataclass, field

import torch
import torch.nn.functional as F


def _safe_complex_abs(z: torch.Tensor, eps: float = 1e-8) -> torch.Tensor:
    """Numerically-safe replacement for a complex tensor's own .abs(). Plain .abs() computes
    sqrt(real^2+imag^2) but its GRADIENT is z/|z| - exactly 0/0 (NaN) at any bin that is precisely
    zero. That never came up while this loss only ever saw AmbienceFDN's render (continuously
    excited from t=0, so no sample - and therefore no whole STFT window - is ever exactly zero);
    it broke immediately (backward() raised a RuntimeError, caught by torch.autograd.
    set_detect_anomaly's diagnostic) once effects/nonlin/model.py's NonLinGatedFDN was fit for
    real - that render genuinely has exact-zero samples for the first several ms (before its
    shortest delay line's first arrival), and an early STFT window landing entirely inside that
    silence hits the singularity. Adding eps^2 inside the sqrt gives a well-defined, bounded
    gradient everywhere, including at true zero, and is imperceptible for any bin that isn't."""
    return torch.sqrt(z.real ** 2 + z.imag ** 2 + eps ** 2)


def stft_magnitude_loss(rendered: torch.Tensor, target: torch.Tensor, fft_sizes: tuple[int, ...] = (512, 1024, 2048), hop_divisor: int = 4, log_eps: float = 1e-6) -> torch.Tensor:
    """Multi-resolution STFT magnitude loss: linear-magnitude L1 (captures loud/broadband
    differences) + log-magnitude L1 (captures quiet-tail/tonal differences that linear-scale L1
    would under-weight) at each of several FFT sizes, averaged."""
    total = rendered.new_zeros(())
    for n_fft in fft_sizes:
        hop = max(1, n_fft // hop_divisor)
        window = torch.hann_window(n_fft, device=rendered.device)
        r_mag = _safe_complex_abs(torch.stft(rendered, n_fft=n_fft, hop_length=hop, window=window, return_complex=True))
        t_mag = _safe_complex_abs(torch.stft(target, n_fft=n_fft, hop_length=hop, window=window, return_complex=True))
        total = total + F.l1_loss(r_mag, t_mag) + F.l1_loss(torch.log(r_mag + log_eps), torch.log(t_mag + log_eps))
    return total / len(fft_sizes)


def envelope_loss(rendered: torch.Tensor, target: torch.Tensor, window_samples: int = 256) -> torch.Tensor:
    """L1 loss on log short-time energy (windowed mean of x^2) - a decay-rate-sensitive term the
    STFT loss alone doesn't strongly weight (an STFT loss cares about spectral content per frame
    more than the overall loudness trend across frames)."""
    hop = max(1, window_samples // 2)
    r_energy = F.avg_pool1d((rendered ** 2).unsqueeze(1), window_samples, stride=hop).squeeze(1)
    t_energy = F.avg_pool1d((target ** 2).unsqueeze(1), window_samples, stride=hop).squeeze(1)
    return F.l1_loss(torch.log(r_energy + 1e-8), torch.log(t_energy + 1e-8))


def weighted_envelope_loss(rendered: torch.Tensor, target: torch.Tensor, window_samples: int = 256) -> torch.Tensor:
    """Like envelope_loss() above, but each frame is weighted by the TARGET's own rate of change
    (in log-energy, detached - a fitting weight, not something gradients flow through). Added for
    effects/nonlin: a gate's shape lives almost entirely in its build-up and knee/fall, which span
    far fewer frames than the long, nearly-flat plateau between them - plain envelope_loss (equal
    weight per frame) lets the plateau win by sheer frame count and can leave the fit under-
    penalized for getting the build-up/fall exactly right. rendered/target: [batch, num_samples]
    (2D, same convention as envelope_loss - flatten a stereo [batch, 2, num_samples] tensor's
    channel into batch before calling, same as stft_magnitude_loss/envelope_loss)."""
    hop = max(1, window_samples // 2)
    r_energy = F.avg_pool1d((rendered ** 2).unsqueeze(1), window_samples, stride=hop).squeeze(1)
    t_energy = F.avg_pool1d((target ** 2).unsqueeze(1), window_samples, stride=hop).squeeze(1)
    r_log = torch.log(r_energy + 1e-8)
    t_log = torch.log(t_energy + 1e-8)

    slope = torch.zeros_like(t_log)
    if t_log.shape[-1] > 1:
        diffs = (t_log[:, 1:] - t_log[:, :-1]).abs()
        slope[:, :-1] = diffs
        slope[:, -1] = diffs[:, -1]
    weight = (slope.detach() + 1e-3)
    weight = weight / weight.mean(dim=-1, keepdim=True).clamp(min=1e-8)

    return (weight * (r_log - t_log).abs()).mean()


def mid_side_stft_loss(rendered_stereo: torch.Tensor, target_stereo: torch.Tensor, fft_sizes: tuple[int, ...] = (512, 1024, 2048), hop_divisor: int = 4, log_eps: float = 1e-6) -> torch.Tensor:
    """stft_magnitude_loss() applied to Mid=(L+R)/2 and Side=(L-R)/2 rather than per-channel L/R.
    A per-channel magnitude loss is phase-blind and therefore structurally cannot see
    interchannel correlation at all (two signals can have identical per-channel magnitude spectra
    while being fully correlated or fully independent) - matching M and S magnitudes is sensitive
    to both the per-channel content AND the interchannel relationship, which a plain per-channel
    loss can't distinguish. rendered_stereo/target_stereo: [batch, 2, num_samples]."""
    r_mid = (rendered_stereo[:, 0] + rendered_stereo[:, 1]) / 2.0
    r_side = (rendered_stereo[:, 0] - rendered_stereo[:, 1]) / 2.0
    t_mid = (target_stereo[:, 0] + target_stereo[:, 1]) / 2.0
    t_side = (target_stereo[:, 0] - target_stereo[:, 1]) / 2.0
    return (
        stft_magnitude_loss(r_mid, t_mid, fft_sizes, hop_divisor, log_eps)
        + stft_magnitude_loss(r_side, t_side, fft_sizes, hop_divisor, log_eps)
    ) / 2.0


def decorrelation_regularizer(rendered_stereo: torch.Tensor, target_correlation: float = 0.0) -> torch.Tensor:
    """Penalizes the rendered stereo pair's zero-lag normalized correlation away from
    `target_correlation` - a cheap, explicit guard against an otherwise-invisible degeneracy: the
    magnitude-only losses above (stft_magnitude_loss/mid_side_stft_loss on their own) could in
    principle be satisfied by a MORE correlated stereo image than the real hardware has, since
    per-channel or M/S magnitude alone under-constrains the exact interchannel phase relationship.
    rendered_stereo: [batch, 2, num_samples]. Belt-and-braces with mid_side_stft_loss, not a
    replacement for it - names the degeneracy explicitly rather than relying on the M/S loss
    alone to rule it out."""
    l = rendered_stereo[:, 0]
    r = rendered_stereo[:, 1]
    l = l - l.mean(dim=-1, keepdim=True)
    r = r - r.mean(dim=-1, keepdim=True)
    denom = (l.norm(dim=-1) * r.norm(dim=-1)).clamp(min=1e-8)
    corr = (l * r).sum(dim=-1) / denom
    return ((corr - target_correlation) ** 2).mean()


def build_loss(spectral_weight: float = 1.0, envelope_weight: float = 1.0, fft_sizes: tuple[int, ...] = (512, 1024, 2048)):
    """Returns a loss_fn(rendered, target) -> scalar tensor combining the two terms above."""

    def loss_fn(rendered: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
        return spectral_weight * stft_magnitude_loss(rendered, target, fft_sizes) + envelope_weight * envelope_loss(rendered, target)

    return loss_fn


@dataclass
class FitResult:
    loss_curve: list[float] = field(default_factory=list)
    final_loss: float = float("nan")
    converged: bool = False
    diverged_at_step: int | None = None


def fit_model(model: torch.nn.Module, target_audio: torch.Tensor, loss_fn, iters: int = 1000, lr: float = 0.02, log_every: int = 100, regularization_fn=None) -> FitResult:
    """Runs Adam against model's parameters to minimize loss_fn(model(), target_audio).

    `model()` (no arguments) must render and return the current [batch, num_samples] waveform
    from its own nn.Parameters - the interface effects/ambience/model.py's AmbienceFDN implements
    for the impulse-response fitting case. (A paired dry/wet variant, where forward() would take
    a dry input, is a known gap - not needed until a nonlinear/time-varying effect requires it,
    per the stated build-order discipline.)

    Explicit NaN/Inf guard at every step on both the loss and the gradients - mirrors this
    codebase's std::isfinite() guard at FDN recirculation points (ShieldsFDNEngine/
    IntruderFDNEngine): abort with a clear diagnostic (via FitResult.diverged_at_step) rather than
    silently returning garbage fitted parameters from a run that blew up.

    `regularization_fn`, if given, is called as regularization_fn(model) -> scalar tensor and
    added to the loss before backward(). Use it to break genuine degeneracy between parameters
    that can both explain the same effect (e.g. Ambience's output tilt and per-line damping can
    both shape frequency response, and an unregularized fit distributed that ambiguity
    inconsistently across captures - noisy, sometimes even sign-flipped tilt gains, on an
    otherwise clean fit) by penalizing drift away from a semantically neutral default.
    """
    optimizer = torch.optim.Adam(model.parameters(), lr=lr)
    loss_curve: list[float] = []
    diverged_at_step = None

    for step in range(iters):
        optimizer.zero_grad()
        rendered = model()
        loss = loss_fn(rendered, target_audio)
        if regularization_fn is not None:
            loss = loss + regularization_fn(model)

        if not torch.isfinite(loss):
            diverged_at_step = step
            break

        loss.backward()

        grads_finite = all(
            p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()
        )
        if not grads_finite:
            diverged_at_step = step
            break

        optimizer.step()
        loss_curve.append(float(loss.item()))
        if log_every and step % log_every == 0:
            print(f"  step {step}: loss={loss.item():.6f}")

    converged = diverged_at_step is None and len(loss_curve) > 0
    return FitResult(
        loss_curve=loss_curve,
        final_loss=loss_curve[-1] if loss_curve else float("nan"),
        converged=converged,
        diverged_at_step=diverged_at_step,
    )


def held_out_split(items: list, fraction: float = 0.2, seed: int = 0) -> tuple[list, list]:
    """Randomly splits items into (train, held_out) - held_out has round(len(items)*fraction)
    items, at least 1 if items is non-empty."""
    if not items:
        return [], []
    rng = random.Random(seed)
    shuffled = list(items)
    rng.shuffle(shuffled)
    n_held = max(1, round(len(shuffled) * fraction))
    return shuffled[n_held:], shuffled[:n_held]
