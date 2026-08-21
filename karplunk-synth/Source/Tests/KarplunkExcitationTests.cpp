#include "../KarplunkExcitation.h"

#include <juce_core/juce_core.h>

#include <cmath>

class KarplunkExcitationTests : public juce::UnitTest
{
public:
    KarplunkExcitationTests() : juce::UnitTest("NoiseBurstExcitation", "Karplunk") {}

    void runTest() override
    {
        beginTest("Output never exceeds the requested velocity (noise is bounded to [-1, 1])");
        {
            NoiseBurstExcitation excitation;
            excitation.prepare(44100.0);
            excitation.setBrightness(1.0f);

            std::vector<float> out(2000, 0.0f);
            excitation.generate(out.data(), (int) out.size(), 0.7f);

            for (auto sample : out)
                expect(std::abs(sample) <= 0.7f + 1.0e-4f, "sample amplitude should stay within velocity bound");
        }

        beginTest("Velocity scales output linearly for an identical noise sequence");
        {
            NoiseBurstExcitation excitationFull;
            NoiseBurstExcitation excitationHalf;
            excitationFull.prepare(44100.0);
            excitationHalf.prepare(44100.0);
            // Both instances start with the same default RNG seed, so with brightness left at
            // its default (1.0) on both, the underlying noise sequence is identical - only
            // velocity differs.

            std::vector<float> outFull(500, 0.0f);
            std::vector<float> outHalf(500, 0.0f);
            excitationFull.generate(outFull.data(), (int) outFull.size(), 1.0f);
            excitationHalf.generate(outHalf.data(), (int) outHalf.size(), 0.5f);

            for (size_t i = 0; i < outFull.size(); ++i)
                expectWithinAbsoluteError(outHalf[(size_t) i], outFull[(size_t) i] * 0.5f, 1.0e-5f);
        }

        beginTest("Lower brightness measurably reduces RMS energy versus full brightness");
        {
            NoiseBurstExcitation darkExcitation;
            NoiseBurstExcitation brightExcitation;
            darkExcitation.prepare(44100.0);
            brightExcitation.prepare(44100.0);
            darkExcitation.setBrightness(0.0f);
            brightExcitation.setBrightness(1.0f);

            constexpr int numSamples = 2000;
            std::vector<float> darkOut((size_t) numSamples, 0.0f);
            std::vector<float> brightOut((size_t) numSamples, 0.0f);
            darkExcitation.generate(darkOut.data(), numSamples, 1.0f);
            brightExcitation.generate(brightOut.data(), numSamples, 1.0f);

            auto rms = [](const std::vector<float>& buf)
            {
                double sumSquares = 0.0;
                for (auto sample : buf)
                    sumSquares += (double) sample * (double) sample;
                return std::sqrt(sumSquares / (double) buf.size());
            };

            expect(rms(darkOut) < rms(brightOut), "brightness = 0 should have lower RMS than brightness = 1");
        }

        beginTest("reset() clears filter state so the next generate() starts from silence-biased output");
        {
            NoiseBurstExcitation excitation;
            excitation.prepare(44100.0);
            excitation.setBrightness(0.05f); // heavy smoothing, so state carries over visibly if not reset

            std::vector<float> warmup(1000, 0.0f);
            excitation.generate(warmup.data(), (int) warmup.size(), 1.0f);

            excitation.reset();

            float firstSampleAfterReset = 0.0f;
            excitation.generate(&firstSampleAfterReset, 1, 1.0f);

            // At heavy smoothing (alpha near the minimum), one sample can't move far from a
            // reset (zero) starting state.
            expect(std::abs(firstSampleAfterReset) < 0.1f, "first sample after reset should start near zero at low brightness");
        }
    }
};

static KarplunkExcitationTests karplunkExcitationTests;
