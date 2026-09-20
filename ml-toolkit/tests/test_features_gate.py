"""gate_envelope_params() - the defining feature for effects/nonlin, with no prior art in this
codebase. Validated against synthesized envelopes with a KNOWN build-up/plateau/knee/fall shape,
not just "does it run"."""
import numpy as np

from core.features import find_onset, gate_envelope_params

SR = 44100


def _synthesize_gate(build_ms=5.0, plateau_ms=150.0, fall_db_per_s=-300.0, dur_s=0.6,
                      droop_db_per_s=0.0, seed=0):
    rng = np.random.default_rng(seed)
    n = int(SR * dur_s)
    env = np.ones(n)

    build_n = max(1, int(SR * build_ms / 1000))
    env[:build_n] = 1 - np.exp(-np.arange(build_n) / (build_n / 3))

    plateau_end_n = build_n + int(SR * plateau_ms / 1000)
    plateau_t = np.arange(plateau_end_n - build_n) / SR
    env[build_n:plateau_end_n] = 10 ** ((droop_db_per_s * plateau_t) / 20)

    fall_t = np.arange(n - plateau_end_n) / SR
    plateau_level_lin = env[plateau_end_n - 1] if plateau_end_n > 0 else 1.0
    env[plateau_end_n:] = plateau_level_lin * 10 ** ((fall_db_per_s * fall_t) / 20)

    return env * rng.standard_normal(n)


def test_gate_envelope_recovers_knee_time():
    x = _synthesize_gate(build_ms=5, plateau_ms=150, fall_db_per_s=-300)
    onset = find_onset(x, SR)
    params = gate_envelope_params(x, SR, onset)
    # Commanded knee is at build_up + plateau = 5 + 150 = 155ms from the envelope's own t=0, but
    # find_onset() (-20dB rel peak) fires a little after the envelope's literal start, so allow
    # a wider tolerance than the envelope's own internal precision.
    assert params["knee_time_ms"] is not None
    assert abs(params["knee_time_ms"] - 150) < 15


def test_gate_envelope_recovers_fall_rate():
    x = _synthesize_gate(fall_db_per_s=-300)
    onset = find_onset(x, SR)
    params = gate_envelope_params(x, SR, onset)
    assert params["fall_rate_db_per_s"] is not None
    assert abs(params["fall_rate_db_per_s"] - (-300)) / 300 < 0.05


def test_gate_envelope_recovers_plateau_droop():
    x = _synthesize_gate(droop_db_per_s=-20.0, plateau_ms=200)
    onset = find_onset(x, SR)
    params = gate_envelope_params(x, SR, onset)
    assert params["plateau_droop_db_per_s"] is not None
    assert abs(params["plateau_droop_db_per_s"] - (-20.0)) < 5.0


def test_gate_envelope_flat_plateau_has_near_zero_droop():
    x = _synthesize_gate(droop_db_per_s=0.0)
    onset = find_onset(x, SR)
    params = gate_envelope_params(x, SR, onset)
    assert abs(params["plateau_droop_db_per_s"]) < 2.0


def test_gate_envelope_20db_crossing_matches_commanded_shape():
    # build=5ms, plateau=150ms fall=-300dB/s -> -20dB point is ~155 + 20/300*1000 ~= 221.7ms
    # from the envelope's t=0; find_onset shifts the reference point slightly, so use a loose bound.
    x = _synthesize_gate(build_ms=5, plateau_ms=150, fall_db_per_s=-300)
    onset = find_onset(x, SR)
    params = gate_envelope_params(x, SR, onset)
    assert params["gate_length_ms_at_20db"] is not None
    assert 190 < params["gate_length_ms_at_20db"] < 250


def test_gate_shape_discriminator_is_slope_difference_not_r2():
    """knee_r2 alone does NOT distinguish a real gate from a plain exponential decay - a
    two-segment line fit is flexible enough to fit a pure exponential well too (both segments end
    up nearly collinear). The real discriminator is the SLOPE DIFFERENCE between the plateau and
    the fall: large for a real gate, small for a plain exponential. This test exists specifically
    to hold that empirical finding in place - see gate_envelope_params()'s docstring."""
    gated = _synthesize_gate(build_ms=5, plateau_ms=150, fall_db_per_s=-300, droop_db_per_s=0.0)
    onset_g = find_onset(gated, SR)
    g = gate_envelope_params(gated, SR, onset_g)

    rng = np.random.default_rng(1)
    n = int(SR * 0.6)
    exp_decay = rng.standard_normal(n) * np.exp(-np.arange(n) / SR / 0.15)
    onset_e = find_onset(exp_decay, SR)
    e = gate_envelope_params(exp_decay, SR, onset_e)

    gate_slope_gap = abs(g["plateau_droop_db_per_s"] - g["fall_rate_db_per_s"])
    exp_slope_gap = abs(e["plateau_droop_db_per_s"] - e["fall_rate_db_per_s"])

    # Both fits are good (r2 high) - that's expected and not the point of this test.
    assert g["knee_r2"] > 0.99
    assert e["knee_r2"] > 0.99
    # The gap is what tells them apart.
    assert gate_slope_gap > 100
    assert exp_slope_gap < 50


def test_gate_envelope_params_returns_none_fields_on_insufficient_data():
    x = np.zeros(10)
    params = gate_envelope_params(x, SR, 0)
    assert params["build_up_ms"] is None
