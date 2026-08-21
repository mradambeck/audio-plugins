#include "../KarplunkStringLine.h"

#include <juce_core/juce_core.h>

#include <cmath>

// The Delay Tuning seam's ring-buffer correctness. read() is deliberately a pure function of the
// current write position (see KarplunkStringLine.h's class comment for why this shape was chosen
// over wrapping juce::dsp::DelayLine - its popSample() is a strictly causal, state-consuming
// read, which cannot support seeding a burst of samples before any of them are read back, exactly
// what Karplus-Strong's noteOn priming needs). Found empirically while building this test.
class KarplunkStringLineTests : public juce::UnitTest
{
public:
    KarplunkStringLineTests() : juce::UnitTest("KarplunkStringLine", "Karplunk") {}

    void runTest() override
    {
        beginTest("Zero delay returns the most recently written sample, even repeated reads");
        {
            KarplunkStringLine<> line;
            line.prepare(44100.0, 64);
            line.setDelaySamples(0.0f);

            line.write(5.0f);
            expectWithinAbsoluteError(line.read(), 5.0f, 1.0e-6f);
            expectWithinAbsoluteError(line.read(), 5.0f, 1.0e-6f); // repeated read, no side effects

            line.write(9.0f);
            expectWithinAbsoluteError(line.read(), 9.0f, 1.0e-6f);
        }

        beginTest("An integer delay reproduces a single written impulse exactly N ticks later");
        {
            KarplunkStringLine<> line; // default Linear interpolation
            constexpr int capacity = 256; // meaningfully larger than the delay under test
            constexpr float delaySamples = 16.0f;
            line.prepare(44100.0, capacity);
            line.setDelaySamples(delaySamples);

            line.write(1.0f); // a single impulse, nothing else written yet

            int impulseTick = -1;
            float impulsePeak = 0.0f;
            for (int i = 0; i < 64; ++i)
            {
                const auto sample = line.read();
                line.write(0.0f);
                if (std::abs(sample) > impulsePeak)
                {
                    impulsePeak = std::abs(sample);
                    impulseTick = i;
                }
            }

            expect(impulsePeak > 0.9f, "the impulse should reappear near full amplitude");
            expectWithinAbsoluteError((float) impulseTick, delaySamples, 1.0f);
        }

        beginTest("Doubling the delay roughly doubles the tick at which the impulse reappears");
        {
            auto findImpulseTick = [](float delaySamples) -> int
            {
                KarplunkStringLine<> line;
                line.prepare(44100.0, 256);
                line.setDelaySamples(delaySamples);
                line.write(1.0f);

                int impulseTick = -1;
                float impulsePeak = 0.0f;
                for (int i = 0; i < 64; ++i)
                {
                    const auto sample = line.read();
                    line.write(0.0f);
                    if (std::abs(sample) > impulsePeak)
                    {
                        impulsePeak = std::abs(sample);
                        impulseTick = i;
                    }
                }
                return impulseTick;
            };

            const auto shortTick = findImpulseTick(10.0f);
            const auto longTick = findImpulseTick(20.0f);
            expect(longTick > shortTick, "a longer delay should reproduce the impulse later");
            expectWithinAbsoluteError((float) (longTick - shortTick), 10.0f, 1.0f);
        }

        beginTest("reset() clears history to silence");
        {
            KarplunkStringLine<> line;
            line.prepare(44100.0, 32);
            line.setDelaySamples(8.0f);
            for (int i = 0; i < 32; ++i)
                line.write(1.0f);
            line.reset();
            for (int i = 0; i < 16; ++i)
                expectWithinAbsoluteError(line.read(), 0.0f, 1.0e-6f);
        }
    }
};

static KarplunkStringLineTests karplunkStringLineTests;
