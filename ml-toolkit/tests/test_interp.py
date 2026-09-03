import pytest

from core.interp import Curve1D, fit_curve


def test_interpolation_within_range():
    curve = Curve1D([0.0, 1.0, 2.0], [0.0, 10.0, 10.0])
    value, extrapolated = curve.evaluate(0.5)
    assert value == pytest.approx(5.0)
    assert extrapolated is False


def test_extrapolation_below_range():
    curve = Curve1D([0.0, 1.0], [0.0, 2.0])
    value, extrapolated = curve.evaluate(-1.0)
    assert value == pytest.approx(-2.0)
    assert extrapolated is True


def test_extrapolation_above_range():
    curve = Curve1D([0.0, 1.0], [0.0, 2.0])
    value, extrapolated = curve.evaluate(2.0)
    assert value == pytest.approx(4.0)
    assert extrapolated is True


def test_points_sorted_regardless_of_input_order():
    curve = Curve1D([2.0, 0.0, 1.0], [20.0, 0.0, 10.0])
    assert curve.xs.tolist() == [0.0, 1.0, 2.0]
    assert curve.ys.tolist() == [0.0, 10.0, 20.0]


def test_fit_curve_builds_from_pairs():
    curve = fit_curve([(0.0, 1.0), (1.0, 2.0), (2.0, 4.0)])
    value, extrapolated = curve.evaluate(1.0)
    assert value == pytest.approx(2.0)
    assert extrapolated is False


def test_requires_at_least_two_points():
    with pytest.raises(ValueError):
        Curve1D([0.0], [0.0])
