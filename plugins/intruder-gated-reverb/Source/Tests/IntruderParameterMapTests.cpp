#include "../IntruderParameterMap.h"
#include "../IntruderReferenceData.h"

#include <juce_core/juce_core.h>

class IntruderParameterMapTests : public juce::UnitTest
{
public:
    IntruderParameterMapTests() : juce::UnitTest("IntruderParameterMap", "Intruder") {}

    void runTest() override
    {
        beginTest("mapTiltDbFromH reproduces the measured points (as a delta from H=0) exactly at each measured H");
        {
            // mapTiltDbFromH returns a delta from the H=0 reference point, not the absolute
            // measured value - see its comment for why (TiltFilter's own zero-point means "no
            // added coloration", but the engine's raw signal has real inherent tilt even at
            // H=0, so feeding it the absolute measured value double-counts that baseline).
            const auto neutralOnsetTiltDb = IntruderReferenceData::hToOnsetTiltDb.back().onsetTiltDb;
            for (const auto& point : IntruderReferenceData::hToOnsetTiltDb)
            {
                bool extrapolated = true;
                const auto result = IntruderParameterMap::mapTiltDbFromH(point.hDb, &extrapolated);
                expectWithinAbsoluteError(result, point.onsetTiltDb - neutralOnsetTiltDb, 1.0e-4f);
                expect(!extrapolated, "a measured H value should not be reported as extrapolated");
            }

            expectWithinAbsoluteError(IntruderParameterMap::mapTiltDbFromH(0.0f), 0.0f, 1.0e-4f,
                "H=0 should map to exactly zero added coloration");
        }

        beginTest("mapTiltDbFromH interpolates monotonically between measured points");
        {
            // findings.md: onset tilt rises monotonically with H across the whole measured range.
            float previous = IntruderParameterMap::mapTiltDbFromH(-9.0f);
            for (float h = -8.5f; h <= 0.01f; h += 0.5f)
            {
                const auto current = IntruderParameterMap::mapTiltDbFromH(h);
                expect(current >= previous, "onset tilt should never decrease as H increases");
                previous = current;
            }
        }

        beginTest("mapTiltDbFromH flags extrapolation outside the measured -9..0dB range");
        {
            bool extrapolated = false;
            IntruderParameterMap::mapTiltDbFromH(-15.0f, &extrapolated);
            expect(extrapolated, "below the lowest measured H should be flagged as extrapolated");

            extrapolated = false;
            IntruderParameterMap::mapTiltDbFromH(5.0f, &extrapolated);
            expect(extrapolated, "above the highest measured H should be flagged as extrapolated");

            extrapolated = true;
            IntruderParameterMap::mapTiltDbFromH(-6.0f, &extrapolated);
            expect(!extrapolated, "within the measured range should not be flagged as extrapolated");
        }

        beginTest("mapTighterToSpacingMultiplier matches the measured endpoints and clamps");
        {
            expectWithinAbsoluteError(IntruderParameterMap::mapTighterToSpacingMultiplier(0.0f), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError(IntruderParameterMap::mapTighterToSpacingMultiplier(1.0f),
                IntruderReferenceData::tighterHalfRiseRatio, 1.0e-4f);

            expectWithinAbsoluteError(IntruderParameterMap::mapTighterToSpacingMultiplier(-1.0f), 1.0f, 1.0e-4f);
            expectWithinAbsoluteError(IntruderParameterMap::mapTighterToSpacingMultiplier(2.0f),
                IntruderReferenceData::tighterHalfRiseRatio, 1.0e-4f);
        }
    }
};

static IntruderParameterMapTests intruderParameterMapTests;
