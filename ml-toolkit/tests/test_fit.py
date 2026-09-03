import torch

from core.dsp_primitives import hadamard_matrix, render_fdn_impulse_response
from core.fit import FitResult, build_loss, fit_model, held_out_split


class _TinyFDN(torch.nn.Module):
    """Minimal fixed-topology FDN wrapping render_fdn_impulse_response, used only to sanity-check
    fit_model()/build_loss() recover known parameters from a synthetic target."""

    def __init__(self, delays: torch.Tensor, mixing_matrix: torch.Tensor, num_samples: int):
        super().__init__()
        self.delays = delays
        self.H = mixing_matrix
        self.num_samples = num_samples
        self.feedback_gain = torch.nn.Parameter(torch.tensor([[0.5]]))
        self.damping_weight = torch.nn.Parameter(torch.full((1, delays.shape[0]), 0.3))

    def forward(self):
        return render_fdn_impulse_response(self.delays, self.feedback_gain, self.damping_weight, self.H, self.num_samples)


def test_fit_model_recovers_known_parameters():
    torch.manual_seed(0)
    num_lines = 8
    num_samples = 8000
    delays = torch.tensor([37.0, 53.0, 71.0, 97.0, 113.0, 131.0, 151.0, 173.0])
    H = hadamard_matrix(num_lines)

    true_feedback_gain = 0.8
    true_damping = 0.15
    target = render_fdn_impulse_response(
        delays, torch.tensor([[true_feedback_gain]]), torch.full((1, num_lines), true_damping), H, num_samples
    ).detach()

    model = _TinyFDN(delays, H, num_samples)
    loss_fn = build_loss()
    result = fit_model(model, target, loss_fn, iters=300, lr=0.03, log_every=0)

    assert isinstance(result, FitResult)
    assert result.converged
    assert result.diverged_at_step is None
    assert abs(model.feedback_gain.item() - true_feedback_gain) < 0.05
    assert abs(model.damping_weight.mean().item() - true_damping) < 0.05
    # loss should have gone down substantially, not just stayed flat
    assert result.loss_curve[-1] < result.loss_curve[0] * 0.2


def test_held_out_split_sizes_and_disjoint():
    items = list(range(20))
    train, held_out = held_out_split(items, fraction=0.25, seed=1)
    assert len(held_out) == 5
    assert len(train) == 15
    assert set(train).isdisjoint(held_out)
    assert set(train) | set(held_out) == set(items)


def test_held_out_split_empty():
    assert held_out_split([]) == ([], [])


def test_fit_model_applies_regularization():
    torch.manual_seed(0)
    num_lines = 8
    num_samples = 4000
    delays = torch.tensor([37.0, 53.0, 71.0, 97.0, 113.0, 131.0, 151.0, 173.0])
    H = hadamard_matrix(num_lines)
    target = render_fdn_impulse_response(
        delays, torch.tensor([[0.8]]), torch.full((1, num_lines), 0.15), H, num_samples
    ).detach()

    model = _TinyFDN(delays, H, num_samples)
    loss_fn = build_loss()

    calls = []

    def reg_fn(m):
        calls.append(1)
        return (m.feedback_gain ** 2).sum() * 0.0  # no-op penalty, just proves it's wired up

    result = fit_model(model, target, loss_fn, iters=5, lr=0.03, log_every=0, regularization_fn=reg_fn)
    assert len(calls) == 5
    assert result.converged
