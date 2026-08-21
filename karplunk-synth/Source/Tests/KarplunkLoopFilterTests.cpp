#include "../KarplunkLoopFilter.h"

#include <juce_core/juce_core.h>

#include <cmath>

class KarplunkLoopFilterTests : public juce::UnitTest
{
public:
    KarplunkLoopFilterTests() : juce::UnitTest("TwoPointAverageLoopFilter", "Karplunk") {}

    void runTest() override
    {
        beginTest("Impulse response matches the classic two-point average, scaled by loop gain");
        {
            TwoPointAverageLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f); // maxLoopGain = 0.9995

            constexpr float expectedGain = 0.9995f;
            const auto y0 = filter.processSample(1.0f);
            const auto y1 = filter.processSample(0.0f);
            const auto y2 = filter.processSample(0.0f);

            // y[n] = g * 0.5 * (x[n] + x[n-1]): y0 = g*0.5*(1+0), y1 = g*0.5*(0+1), y2 = g*0.5*(0+0)
            expectWithinAbsoluteError(y0, expectedGain * 0.5f, 1.0e-5f);
            expectWithinAbsoluteError(y1, expectedGain * 0.5f, 1.0e-5f);
            expectWithinAbsoluteError(y2, 0.0f, 1.0e-5f);
        }

        beginTest("setDamping(0) and setDamping(1) hit the documented gain floor/ceiling");
        {
            TwoPointAverageLoopFilter minFilter;
            minFilter.prepare(44100.0);
            minFilter.setDamping(0.0f);
            const auto minY = minFilter.processSample(1.0f);
            expectWithinAbsoluteError(minY, 0.90f * 0.5f, 1.0e-5f);

            TwoPointAverageLoopFilter maxFilter;
            maxFilter.prepare(44100.0);
            maxFilter.setDamping(1.0f);
            const auto maxY = maxFilter.processSample(1.0f);
            expectWithinAbsoluteError(maxY, 0.9995f * 0.5f, 1.0e-5f);
        }

        beginTest("A sustained unit input settles to a stable output, never grows unbounded");
        {
            TwoPointAverageLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f); // longest sustain - the highest-risk setting for runaway growth

            float lastOutput = 0.0f;
            for (int i = 0; i < 10000; ++i)
            {
                lastOutput = filter.processSample(1.0f);
                expect(std::abs(lastOutput) <= 1.0f, "output should never exceed the input's own magnitude for a one-zero averaging filter with gain < 1");
            }
        }

        beginTest("reset() clears history");
        {
            TwoPointAverageLoopFilter filter;
            filter.prepare(44100.0);
            filter.setDamping(1.0f);
            filter.processSample(1.0f);
            filter.reset();
            const auto y = filter.processSample(0.0f);
            expectWithinAbsoluteError(y, 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkLoopFilterTests karplunkLoopFilterTests;
