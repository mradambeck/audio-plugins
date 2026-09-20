"""_build_plateau_droop_curves - added after a real, ear-caught complaint traced to a genuine
pipeline bug: pooling plateau_droop_db_per_s across ALL High values at a given Time (the same way
tau_a_ms/t_knee_ms/fall_rate_db_per_s are legitimately pooled) silently corrupted the High=0
baseline with High!=0 captures, even though this specific parameter is documented elsewhere in
this project as NOT High-neutral. See build_measured_gate_curves.py's own module docstring and
_build_plateau_droop_curves' own docstring for the full story."""
import json
import os

from effects.nonlin.build_measured_gate_curves import _build_plateau_droop_curves

HERE = os.path.dirname(__file__)
REAL_CURVES_PATH = os.path.join(HERE, "..", "effects", "nonlin", "curves.json")


def _real_curves() -> dict:
    """The actual project curves.json, already populated with real feedback_gain/damping_weight/
    diffuser_gain/tau_a_ms/t_knee_ms/fall_rate_db_per_s/tau_k_ms Time curves -
    _measure_tank_natural_plateau_droop needs these to render a natural-droop estimate (even
    though, at the four real Time settings used below, a hand-verified override supersedes that
    estimate - see build_measured_gate_curves.py's own _NATURAL_DROOP_CPP_VERIFIED_OVERRIDE)."""
    return json.load(open(REAL_CURVES_PATH))


def _fake_features(time_to_h0_droop: dict, time_to_hneg_droop: dict) -> dict:
    captures = []
    for t, droop in time_to_h0_droop.items():
        captures.append({"params": {"time": t, "high": 0}, "gate": {"plateau_droop_db_per_s": droop}})
    for t, droop in time_to_hneg_droop.items():
        captures.append({"params": {"time": t, "high": -9}, "gate": {"plateau_droop_db_per_s": droop}})
    return {"captures": captures}


def test_plateau_droop_baseline_uses_only_high0_captures():
    """The actual regression this test guards: an EXTREME High!=0 value at the same Time as a
    normal High=0 value must NOT shift the baseline at all - reproduces the exact real-world
    scenario (Time=9.8 had High=0/-4/-9 captures with very different droop) with a deliberately
    extreme High=-9 value to make any pooling contamination obvious."""
    features_uncontaminated = _fake_features(
        time_to_h0_droop={2.2: -10.0, 4.8: -10.0, 7.0: -10.0, 9.8: -10.0},
        time_to_hneg_droop={},
    )
    features_with_extreme_hneg = _fake_features(
        time_to_h0_droop={2.2: -10.0, 4.8: -10.0, 7.0: -10.0, 9.8: -10.0},
        time_to_hneg_droop={9.8: -5000.0},  # a wildly different High=-9 value at the same Time
    )
    curves = _real_curves()

    result_clean = _build_plateau_droop_curves(features_uncontaminated, curves)
    result_contaminated = _build_plateau_droop_curves(features_with_extreme_hneg, curves)

    baseline_clean = dict(result_clean["time_to_plateau_droop_db_per_s"]["points"])
    baseline_contaminated = dict(result_contaminated["time_to_plateau_droop_db_per_s"]["points"])

    assert abs(baseline_clean[9.8] - baseline_contaminated[9.8]) < 1e-6, (
        f"Time=9.8's baseline changed ({baseline_clean[9.8]} -> {baseline_contaminated[9.8]}) "
        "when an extreme High=-9 capture was added at the same Time - the baseline is pooling "
        "across High instead of using High=0 only"
    )


def test_plateau_droop_offset_is_built_from_high_sweep_not_baseline():
    """The High offset curve should reflect the real difference between High=0 and other High
    captures at the richest-High-sweep Time - this is legitimate (unlike the baseline bug above),
    since the offset is SUPPOSED to capture how droop changes with High."""
    features = _fake_features(
        time_to_h0_droop={2.2: -10.0, 4.8: -10.0, 7.0: -10.0, 9.8: -10.0},
        time_to_hneg_droop={9.8: -55.0},
    )
    curves = _real_curves()
    result = _build_plateau_droop_curves(features, curves)

    offset_points = dict(result["high_to_plateau_droop_db_per_s_offset"]["points"])
    assert abs(offset_points[0.0] - 0.0) < 1e-6
    assert abs(offset_points[-9.0] - (-45.0)) < 1e-6, (
        f"High=-9 offset should be -45.0 (measured -55.0 minus High=0 baseline -10.0), "
        f"got {offset_points[-9.0]}"
    )
