"""onset_density_loss() and spectral_flatness_loss() - added to catch the onset-density/
"openness vs. grit" gap DURING fitting (effects/nonlin/fit_nonlin.py) rather than after building
and listening to the plugin, per the actual sequence that happened on plugins/inhalt-nonlin's
first pass (see core.fit's own module-level docstrings on these two functions)."""
import torch

from core.fit import onset_density_loss, spectral_flatness_loss

torch.manual_seed(0)


def test_onset_density_loss_is_near_zero_for_identical_signals():
    x = torch.randn(2, 4096) * 0.1
    assert onset_density_loss(x, x, sample_rate=44100.0).item() < 1e-4


def test_onset_density_loss_gradients_flow():
    r = (torch.randn(2, 4096) * 0.1).requires_grad_()
    t = torch.randn(2, 4096) * 0.1
    loss = onset_density_loss(r, t, sample_rate=44100.0)
    loss.backward()
    assert torch.isfinite(r.grad).all()


def test_onset_density_loss_discriminates_sparse_from_dense_onset():
    """The actual property this loss exists to catch: a sparse impulse train (the render's
    original, thin onset) compared against dense noise (the real captures' own near-Gaussian
    onset) must cost substantially more than dense-vs-dense - otherwise this loss wouldn't have
    caught the real gap it was written for."""
    n = 2205  # 50ms at 44.1kHz, comfortably covers the default 20ms onset window
    dense_target = torch.randn(1, n) * 0.1

    sparse_render = torch.zeros(1, n)
    sparse_render[0, ::200] = 1.0  # an isolated spike every ~4.5ms - a sparse impulse train

    dense_render = torch.randn(1, n) * 0.1

    loss_sparse_vs_dense = onset_density_loss(sparse_render, dense_target, sample_rate=44100.0)
    loss_dense_vs_dense = onset_density_loss(dense_render, dense_target, sample_rate=44100.0)

    assert loss_sparse_vs_dense.item() > loss_dense_vs_dense.item() * 5, (
        f"sparse-vs-dense loss ({loss_sparse_vs_dense.item():.3f}) should be dramatically higher "
        f"than dense-vs-dense ({loss_dense_vs_dense.item():.3f}) - otherwise this loss doesn't "
        "actually discriminate the onset-density gap it was written to catch"
    )


def test_spectral_flatness_loss_is_near_zero_for_identical_signals():
    x = torch.randn(2, 4096) * 0.1
    assert spectral_flatness_loss(x, x).item() < 1e-4


def test_spectral_flatness_loss_gradients_flow():
    r = (torch.randn(2, 4096) * 0.1).requires_grad_()
    t = torch.randn(2, 4096) * 0.1
    loss = spectral_flatness_loss(r, t)
    loss.backward()
    assert torch.isfinite(r.grad).all()


def test_spectral_flatness_loss_discriminates_tonal_from_noisy():
    """A pure tone (highly UNflat - one dominant bin) compared against white noise (highly flat)
    must cost substantially more than noise-vs-noise - the "openness vs. grit" gap this loss
    targets is exactly a flatness mismatch."""
    n = 4096
    t = torch.arange(n, dtype=torch.float32)
    tone = 0.5 * torch.sin(2 * torch.pi * 440.0 * t / 44100.0).unsqueeze(0)
    noise_target = torch.randn(1, n) * 0.1
    noise_render = torch.randn(1, n) * 0.1

    loss_tone_vs_noise = spectral_flatness_loss(tone, noise_target)
    loss_noise_vs_noise = spectral_flatness_loss(noise_render, noise_target)

    assert loss_tone_vs_noise.item() > loss_noise_vs_noise.item() * 3, (
        f"tone-vs-noise flatness loss ({loss_tone_vs_noise.item():.3f}) should be substantially "
        f"higher than noise-vs-noise ({loss_noise_vs_noise.item():.3f})"
    )
