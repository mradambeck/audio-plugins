#include "AuraParameterMap.h"
#include "AuraReferenceData.h"
#include "AuraOnsetTiltData.h"
#include "AuraDecayGainData.h"
#include "AuraSubBassGainData.h"
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

    wildjag::dsp::FittedCurve1D<8> toDecayGainCurve(const std::array<AuraDecayGainData::TimeToDecayGainPoint, 8>& src)
    {
        std::array<wildjag::dsp::FittedCurve1D<8>::Point, 8> pts;
        for (size_t i = 0; i < 8; ++i)
            pts[i] = { src[i].timeSeconds, src[i].decayGain };
        return wildjag::dsp::FittedCurve1D<8>(pts);
    }

    wildjag::dsp::FittedCurve1D<8> toDampingCurve(const std::array<AuraDecayGainData::TimeToDecayGainPoint, 8>& src)
    {
        std::array<wildjag::dsp::FittedCurve1D<8>::Point, 8> pts;
        for (size_t i = 0; i < 8; ++i)
            pts[i] = { src[i].timeSeconds, src[i].dampingWeight };
        return wildjag::dsp::FittedCurve1D<8>(pts);
    }

    template <size_t N>
    wildjag::dsp::FittedCurve1D<N> toCurve(const std::array<AuraDecayGainData::HighToDecayGainOffsetPoint, N>& src)
    {
        std::array<typename wildjag::dsp::FittedCurve1D<N>::Point, N> pts;
        for (size_t i = 0; i < N; ++i)
            pts[i] = { src[i].highDb, src[i].decayGainOffset };
        return wildjag::dsp::FittedCurve1D<N>(pts);
    }

    wildjag::dsp::FittedCurve1D<8> toSubBassGainCurve(const std::array<AuraSubBassGainData::TimeToSubBassGainPoint, 8>& src)
    {
        std::array<wildjag::dsp::FittedCurve1D<8>::Point, 8> pts;
        for (size_t i = 0; i < 8; ++i)
            pts[i] = { src[i].timeSeconds, src[i].subBassGain };
        return wildjag::dsp::FittedCurve1D<8>(pts);
    }
}

namespace AuraParameterMap
{
    DecayParams mapTimeAndHighToDecayParams(float timeSeconds, float highDb, bool* extrapolated)
    {
        static const auto timeToDecayGain = toDecayGainCurve(AuraDecayGainData::timeToDecayGain);
        static const auto highToDecayGainOffset = toCurve(AuraDecayGainData::highToDecayGainOffset);
        static const auto timeToDamping = toDampingCurve(AuraDecayGainData::timeToDecayGain);
        static const auto highToDampingOffset = toCurve(wildjag::dsp::high_to_damping_weight_mean_offsetPoints);

        bool timeExtrapolated = false, highExtrapolated = false;
        bool tmp1 = false, tmp2 = false;

        const auto decayGain = timeToDecayGain.evaluate(timeSeconds, &timeExtrapolated)
            + highToDecayGainOffset.evaluate(highDb, &highExtrapolated);
        const auto dampingWeight = timeToDamping.evaluate(timeSeconds, &tmp1)
            + highToDampingOffset.evaluate(highDb, &tmp2);

        if (extrapolated != nullptr)
            *extrapolated = timeExtrapolated || highExtrapolated;

        return { decayGain, dampingWeight };
    }

    float mapInputTiltDb(float highDb, bool* extrapolated)
    {
        static const auto highToOnsetTilt = toCurve(AuraOnsetTiltData::highToOnsetTiltDb);

        // Delta from the High=0 (neutral) reference point - see this function's declaration
        // comment for why the absolute measured value can't be fed to TiltFilter directly.
        const auto neutralOnsetTiltDb = AuraOnsetTiltData::highToOnsetTiltDb.back().onsetTiltDb;
        return highToOnsetTilt.evaluate(highDb, extrapolated) - neutralOnsetTiltDb;
    }

    float mapTimeToSubBassGain(float timeSeconds, bool* extrapolated)
    {
        static const auto timeToSubBassGain = toSubBassGainCurve(AuraSubBassGainData::timeToSubBassGain);
        return timeToSubBassGain.evaluate(timeSeconds, extrapolated);
    }
}
