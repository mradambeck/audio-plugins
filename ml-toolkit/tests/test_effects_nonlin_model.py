"""NonLinGatedFDN - covers the two things that would silently corrupt a fit if broken: the
aliasing-convergence property the feedback-gain ceiling was chosen to guarantee (see model.py's
module docstring), and the basic shape/stability/decorrelation properties the rest of the pipeline
assumes."""
import torch

from effects.nonlin.model import NonLinGatedFDN, MAX_FEEDBACK_GAIN, rt60_estimate_s

SR = 44100.0


def _render_near_gain_ceiling(num_samples: int, seed: int = 0) -> torch.Tensor:
    torch.manual_seed(seed)
    model = NonLinGatedFDN(batch=1, num_samples=num_samples, sample_rate=SR)
    with torch.no_grad():
        model.feedback_gain_raw.fill_(8.0)  # sigmoid(8)=0.9997 -> effective ~0.9497, near the
        # MAX_FEEDBACK_GAIN=0.95 ceiling: the worst case for frequency-sampling aliasing, and the
        # exact case model.py's docstring measured at 1.42% error against Ambience's 0.985 ceiling.
        model.damping_weight_raw.fill_(-1.0)
    return model()


def test_render_is_converged_at_the_chosen_window():
    """The permanent regression guard for model.py's "Feedback gain ceiling: 0.95" docstring
    section - if MAX_FEEDBACK_GAIN or the delay sets are ever changed without re-running this
    check, this is what catches a silent return to the under-converged aliasing regime that made
    Ambience's inherited 0.985 ceiling wrong for this module."""
    n_a = int(SR * 1.5)
    n_b = int(SR * 3.0)
    out_a = _render_near_gain_ceiling(n_a)
    out_b = _render_near_gain_ceiling(n_b)

    diff = (out_a[0] - out_b[0, :, :n_a]).abs()
    peak = out_a[0].abs().max().item()
    rel_error_pct = diff.max().item() / peak * 100
    assert rel_error_pct < 0.5, (
        f"1.5s render diverges from 3.0s render by {rel_error_pct:.3f}% of peak - "
        "the render window is no longer converged at MAX_FEEDBACK_GAIN; see model.py's "
        "'Feedback gain ceiling' docstring section before changing either constant."
    )


def test_output_shape_and_finiteness():
    model = NonLinGatedFDN(batch=3, num_samples=4096, sample_rate=SR)
    out = model()
    assert out.shape == (3, 2, 4096)
    assert torch.isfinite(out).all()


def test_left_and_right_channels_differ():
    """Guards against an accidental delay-set copy-paste collapsing the two tanks into one."""
    model = NonLinGatedFDN(batch=1, num_samples=8192, sample_rate=SR)
    out = model()
    diff = (out[0, 0] - out[0, 1]).abs().max().item()
    assert diff > 1e-3


def test_rendered_channels_are_decorrelated():
    """Sanity-checks the disjoint-delay-set design against the same property
    core.features.interchannel_correlation() measures on real captures (~0.00-0.04 zero-lag,
    per effects/nonlin/findings.md)."""
    model = NonLinGatedFDN(batch=1, num_samples=int(SR * 0.5), sample_rate=SR)
    out = model()
    l = out[0, 0].detach().numpy()
    r = out[0, 1].detach().numpy()
    import numpy as np

    corr = abs(float(np.corrcoef(l, r)[0, 1]))
    assert corr < 0.15


def test_gradients_flow_to_every_learnable_parameter():
    """A silently-detached parameter (e.g. accidentally recomputed from a .detach()'d tensor)
    would make fit_nonlin.py optimize nothing for that parameter without erroring - this is the
    property that actually matters for the fit, not just "forward() runs"."""
    model = NonLinGatedFDN(batch=1, num_samples=4096, sample_rate=SR)
    out = model()
    out.sum().backward()
    for name, param in model.named_parameters():
        assert param.grad is not None, f"{name} received no gradient"
        assert torch.isfinite(param.grad).all(), f"{name} received a non-finite gradient"


def test_feedback_gain_never_exceeds_ceiling():
    torch.manual_seed(0)
    model = NonLinGatedFDN(batch=8, num_samples=2048, sample_rate=SR)
    with torch.no_grad():
        model.feedback_gain_raw.uniform_(-50, 50)  # extreme raw values, as an optimizer could reach
    gain = model.effective_feedback_gain()
    assert (gain <= MAX_FEEDBACK_GAIN).all()
    assert (gain > 0).all()


def test_rt60_estimate_matches_closed_form():
    # RT60 where gain^n = 0.001, n = (RT60_s * 1000 / mean_delay_ms)
    import math

    gain, mean_delay_ms = 0.95, 16.33
    rt60 = rt60_estimate_s(gain, mean_delay_ms)
    n_round_trips = rt60 * 1000.0 / mean_delay_ms
    assert abs(gain ** n_round_trips - 0.001) < 1e-6
