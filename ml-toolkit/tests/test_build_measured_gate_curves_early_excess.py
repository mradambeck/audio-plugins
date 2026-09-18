"""_build_early_excess_curves - added for a real "let's go back to trying to get it to have the
same decay and timing as the convolution" complaint. Real NonLin captures' post-knee fall is
CURVED (core.features.post_knee_excess_db quantifies this), not the single constant dB/s rate
fall_rate_db_per_s alone can represent - this builds the new earlyExcessDb gate-shape term's own
Time baseline and High offset. See build_measured_gate_curves.py's own docstring for the full
story.

_EARLY_EXCESS_NATURAL_CPP_MEASURED is keyed by the real (time, high) pairs this project's own 9
captures use, so every test below builds its fake feature sets from THOSE exact pairs (Time=9.8's
own High=0/-4/-9 sweep, plus Time=2.2/4.8/7.0's own single points) rather than inventing new
(time, high) combinations - using an unmeasured pair is its own dedicated test below."""
from effects.nonlin.build_measured_gate_curves import (
    _EARLY_EXCESS_NATURAL_CPP_MEASURED,
    _build_early_excess_curves,
)


def _fake_features(entries: list[tuple[float, float, float]]) -> dict:
    """entries: list of (time, high, post_knee_excess_db)."""
    return {
        "captures": [
            {"params": {"time": t, "high": h}, "gate": {"post_knee_excess_db": excess}}
            for t, h, excess in entries
        ]
    }


def test_written_value_subtracts_the_renders_own_natural_excess():
    """The additive-vs-total correction: the written value must equal target minus that setting's
    own natural excess, not the raw target. Uses Time=9.8's own real High=0/-4/-9 sweep (natural
    1.21/1.19/1.21) so a High offset can be built, plus Time=2.2/High=0 (natural 0.22) as a second
    Time point."""
    features = _fake_features([
        (9.8, 0.0, 5.0),    # natural 1.21 -> written 3.79
        (9.8, -4.0, 3.0),   # natural 1.19 -> written 1.81
        (9.8, -9.0, 2.0),   # natural 1.21 -> written 0.79
        (2.2, 0.0, -2.915), # natural 0.22 -> written -3.135
    ])
    result = _build_early_excess_curves(features)
    baseline = dict(result["time_to_early_excess_db"]["points"])
    assert abs(baseline[2.2] - (-3.135)) < 1e-6, f"expected -2.915 - 0.22 = -3.135, got {baseline[2.2]}"
    # Time=9.8's own baseline is the H0-equivalent AVERAGE of all three of its own captures -
    # since the offset curve is built FROM this same sweep, all three should agree exactly at 3.79.
    assert abs(baseline[9.8] - 3.79) < 1e-6, f"expected all three H0-equivalents to agree at 3.79, got {baseline[9.8]}"


def test_missing_natural_override_is_skipped_not_guessed():
    """A (time, high) pair with no entry in _EARLY_EXCESS_NATURAL_CPP_MEASURED must be skipped
    entirely (printed, not silently defaulted to 0 or invented) - guards against a future capture
    set gaining a new Time/High combination without its own C++-measured natural value."""
    assert (3.3, 0.0) not in _EARLY_EXCESS_NATURAL_CPP_MEASURED, "test fixture must use an unmeasured pair"
    features = _fake_features([
        (9.8, 0.0, 5.0), (9.8, -4.0, 1.0), (9.8, -9.0, 0.5),
        (4.8, 0.0, 1.0),  # a second real Time point WITH a natural entry, so fit_curve has >= 2 points
        (3.3, 0.0, -1.0),  # no natural-override entry for this (time, high)
    ])
    result = _build_early_excess_curves(features)
    baseline = dict(result["time_to_early_excess_db"]["points"])
    assert 3.3 not in baseline, "a (time, high) pair with no natural-override entry should be skipped, not guessed"


def test_naive_high_pooling_would_have_corrupted_the_baseline():
    """Mirrors the equivalent test for fall_rate_db_per_s/plateau_droop_db_per_s: an extreme
    High!=0 capture at the same Time as a normal High=0 capture must not shift that Time's own
    baseline value - the actual regression class every other gate-shape parameter in this module
    already had."""
    features_clean = _fake_features([
        (9.8, 0.0, 2.0), (9.8, -4.0, 2.0), (9.8, -9.0, 2.0), (4.8, 0.0, 1.0),
    ])
    features_contaminated = _fake_features([
        (9.8, 0.0, 2.0), (9.8, -4.0, -500.0), (9.8, -9.0, 2.0), (4.8, 0.0, 1.0),
    ])
    baseline_clean = dict(_build_early_excess_curves(features_clean)["time_to_early_excess_db"]["points"])
    baseline_contaminated = dict(
        _build_early_excess_curves(features_contaminated)["time_to_early_excess_db"]["points"]
    )
    assert abs(baseline_clean[9.8] - baseline_contaminated[9.8]) < 1e-6, (
        f"Time=9.8's baseline changed ({baseline_clean[9.8]} -> {baseline_contaminated[9.8]}) when "
        "an extreme High=-4 capture was added at the same Time - naive High-pooling regression"
    )


def test_offset_is_built_from_richest_time_sweep():
    """The High offset curve should reflect Time=9.8's own real High sweep (the richest available),
    zero-anchored at High=0 - computed on the WRITTEN (natural-corrected) values, not the raw
    targets, so the offset itself already accounts for natural's own small per-High variation
    (1.21/1.19/1.21 at High=0/-4/-9) rather than assuming it's exactly constant."""
    features = _fake_features([
        (9.8, 0.0, 2.0), (9.8, -4.0, -2.0), (9.8, -9.0, -3.0),
        (4.8, 0.0, 1.0),  # a second Time point so the baseline curve has >= 2 points
    ])
    result = _build_early_excess_curves(features)
    offsets = dict(result["high_to_early_excess_db_offset"]["points"])
    # written = target - natural: High=0 -> 2.0-1.21=0.79; High=-4 -> -2.0-1.19=-3.19;
    # High=-9 -> -3.0-1.21=-4.21. offset = written - written(High=0).
    assert abs(offsets[0.0]) < 1e-6
    assert abs(offsets[-4.0] - (-3.98)) < 1e-6, f"expected -3.19 - 0.79 = -3.98, got {offsets[-4.0]}"
    assert abs(offsets[-9.0] - (-5.0)) < 1e-6, f"expected -4.21 - 0.79 = -5.00, got {offsets[-9.0]}"
