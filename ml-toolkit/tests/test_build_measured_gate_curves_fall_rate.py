"""_build_fall_rate_curves - added after a real "the delay tail still isn't quite a match"
listening complaint traced to the same two bugs already found and fixed for
plateau_droop_db_per_s and t_knee_ms (see build_measured_gate_curves.py's own docstrings): naive
High-pooling of the Time baseline, and fallRateDbPerSec double-counting plateauDroopDbPerSec's own
ongoing contribution past the knee (plateauDb(t) = plateauDroopDbPerSec * t never turns off).

A THIRD, narrower bug specific to this parameter is also guarded here: _isotonic_nondecreasing
(built for t_knee_ms, which increases with Time) collapsed all of fall_rate_db_per_s's Time points
to a single flat value when applied directly, because this parameter DECREASES (gets steeper) with
Time - the opposite trend. Fixed by negating before/after; see the last test below."""
from effects.nonlin.build_measured_gate_curves import _build_fall_rate_curves


def _fake_features(entries: list[tuple[float, float, float]]) -> dict:
    """entries: list of (time, high, fall_rate_db_per_s)."""
    return {
        "captures": [
            {"params": {"time": t, "high": h}, "gate": {"fall_rate_db_per_s": fall}}
            for t, h, fall in entries
        ]
    }


def _flat_plateau_curves(value: float = 0.0) -> dict:
    """A curves dict whose time_to_plateau_droop_db_per_s is flat at `value` - isolates a test
    from the additive correction (written == target - value, a simple, predictable offset)."""
    return {
        "time_to_plateau_droop_db_per_s": {
            "points": [[0.1, value], [9.8, value]],
        },
    }


def test_baseline_corrects_high_only_captures_instead_of_pooling_raw():
    """The actual regression this guards, identical in shape to t_knee_ms's own version of this
    test: a Time setting that ONLY exists at a non-zero High must have its raw measurement
    corrected toward an H=0-equivalent TARGET using the offset curve, not pooled in raw."""
    features = _fake_features([
        (9.0, 0.0, -200.0),
        (9.0, -4.0, -300.0),
        (1.0, -4.0, -150.0),
    ])
    result = _build_fall_rate_curves(features, _flat_plateau_curves())
    baseline = dict(result["time_to_fall_rate_db_per_s"]["points"])

    # Time=1.0's H0-equivalent target is 150 (raw -150 minus the -100 offset at High=-4), and with
    # a flat zero plateau curve the written value equals the target unchanged.
    assert abs(baseline[1.0] - (-50.0)) < 1e-6, (
        f"Time=1.0 (High=-4 only) should be corrected to an H0-equivalent target (-150 - (-100) "
        f"offset = -50), not pooled in raw (-150) - got {baseline[1.0]}"
    )


def test_naive_high_pooling_would_have_corrupted_the_baseline():
    """Reproduces the exact real-world bug concretely: an extreme High!=0 capture at the same Time
    as a normal High=0 capture must NOT shift that Time's own baseline value at all (mirrors
    test_build_measured_gate_curves_plateau_droop's own version of this test)."""
    features_clean = _fake_features([
        (2.2, 0.0, -140.0), (4.8, 0.0, -140.0), (7.0, 0.0, -140.0),
        (9.0, 0.0, -140.0), (9.0, -4.0, -140.0), (9.0, -9.0, -140.0),
    ])
    features_contaminated = _fake_features([
        (2.2, 0.0, -140.0), (4.8, 0.0, -140.0), (7.0, 0.0, -140.0),
        (9.0, 0.0, -140.0), (9.0, -4.0, -5000.0), (9.0, -9.0, -140.0),
    ])
    curves = _flat_plateau_curves()
    baseline_clean = dict(_build_fall_rate_curves(features_clean, curves)["time_to_fall_rate_db_per_s"]["points"])
    baseline_contaminated = dict(
        _build_fall_rate_curves(features_contaminated, curves)["time_to_fall_rate_db_per_s"]["points"]
    )
    assert abs(baseline_clean[9.0] - baseline_contaminated[9.0]) < 1e-6, (
        f"Time=9.0's baseline changed ({baseline_clean[9.0]} -> {baseline_contaminated[9.0]}) when "
        "an extreme High=-4 capture was added at the same Time - naive High-pooling regression"
    )


