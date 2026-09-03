#pragma once

#include <array>
#include <cassert>
#include <cstddef>

namespace wildjag::dsp
{

// Generic piecewise-linear interpolation over a fixed, compile-time-sized set of (x, y) points,
// with linear extrapolation (by the nearest segment's slope) beyond the measured range. Points
// must be sorted ascending by x - core/export.py's write_cpp_header() (ml-toolkit/) generates the
// point arrays this is built from and asserts that ordering at generation time; the assert below
// is this class's own runtime safety net for the same invariant.
//
// Generalizes intruder-gated-reverb/Source/IntruderParameterMap.cpp's hand-written interpolation
// (piecewise-linear + an `extrapolated` out-flag) into reusable, templated machinery - a plugin
// typically holds several small FittedCurve1D instances (one per knob-to-DSP-parameter
// relationship), matching IntruderReferenceData.h's existing pattern of several small tables
// rather than one monolithic blob.
template <std::size_t NumPoints>
class FittedCurve1D
{
public:
    struct Point { float x; float y; };

    static_assert(NumPoints >= 2, "FittedCurve1D needs at least 2 points to interpolate between");

    constexpr explicit FittedCurve1D(const std::array<Point, NumPoints>& pointsIn) noexcept
        : points(pointsIn)
    {
        assert(isSortedAscendingByX());
    }

    // Piecewise-linear interpolation within [points.front().x, points.back().x]; linear
    // extrapolation (by the nearest segment's slope) outside it. `extrapolated`, if non-null, is
    // set to true when x fell outside the measured range - mirrors
    // IntruderParameterMap::mapTiltDbFromH's own extrapolated out-param, so a caller can flag an
    // unverified region rather than silently presenting it as fitted.
    float evaluate(float x, bool* extrapolated = nullptr) const noexcept
    {
        if (extrapolated != nullptr)
            *extrapolated = (x < points.front().x) || (x > points.back().x);

        if (x <= points.front().x)
            return extrapolateFrom(points[0], points[1], x);

        if (x >= points.back().x)
            return extrapolateFrom(points[NumPoints - 2], points[NumPoints - 1], x);

        for (std::size_t i = 0; i + 1 < NumPoints; ++i)
        {
            if (x <= points[i + 1].x)
            {
                const auto& a = points[i];
                const auto& b = points[i + 1];
                const auto fraction = (x - a.x) / (b.x - a.x);
                return a.y + fraction * (b.y - a.y);
            }
        }

        return points.back().y; // unreachable given the bounds checks above; keeps the compiler happy
    }

private:
    static float extrapolateFrom(const Point& a, const Point& b, float x) noexcept
    {
        const auto slope = (b.y - a.y) / (b.x - a.x);
        return a.y + slope * (x - a.x);
    }

    bool isSortedAscendingByX() const noexcept
    {
        for (std::size_t i = 0; i + 1 < NumPoints; ++i)
            if (points[i].x > points[i + 1].x)
                return false;
        return true;
    }

    std::array<Point, NumPoints> points;
};

} // namespace wildjag::dsp
