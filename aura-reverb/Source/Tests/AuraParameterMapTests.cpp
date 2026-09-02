#include "../AuraParameterMap.h"
#include "../AuraReferenceData.h"

#include <juce_core/juce_core.h>

class AuraParameterMapTests : public juce::UnitTest
{
public:
    AuraParameterMapTests() : juce::UnitTest("AuraParameterMap", "Aura") {}

    void runTest() override
    {
        beginTest("mapTimeAndHighToBandGains reproduces the measured points exactly at Time's measured settings, High=0");
        {
            for (const auto& point : wildjag::dsp::time_to_high_band_gainPoints)
            {
                bool extrapolated = true;
                const auto gains = AuraParameterMap::mapTimeAndHighToBandGains(point.x, 0.0f, &extrapolated);
                expectWithinAbsoluteError(gains.highBandGain, point.y, 1.0e-4f);
                expect(!extrapolated, "a measured Time value at High=0 should not be reported as extrapolated");
            }
        }

        beginTest("mapTimeAndHighToBandGains: highBandGain and lowBandGain increase monotonically with Time");
        {
            float previousHigh = AuraParameterMap::mapTimeAndHighToBandGains(0.1f, 0.0f).highBandGain;
            float previousLow = AuraParameterMap::mapTimeAndHighToBandGains(0.1f, 0.0f).lowBandGain;
            for (float t = 0.3f; t <= 5.5f; t += 0.2f)
            {
                const auto gains = AuraParameterMap::mapTimeAndHighToBandGains(t, 0.0f);
                expect(gains.highBandGain >= previousHigh - 1.0e-4f, "highBandGain should not decrease as Time increases");
                expect(gains.lowBandGain >= previousLow - 1.0e-4f, "lowBandGain should not decrease as Time increases");
                previousHigh = gains.highBandGain;
                previousLow = gains.lowBandGain;
            }
        }

        beginTest("mapTimeAndHighToBandGains flags extrapolation outside the measured 0.1-5.5s/-8..0dB range");
        {
            bool extrapolated = false;
            AuraParameterMap::mapTimeAndHighToBandGains(8.0f, 0.0f, &extrapolated);
            expect(extrapolated, "above the highest measured Time should be flagged as extrapolated");

            extrapolated = false;
            AuraParameterMap::mapTimeAndHighToBandGains(2.3f, -8.0f, &extrapolated);
            expect(!extrapolated, "the measured High=-8 point should not be flagged as extrapolated");

            extrapolated = false;
            AuraParameterMap::mapTimeAndHighToBandGains(2.3f, 3.0f, &extrapolated);
            expect(extrapolated, "above the highest measured High should be flagged as extrapolated");
        }

        beginTest("mapTimeAndHighToBandGains stays within a sane, stable range across the full parameter grid");
        {
            for (float t = 0.1f; t <= 8.0f; t += 0.3f)
            {
                for (float h = -8.0f; h <= 0.0f; h += 1.0f)
                {
                    const auto gains = AuraParameterMap::mapTimeAndHighToBandGains(t, h);
                    expect(gains.highBandGain > -0.5f && gains.highBandGain < 1.5f,
                        "highBandGain should stay in a physically sane range even when extrapolating");
                    expect(gains.lowBandGain > -0.5f && gains.lowBandGain < 1.5f,
                        "lowBandGain should stay in a physically sane range even when extrapolating");
                    expect(gains.dampingWeight > -0.5f && gains.dampingWeight < 1.5f,
                        "dampingWeight should stay in a physically sane range even when extrapolating");
                }
            }
        }
    }
};

static AuraParameterMapTests auraParameterMapTests;