def test_written_value_subtracts_this_times_own_plateau_droop_target():
    """The additive-vs-total correction itself: with no override present, the written value must
    equal target_fall - plateau_target (using curves' own time_to_plateau_droop_db_per_s), not the
    raw target fall rate. Uses Time values that deliberately AVOID _FALL_RATE_CPP_VERIFIED_OVERRIDE's
    own real keys (4.8, 7.0, 9.8), so this test exercises the analytical fallback path, not the
    override table."""
    features = _fake_features([
        (2.3, 0.0, -100.0), (5.1, 0.0, -110.0), (7.3, 0.0, -120.0),
        (9.1, 0.0, -130.0), (9.1, -4.0, -140.0),
    ])
    curves = _flat_plateau_curves(value=-20.0)  # constant plateau contribution of -20dB/s
    result = _build_fall_rate_curves(features, curves)
    written = dict(result["time_to_fall_rate_db_per_s"]["points"])

    assert abs(written[2.3] - (-80.0)) < 1e-6, f"expected -100 - (-20) = -80, got {written[2.3]}"
    assert abs(written[5.1] - (-90.0)) < 1e-6, f"expected -110 - (-20) = -90, got {written[5.1]}"
    assert abs(written[7.3] - (-100.0)) < 1e-6, f"expected -120 - (-20) = -100, got {written[7.3]}"


def test_isotonic_regression_is_applied_in_the_decreasing_direction_not_collapsed_flat():
    """The bug caught while verifying this fix, not assumed correct from the t_knee_ms precedent:
    fall_rate_db_per_s gets MORE NEGATIVE (steeper) as Time increases - the OPPOSITE trend from
    t_knee_ms. Calling _isotonic_nondecreasing directly (t_knee_ms's own convention) collapsed all
    6 real Time points to a single flat value. This test reproduces that shape with a small,
    synthetic, non-monotonic-in-the-wrong-direction sequence and asserts only the LOCAL violation
    gets pooled, not the entire curve."""
    # 2.0 -> 4.0 is a violation (-90 is LESS steep than -100, should be more steep as Time grows);
    # 4.0 -> 6.0 -> 8.0 are a clean, already-monotonic-decreasing (steepening) run.
    features = _fake_features([
        (2.0, 0.0, -100.0),
        (4.0, 0.0, -90.0),
        (6.0, 0.0, -150.0),
        (8.0, 0.0, -160.0),
        (8.0, -5.0, -160.0),  # gives the function a (degenerate, zero-offset) High sweep to use
    ])
    result = _build_fall_rate_curves(features, _flat_plateau_curves())
    written = dict(result["time_to_fall_rate_db_per_s"]["points"])

    # The 6.0/8.0 pair must be UNTOUCHED by the 2.0/4.0 violation - a global flatten (the bug this
    # test guards) would have pooled all four points to one shared value instead.
    assert abs(written[6.0] - (-150.0)) < 1e-6, (
        f"Time=6.0 should be untouched by the 2.0/4.0 violation (still -150), got {written[6.0]} "
        "- isotonic regression may have been applied in the wrong (non-decreasing) direction, "
        "collapsing the whole curve flat"
    )
    assert abs(written[8.0] - (-160.0)) < 1e-6, f"Time=8.0 should stay at -160, got {written[8.0]}"
    # The 2.0/4.0 violation itself should be pooled to their average, the smallest possible edit.
    assert abs(written[2.0] - (-95.0)) < 1e-6, f"expected 2.0/4.0 pooled to -95, got {written[2.0]}"
    assert abs(written[4.0] - (-95.0)) < 1e-6, f"expected 2.0/4.0 pooled to -95, got {written[4.0]}"
