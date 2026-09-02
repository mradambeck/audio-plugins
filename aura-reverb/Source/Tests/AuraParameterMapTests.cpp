#include "../AuraParameterMap.h"
#include "../AuraReferenceData.h"
#include "../AuraOnsetTiltData.h"
#include "../AuraDecayGainData.h"

#include <juce_core/juce_core.h>

class AuraParameterMapTests : public juce::UnitTest
{
public:
    AuraParameterMapTests() : juce::UnitTest("AuraParameterMap", "Aura") {}

    void runTest() override
    {
        beginTest("mapTimeAndHighToDecayParams reproduces the calibrated decayGain points exactly at Time's measured settings, High=0");
        {
            for (const auto& point : AuraDecayGainData::timeToDecayGain)
            {
                bool extrapolated = true;
                const auto params = AuraParameterMap::mapTimeAndHighToDecayParams(point.timeSeconds, 0.0f, &extrapolated);
                expectWithinAbsoluteError(params.decayGain, point.decayGain, 1.0e-4f);
                expect(!extrapolated, "a measured Time value at High=0 should not be reported as extrapolated");
            }
        }

        beginTest("mapTimeAndHighToDecayParams: decayGain and dampingWeight increase monotonically with Time");
        {
            float previousGain = AuraParameterMap::mapTimeAndHighToDecayParams(0.1f, 0.0f).decayGain;
            float previousDamping = AuraParameterMap::mapTimeAndHighToDecayParams(0.1f, 0.0f).dampingWeight;
            for (float t = 0.3f; t <= 5.5f; t += 0.2f)
            {
                const auto params = AuraParameterMap::mapTimeAndHighToDecayParams(t, 0.0f);
                expect(params.decayGain >= previousGain - 1.0e-4f, "decayGain should not decrease as Time increases");
                expect(params.dampingWeight >= previousDamping - 1.0e-4f, "dampingWeight should not decrease as Time increases");
                previousGain = params.decayGain;
                previousDamping = params.dampingWeight;
            }
        }

        beginTest("mapTimeAndHighToDecayParams flags extrapolation outside the measured 0.1-5.5s/-8..0dB range");
        {
            bool extrapolated = false;
            AuraParameterMap::mapTimeAndHighToDecayParams(8.0f, 0.0f, &extrapolated);
            expect(extrapolated, "above the highest measured Time should be flagged as extrapolated");

            extrapolated = false;
            AuraParameterMap::mapTimeAndHighToDecayParams(2.3f, -8.0f, &extrapolated);
            expect(!extrapolated, "the measured High=-8 point should not be flagged as extrapolated");

            extrapolated = false;
            AuraParameterMap::mapTimeAndHighToDecayParams(2.3f, 3.0f, &extrapolated);
            expect(extrapolated, "above the highest measured High should be flagged as extrapolated");
        }

        beginTest("mapTimeAndHighToDecayParams stays within a sane, stable range across the full parameter grid");
        {
            for (float t = 0.1f; t <= 8.0f; t += 0.3f)
            {
                for (float h = -8.0f; h <= 0.0f; h += 1.0f)
                {
                    const auto params = AuraParameterMap::mapTimeAndHighToDecayParams(t, h);
                    expect(params.decayGain > -0.5f && params.decayGain < 1.5f,
                        "decayGain should stay in a physically sane range even when extrapolating");
                    expect(params.dampingWeight > -0.5f && params.dampingWeight < 1.5f,
                        "dampingWeight should stay in a physically sane range even when extrapolating");
                }
            }
        }

        beginTest("mapInputTiltDb reproduces the measured points (as a delta from High=0) exactly at each measured High");
        {
            const auto neutralOnsetTiltDb = AuraOnsetTiltData::highToOnsetTiltDb.back().onsetTiltDb;
            for (const auto& point : AuraOnsetTiltData::highToOnsetTiltDb)
            {
                bool extrapolated = true;
                const auto result = AuraParameterMap::mapInputTiltDb(point.highDb, &extrapolated);
                expectWithinAbsoluteError(result, point.onsetTiltDb - neutralOnsetTiltDb, 1.0e-4f);
                expect(!extrapolated, "a measured High value should not be reported as extrapolated");
            }

            expectWithinAbsoluteError(AuraParameterMap::mapInputTiltDb(0.0f), 0.0f, 1.0e-4f,
                "High=0 should map to exactly zero added coloration");
        }

        beginTest("mapInputTiltDb interpolates monotonically between measured points");
        {
            // AuraOnsetTiltData.h: onset tilt rises monotonically with High across the measured range.
            float previous = AuraParameterMap::mapInputTiltDb(-8.0f);
            for (float h = -7.5f; h <= 0.01f; h += 0.5f)
            {
                const auto current = AuraParameterMap::mapInputTiltDb(h);
                expect(current >= previous, "onset tilt should never decrease as High increases");
                previous = current;
            }
        }

        beginTest("mapInputTiltDb flags extrapolation outside the measured -8..0dB range");
        {
            bool extrapolated = false;
            AuraParameterMap::mapInputTiltDb(-12.0f, &extrapolated);
            expect(extrapolated, "below the lowest measured High should be flagged as extrapolated");

            extrapolated = false;
            AuraParameterMap::mapInputTiltDb(3.0f, &extrapolated);
            expect(extrapolated, "above the highest measured High should be flagged as extrapolated");

            extrapolated = true;
            AuraParameterMap::mapInputTiltDb(-4.0f, &extrapolated);
            expect(!extrapolated, "within the measured range should not be flagged as extrapolated");
        }
    }
};

static AuraParameterMapTests auraParameterMapTests;
