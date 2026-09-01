import math

import torch

from core.dsp_primitives import (
    cutoff_hz_from_weight,
    hadamard_matrix,
    real_rate_damping_weight,
    render_fdn_impulse_response,
)


def test_hadamard_matrix_is_orthogonal():
    H = hadamard_matrix(8)
    identity = H @ H.T
    assert torch.allclose(identity, torch.eye(8), atol=1e-5)


def test_render_fdn_impulse_response_matches_time_domain_reference():
    """Slow time-domain simulation, short delays so it finishes quickly - the same correctness
    check performed during the Phase A0 performance spike, kept as a permanent regression test."""
    torch.manual_seed(0)
    num_lines = 8
    num_samples = 4000
    delays_int = torch.tensor([37, 53, 71, 97, 113, 131, 151, 173])
    H = hadamard_matrix(num_lines)
    batch = 2
    feedback_gain = torch.rand(batch, 1) * 0.3 + 0.6
    damping_weight = torch.rand(batch, num_lines) * 0.4 + 0.1

    def time_domain_sim():
        max_delay = int(delays_int.max().item())
        buffers = torch.zeros(batch, num_lines, max_delay)
        damping_state = torch.zeros(batch, num_lines)
        out = torch.zeros(batch, num_samples)
        impulse = torch.zeros(batch, num_samples)
        impulse[:, 0] = 1.0
        for n in range(num_samples):
            wpos = n % max_delay
            rpos = (wpos - delays_int) % max_delay
            read_vals = buffers[:, torch.arange(num_lines), rpos]
            damping_state_local = damping_state + damping_weight * (read_vals - damping_state)
            mixed = damping_state_local @ H.T
            line_in = impulse[:, n : n + 1] + feedback_gain * mixed
            buffers[:, :, wpos] = line_in
            out[:, n] = mixed.sum(dim=1)
            damping_state.copy_(damping_state_local)
        return out

    ref = time_domain_sim()
    fast = render_fdn_impulse_response(delays_int.float(), feedback_gain, damping_weight, H, num_samples)

    trim = int(num_samples * 0.9)  # avoid the frequency-sampling method's circular-wraparound
    # region near the very end of a short test window - see core/dsp_primitives.py's module
    # docstring on the frequency-sampling caveat.
    diff = (ref[:, :trim] - fast[:, :trim]).abs()
    assert diff.max().item() < 1e-2


def test_render_fdn_impulse_response_is_finite_and_decays():
    torch.manual_seed(1)
    num_lines = 8
    num_samples = 44100 * 2
    delays = torch.tensor([1327.0, 1559.0, 1811.0, 2099.0, 2437.0, 2683.0, 2999.0, 3343.0])
    H = hadamard_matrix(num_lines)
    feedback_gain = torch.tensor([[0.85]])
    damping_weight = torch.full((1, num_lines), 0.2)

    ir = render_fdn_impulse_response(delays, feedback_gain, damping_weight, H, num_samples)
    assert torch.isfinite(ir).all()

    # energy in the second half should be well below the first half - a genuinely decaying tail,
    # not a runaway or non-decaying feedback loop.
    half = num_samples // 2
    first_half_energy = (ir[:, :half] ** 2).mean()
    second_half_energy = (ir[:, half:] ** 2).mean()
    assert second_half_energy < first_half_energy * 0.5


def test_cutoff_weight_round_trip():
    weight = torch.tensor([0.05, 0.2, 0.5])
    sr = 11025.0
    cutoff = cutoff_hz_from_weight(weight, sr)
    weight_back = real_rate_damping_weight(weight, sr, sr)
    assert torch.allclose(weight, weight_back, atol=1e-6)
    assert (cutoff > 0).all()


def test_real_rate_damping_weight_preserves_cutoff_across_sample_rates():
    weight_at_fit_rate = torch.tensor([0.1])
    fit_sr = 11025.0
    real_sr = 44100.0
    weight_at_real_rate = real_rate_damping_weight(weight_at_fit_rate, fit_sr, real_sr)

    cutoff_fit = cutoff_hz_from_weight(weight_at_fit_rate, fit_sr)
    cutoff_real = cutoff_hz_from_weight(weight_at_real_rate, real_sr)
    assert torch.allclose(cutoff_fit, cutoff_real, atol=1e-3)
