"""normalized_echo_density() (Abel-Huang NED) and its sustained-crossing helper, plus
mixing_time_ms() as an independent second estimator of the same underlying question."""
import numpy as np

from core.features import mixing_time_ms, normalized_echo_density, time_to_ned_threshold

SR = 44100


def test_ned_on_gaussian_noise_is_near_one():
    rng = np.random.default_rng(0)
    x = rng.standard_normal(SR)
    _, ned = normalized_echo_density(x, SR, 0)
    assert abs(np.mean(ned) - 1.0) < 0.05


def test_ned_on_sparse_impulse_train_is_much_less_than_one():
    x = np.zeros(SR)
    x[::2000] = 1.0
    _, ned = normalized_echo_density(x, SR, 0)
    assert np.mean(ned) < 0.5


def test_time_to_ned_threshold_finds_sustained_crossing():
    times = np.array([0.0, 0.1, 0.2, 0.3, 0.4, 0.5])
    # a single-frame spurious crossing at t=0.1 that doesn't hold, then a real sustained one from t=0.3
    ned = np.array([0.2, 0.95, 0.2, 0.95, 0.96, 0.97])
    t = time_to_ned_threshold(times, ned, threshold=0.9, hold_frames=3)
    assert t == 0.3


def test_time_to_ned_threshold_returns_none_if_never_sustained():
    times = np.array([0.0, 0.1, 0.2])
    ned = np.array([0.1, 0.2, 0.3])
    assert time_to_ned_threshold(times, ned, threshold=0.9, hold_frames=3) is None


def test_mixing_time_near_zero_for_already_gaussian_signal():
    rng = np.random.default_rng(1)
    x = rng.standard_normal(SR // 2)
    t_ms = mixing_time_ms(x, SR, 0)
    assert t_ms is not None
    assert t_ms < 50.0


def test_mixing_time_is_later_for_a_signal_that_starts_sparse():
    # Sparse impulses for the first 100ms (high excess kurtosis), then Gaussian noise.
    rng = np.random.default_rng(2)
    n = SR // 2
    x = np.zeros(n)
    sparse_n = int(SR * 0.1)
    x[:sparse_n:400] = 1.0
    x[sparse_n:] = rng.standard_normal(n - sparse_n)
    t_ms = mixing_time_ms(x, SR, 0)
    assert t_ms is not None
    assert t_ms > 50.0
