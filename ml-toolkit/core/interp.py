"""Interpolates a knob value to a fitted DSP parameter, from scattered (knob_value, fitted_value)
points measured across a capture grid.

Generalizes plugins/intruder-gated-reverb/Source/IntruderParameterMap.cpp's hand-written piecewise-linear
interpolation (with linear extrapolation beyond the measured range, flagged via an `extrapolated`
out-value) into reusable Python machinery that both a) drives Phase B's cross-validation and b)
feeds core/export.py's codegen, which regenerates the C++ side of the same interpolation.

Default is piecewise-linear, matching what Intruder already ships and unit-tests - only move to a
smoother fit (e.g. a PCHIP monotonic spline) if hand review of the fitted-values-vs-knob plots
visibly shows audible stepping between measured points. Ambience has three independent knobs
(Time, Low, High); the default assumption is that each is fit as its own 1D curve against
whichever parameter it drives (see effects/ambience/build_curves.py) - escalate to true N-D
interpolation only if cross-validation shows real cross-knob interaction 1D curves can't capture.
"""
from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass
class Curve1D:
    """A monotonically-sorted set of (x, y) points with piecewise-linear interpolation and
    linear extrapolation beyond the measured range."""

    xs: np.ndarray
    ys: np.ndarray

    def __post_init__(self) -> None:
        xs = np.asarray(self.xs, dtype=float)
        ys = np.asarray(self.ys, dtype=float)
        if xs.shape != ys.shape or xs.ndim != 1:
            raise ValueError("xs and ys must be 1D arrays of the same length")
        if xs.shape[0] < 2:
            raise ValueError("Curve1D needs at least 2 points")
        order = np.argsort(xs)
        self.xs = xs[order]
        self.ys = ys[order]

    def evaluate(self, x: float) -> tuple[float, bool]:
        """Returns (interpolated_value, extrapolated) - extrapolated is True when x fell outside
        [xs[0], xs[-1]], mirroring IntruderParameterMap::mapTiltDbFromH's `extrapolated` out-param
        so a caller (Python or, via the generated C++, a plugin) can flag an unverified region
        rather than silently presenting it as fitted."""
        xs, ys = self.xs, self.ys
        if x < xs[0]:
            slope = (ys[1] - ys[0]) / (xs[1] - xs[0])
            return float(ys[0] + slope * (x - xs[0])), True
        if x > xs[-1]:
            slope = (ys[-1] - ys[-2]) / (xs[-1] - xs[-2])
            return float(ys[-1] + slope * (x - xs[-1])), True
        return float(np.interp(x, xs, ys)), False

    def points(self) -> list[tuple[float, float]]:
        return list(zip(self.xs.tolist(), self.ys.tolist()))


def fit_curve(pairs: list[tuple[float, float]]) -> Curve1D:
    """Builds a Curve1D from a list of (knob_value, fitted_value) points - one point per
    distinct knob setting (average duplicate knob values before calling this, the way
    plugins/intruder-gated-reverb/analysis/findings.md averages repeat captures at the same setting)."""
    xs = np.array([p[0] for p in pairs], dtype=float)
    ys = np.array([p[1] for p in pairs], dtype=float)
    return Curve1D(xs, ys)
