"""_build_t_knee_ms_curves and _isotonic_nondecreasing - added after a real, ear-caught "reverb
rings out too long at High=0" complaint traced to a double-counting bug: an earlier attempt at a
t_knee_ms High offset was reverted after it badly regressed short-Time captures, because the
Time-only baseline it was added to already implicitly contained an uncorrected High=-3 effect for
those captures (they only exist at High=-3 in the real set). See build_measured_gate_curves.py's
own _build_t_knee_ms_curves docstring for the full story."""
from effects.nonlin.build_measured_gate_curves import _build_t_knee_ms_curves, _isotonic_nondecreasing


def _fake_features(entries: list[tuple[float, float, float]]) -> dict:
    """entries: list of (time, high, knee_time_ms)."""
    return {
        "captures": [
            {"params": {"time": t, "high": h}, "gate": {"knee_time_ms": knee}}
            for t, h, knee in entries
        ]
    }


def test_baseline_corrects_high_only_captures_instead_of_pooling_raw():
    """The actual regression this test guards: a Time setting that ONLY exists at a non-zero
    High must have its raw measurement corrected toward an H=0-equivalent value using the offset
    curve, not pooled in raw (which is what caused the original double-counting bug once an
    offset was also applied downstream in InhaltParameterMap.cpp)."""
    # Richest-High-sweep Time=9.0 defines the offset: High=-4 measures 100ms MORE than High=0.
    # Time=1.0 only exists at High=-4, with a raw measurement of 150ms.
    features = _fake_features([
        (9.0, 0.0, 200.0),
        (9.0, -4.0, 300.0),
        (1.0, -4.0, 150.0),
    ])
    result = _build_t_knee_ms_curves(features)
    baseline = dict(result["time_to_t_knee_ms"]["points"])

    # Time=1.0's corrected value should be 150 - 100 = 50, NOT the raw 150.
    assert abs(baseline[1.0] - 50.0) < 1e-6, (
        f"Time=1.0 (High=-4 only) should be corrected to an H=0-equivalent baseline (150 - 100 "
        f"offset = 50), not pooled in raw (150) - got {baseline[1.0]}"
    )


def test_offset_curve_is_exported_and_zero_at_high0():
    features = _fake_features([
        (9.0, 0.0, 200.0),
        (9.0, -4.0, 300.0),
        (9.0, -9.0, 320.0),
        (5.0, 0.0, 150.0),  # a second distinct Time point - the baseline curve needs >= 2
    ])
    result = _build_t_knee_ms_curves(features)
    offsets = dict(result["high_to_t_knee_ms_offset"]["points"])
    assert abs(offsets[0.0]) < 1e-6
    assert abs(offsets[-4.0] - 100.0) < 1e-6
    assert abs(offsets[-9.0] - 120.0) < 1e-6


def test_isotonic_nondecreasing_fixes_a_single_dip():
    pairs = [(1.0, 10.0), (2.0, 20.0), (3.0, 15.0), (4.0, 25.0)]
    result = _isotonic_nondecreasing(pairs)
    ys = [v for _, v in result]
    assert all(ys[i] <= ys[i + 1] + 1e-9 for i in range(len(ys) - 1)), f"not monotonic: {ys}"
    # points 2 and 3 (20.0, 15.0) should be pooled to their average (17.5), leaving the sequence
    # [10, 17.5, 17.5, 25] - the smallest possible edit, not a global flattening.
    assert abs(ys[1] - 17.5) < 1e-6
    assert abs(ys[2] - 17.5) < 1e-6
    assert abs(ys[0] - 10.0) < 1e-6
    assert abs(ys[3] - 25.0) < 1e-6


def test_isotonic_nondecreasing_is_a_noop_for_already_monotonic_input():
    pairs = [(1.0, 10.0), (2.0, 20.0), (3.0, 30.0)]
    assert _isotonic_nondecreasing(pairs) == pairs


def test_isotonic_nondecreasing_propagates_pooling_backward():
    """A violation that requires pooling THREE points together (not just an adjacent pair) must
    correctly merge all of them, not just the immediate pair - guards the block-based PAVA
    implementation against the classic off-by-one bug in naive adjacent-swap approaches."""
    pairs = [(1.0, 30.0), (2.0, 10.0), (3.0, 20.0)]
    result = _isotonic_nondecreasing(pairs)
    ys = [v for _, v in result]
    assert all(ys[i] <= ys[i + 1] + 1e-9 for i in range(len(ys) - 1)), f"not monotonic: {ys}"
    expected = (30.0 + 10.0 + 20.0) / 3.0
    assert all(abs(y - expected) < 1e-6 for y in ys), f"expected all pooled to {expected}, got {ys}"
