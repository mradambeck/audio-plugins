#include "AuraParameterMap.h"
#include "AuraReferenceData.h"
#include "AuraOnsetTiltData.h"
#include "../../common/dsp/FittedCurve1D.h"

namespace
{
    template <size_t N>
    wildjag::dsp::FittedCurve1D<N> toCurve(const std::array<wildjag::dsp::FittedPoint, N>& src)
    {
        std::array<typename wildjag::dsp::FittedCurve1D<N>::Point, N> pts;
        for (size_t i = 0; i < N; ++i)
            pts[i] = { src[i].x, src[i].y };
        return wildjag::dsp::FittedCurve1D<N>(pts);
    }

    template <size_t N>
    wildjag::dsp::FittedCurve1D<N> toCurve(const std::array<AuraOnsetTiltData::HighToOnsetTiltPoint, N>& src)
    {
        std::array<typename wildjag::dsp::FittedCurve1D<N>::Point, N> pts;
        for (size_t i = 0; i < N; ++i)
            pts[i] = { src[i].highDb, src[i].onsetTiltDb };
        return wildjag::dsp::FittedCurve1D<N>(pts);
    }
}

namespace AuraParameterMap
{
    BandGains mapTimeAndHighToBandGains(float timeSeconds, float highDb, bool* extrapolated)
    {
        static const auto timeToHighBandGain = toCurve(wildjag::dsp::time_to_high_band_gainPoints);
        static const auto highToHighBandGainOffset = toCurve(wildjag::dsp::high_to_high_band_gain_offsetPoints);
        static const auto timeToLowBandGain = toCurve(wildjag::dsp::time_to_low_band_gainPoints);
        static const auto highToLowBandGainOffset = toCurve(wildjag::dsp::high_to_low_band_gain_offsetPoints);
        static const auto timeToDamping = toCurve(wildjag::dsp::time_to_damping_weight_meanPoints);
        static const auto highToDampingOffset = toCurve(wildjag::dsp::high_to_damping_weight_mean_offsetPoints);

        bool timeExtrapolated = false, highExtrapolated = false;
        bool tmp1 = false, tmp2 = false;

        const auto highBandGain = timeToHighBandGain.evaluate(timeSeconds, &timeExtrapolated)
            + highToHighBandGainOffset.evaluate(highDb, &highExtrapolated);
        const auto lowBandGain = timeToLowBandGain.evaluate(timeSeconds, &tmp1)
            + highToLowBandGainOffset.evaluate(highDb, &tmp2);
        const auto dampingWeight = timeToDamping.evaluate(timeSeconds, &tmp1)
            + highToDampingOffset.evaluate(highDb, &tmp2);

        if (extrapolated != nullptr)
            *extrapolated = timeExtrapolated || highExtrapolated;

        return { highBandGain, lowBandGain, dampingWeight };
    }

    float mapInputTiltDb(float highDb, bool* extrapolated)
    {
        static const auto highToOnsetTilt = toCurve(AuraOnsetTiltData::highToOnsetTiltDb);

        // Delta from the High=0 (neutral) reference point - see this function's declaration
        // comment for why the absolute measured value can't be fed to TiltFilter directly.
        const auto neutralOnsetTiltDb = AuraOnsetTiltData::highToOnsetTiltDb.back().onsetTiltDb;
        return highToOnsetTilt.evaluate(highDb, extrapolated) - neutralOnsetTiltDb;
    }
}
