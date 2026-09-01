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


def stft_magnitude_loss(rendered: torch.Tensor, target: torch.Tensor, fft_sizes: tuple[int, ...] = (512, 1024, 2048), hop_divisor: int = 4, log_eps: float = 1e-6) -> torch.Tensor:
    """Multi-resolution STFT magnitude loss: linear-magnitude L1 (captures loud/broadband
    differences) + log-magnitude L1 (captures quiet-tail/tonal differences that linear-scale L1
    would under-weight) at each of several FFT sizes, averaged."""
    total = rendered.new_zeros(())
    for n_fft in fft_sizes:
        hop = max(1, n_fft // hop_divisor)
        window = torch.hann_window(n_fft, device=rendered.device)
        r_mag = torch.stft(rendered, n_fft=n_fft, hop_length=hop, window=window, return_complex=True).abs()
        t_mag = torch.stft(target, n_fft=n_fft, hop_length=hop, window=window, return_complex=True).abs()
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


def fit_model(model: torch.nn.Module, target_audio: torch.Tensor, loss_fn, iters: int = 1000, lr: float = 0.02, log_every: int = 100) -> FitResult:
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
    """
    optimizer = torch.optim.Adam(model.parameters(), lr=lr)
    loss_curve: list[float] = []
    diverged_at_step = None

    for step in range(iters):
        optimizer.zero_grad()
        rendered = model()
        loss = loss_fn(rendered, target_audio)

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
