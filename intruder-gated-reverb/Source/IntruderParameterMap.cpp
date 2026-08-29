#include "IntruderParameterMap.h"
#include "IntruderReferenceData.h"

#include <algorithm>

namespace
{
    // Piecewise-linear interpolation over IntruderReferenceData::hToOnsetTiltDb, linearly
    // extrapolated by the nearest segment's slope outside the measured range. Returns the raw
    // measured onset-tilt value (an ABSOLUTE spectral quantity - 10*log10(HF/LF energy) - not a
    // TiltFilter coloration delta; see mapTiltDbFromH()'s comment for why that distinction matters).
    float interpolatedOnsetTiltDb(float hDb)
    {
        const auto& points = IntruderReferenceData::hToOnsetTiltDb;

        if (hDb <= points.front().hDb)
        {
            const auto& a = points[0];
            const auto& b = points[1];
            const auto slope = (b.onsetTiltDb - a.onsetTiltDb) / (b.hDb - a.hDb);
            return a.onsetTiltDb + slope * (hDb - a.hDb);
        }

        if (hDb >= points.back().hDb)
        {
            const auto& a = points[points.size() - 2];
            const auto& b = points[points.size() - 1];
            const auto slope = (b.onsetTiltDb - a.onsetTiltDb) / (b.hDb - a.hDb);
            return b.onsetTiltDb + slope * (hDb - b.hDb);
        }

        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const auto& a = points[i];
            const auto& b = points[i + 1];
            if (hDb >= a.hDb && hDb <= b.hDb)
            {
                const auto t = (hDb - a.hDb) / (b.hDb - a.hDb);
                return a.onsetTiltDb + t * (b.onsetTiltDb - a.onsetTiltDb);
            }
        }

        return points.back().onsetTiltDb; // unreachable given the bounds checks above
    }
}

namespace IntruderParameterMap
{
    float mapTiltDbFromH(float hDb, bool* extrapolated)
    {
        const auto& points = IntruderReferenceData::hToOnsetTiltDb;

        if (extrapolated != nullptr)
            *extrapolated = (hDb < points.front().hDb || hDb > points.back().hDb);

        // TiltFilter's tiltDb is a coloration DELTA (0 = no shelf boost/cut applied), but the
        // reference table holds ABSOLUTE measured onset tilt - and the engine's own ER/diffuser
        // structure has real inherent spectral tilt even with zero added coloration (confirmed by
        // rendering at H=0 and finding ~+9-15dB onset tilt, not 0dB). Feeding the absolute measured
        // target straight into TiltFilter stacked it on top of that baseline instead of replacing
        // it - caught by re-running analyze_irs.py on renders at H=0/-4/-9 and finding the
        // interpolated SHAPE was right but everything was offset ~9-12dB too bright. Fixed by
        // returning the delta from the H=0 reference point (0dB on the hardware reads as the
        // "neutral" position, matching TiltFilter's own zero-point convention) rather than the
        // absolute measured value.
        const auto neutralOnsetTiltDb = points.back().onsetTiltDb; // points.back() is H=0 (IntruderReferenceData.h enforces this ordering via static_assert)
        return interpolatedOnsetTiltDb(hDb) - neutralOnsetTiltDb;
    }

    float mapTighterToSpacingMultiplier(float tighter01)
    {
        const auto clamped = std::clamp(tighter01, 0.0f, 1.0f);
        return 1.0f + clamped * (IntruderReferenceData::tighterHalfRiseRatio - 1.0f);
    }
}
