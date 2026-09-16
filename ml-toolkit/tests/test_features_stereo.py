"""Stereo measurement functions, added for effects/nonlin's decorrelated-channel hardware. The
delayed-identical-channel case is the one that actually justifies iacc() existing alongside
interchannel_correlation() - see the docstrings."""
import numpy as np

from core.features import (
    band_interchannel_coherence,
    iacc,
    interchannel_correlation,
    mid_side_ratio_db,
)

SR = 44100


def test_identical_channels_are_fully_correlated():
    rng = np.random.default_rng(0)
    x = rng.standard_normal(SR)
    assert interchannel_correlation(x, x) > 0.999
    assert iacc(x, x, SR) > 0.999


def test_independent_noise_channels_are_near_zero_correlated():
    rng = np.random.default_rng(1)
    l = rng.standard_normal(SR)
    r = rng.standard_normal(SR)
    assert abs(interchannel_correlation(l, r)) < 0.05
    assert abs(iacc(l, r, SR)) < 0.05


def test_small_interchannel_delay_fools_zero_lag_correlation_but_not_iacc():
    """The whole reason iacc() exists: a fixed sub-millisecond delay between otherwise-identical
    channels reads as near-zero at zero lag (interchannel_correlation) while still being highly
    correlated at its own lag (iacc). Without this check, "decorrelated" and "delayed" are
    indistinguishable from interchannel_correlation() alone."""
    rng = np.random.default_rng(2)
    l = rng.standard_normal(SR)
    delay_samples = int(SR * 0.0005)  # 0.5ms
    r = np.roll(l, delay_samples)

    assert abs(interchannel_correlation(l, r)) < 0.1
    assert iacc(l, r, SR, max_lag_ms=1.0) > 0.99


def test_band_interchannel_coherence_reports_a_noise_floor():
    rng = np.random.default_rng(3)
    l = rng.standard_normal(SR)
    r = rng.standard_normal(SR)
    coh = band_interchannel_coherence(l, r, SR)
    assert coh["_noise_floor"] is not None
    assert 0.0 < coh["_noise_floor"] < 1.0
    # Independent-noise band coherence values should sit in the neighbourhood of the floor, not at
    # exactly zero and not close to 1.
    band_values = [v for k, v in coh.items() if k != "_noise_floor" and v is not None]
    assert band_values
    assert np.mean(band_values) < 0.5


def test_band_interchannel_coherence_is_near_one_for_identical_channels():
    rng = np.random.default_rng(4)
    x = rng.standard_normal(SR)
    coh = band_interchannel_coherence(x, x, SR)
    band_values = [v for k, v in coh.items() if k != "_noise_floor" and v is not None]
    assert band_values
    assert np.mean(band_values) > 0.9


def test_mid_side_ratio_very_negative_for_identical_channels():
    rng = np.random.default_rng(5)
    x = rng.standard_normal(SR)
    assert mid_side_ratio_db(x, x) < -60.0


def test_mid_side_ratio_near_zero_for_independent_channels():
    rng = np.random.default_rng(6)
    l = rng.standard_normal(SR)
    r = rng.standard_normal(SR)
    assert abs(mid_side_ratio_db(l, r)) < 2.0
