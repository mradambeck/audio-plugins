#include "../KarplunkExcitation.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

class KarplunkExcitationTests : public juce::UnitTest
{
public:
    KarplunkExcitationTests() : juce::UnitTest("NoiseExcitation", "Karplunk") {}

    void runTest() override
    {
        beginTest("Output never exceeds the requested velocity, at any bowAmount");
        {
            for (float bowAmount : { 0.0f, 1.0f })
            {
                NoiseExcitation excitation;
                excitation.prepare(44100.0);
                excitation.setBrightness(1.0f);
                excitation.setBowAmount(bowAmount);
                excitation.setBaseDuration(500);

                for (int i = 0; i < 2000; ++i)
                {
                    const auto sample = excitation.nextExcitationSample(0.7f);
                    expect(std::abs(sample) <= 0.7f + 1.0e-4f, "sample amplitude should stay within velocity bound");
                }
            }
        }

        beginTest("Velocity scales output linearly for an identical noise sequence");
        {
            NoiseExcitation excitationFull;
            NoiseExcitation excitationHalf;
            excitationFull.prepare(44100.0);
            excitationHalf.prepare(44100.0);
            excitationFull.setBaseDuration(500);
            excitationHalf.setBaseDuration(500);
            // Both instances start with the same default RNG seed and the same (default,
            // bowAmount = 0) envelope path, so the envelope shape and noise sequence are
            // identical between the two - only velocity differs, and velocity is a pure output
            // multiplier that never feeds back into the envelope or RNG state.

            for (int i = 0; i < 500; ++i)
            {
                const auto full = excitationFull.nextExcitationSample(1.0f);
                const auto half = excitationHalf.nextExcitationSample(0.5f);
                expectWithinAbsoluteError(half, full * 0.5f, 1.0e-5f);
            }
        }

        beginTest("Lower brightness measurably reduces RMS energy versus full brightness");
        {
            NoiseExcitation darkExcitation;
            NoiseExcitation brightExcitation;
            darkExcitation.prepare(44100.0);
            brightExcitation.prepare(44100.0);
            darkExcitation.setBaseDuration(2000);
            brightExcitation.setBaseDuration(2000);
            darkExcitation.setBrightness(0.0f);
            brightExcitation.setBrightness(1.0f);

            constexpr int numSamples = 2000;
            auto rms = [](NoiseExcitation& excitation)
            {
                double sumSquares = 0.0;
                for (int i = 0; i < numSamples; ++i)
                {
                    const auto sample = excitation.nextExcitationSample(1.0f);
                    sumSquares += (double) sample * (double) sample;
                }
                return std::sqrt(sumSquares / (double) numSamples);
            };

            expect(rms(darkExcitation) < rms(brightExcitation), "brightness = 0 should have lower RMS than brightness = 1");
        }

        beginTest("reset() clears envelope and filter state so the next sample after reset starts near zero");
        {
            NoiseExcitation excitation;
            excitation.prepare(44100.0);
            excitation.setBrightness(0.05f); // heavy smoothing, so state carries over visibly if not reset
            excitation.setBaseDuration(1000);

            for (int i = 0; i < 1000; ++i)
                excitation.nextExcitationSample(1.0f);

            excitation.reset();

            // attackEnv restarts at 0 (rises from there) and lowpassState restarts at 0 too, so
            // the very first sample after reset can't be far from zero regardless of how long the
            // pre-reset run had been going.
            const auto firstSampleAfterReset = excitation.nextExcitationSample(1.0f);
            expect(std::abs(firstSampleAfterReset) < 0.1f, "first sample after reset should start near zero");
        }

        beginTest("Attack is slower and decay lasts longer as bowAmount approaches 1, for the same note");
        {
            // Raw white noise (brightness = 1, no lowpass smoothing) so the envelope shape is
            // directly reflected in the average |sample| over a window - a proxy for the
            // envelope's own magnitude at that point in time, since the noise itself is unbiased.
            auto averageAbs = [](NoiseExcitation& excitation, int numSamples)
            {
                double sum = 0.0;
                for (int i = 0; i < numSamples; ++i)
                    sum += std::abs(excitation.nextExcitationSample(1.0f));
                return sum / (double) numSamples;
            };
            auto renderAndDiscard = [](NoiseExcitation& excitation, int numSamples)
            {
                for (int i = 0; i < numSamples; ++i)
                    excitation.nextExcitationSample(1.0f);
            };

            constexpr int baseDuration = 200; // pluck decay time constant = 400 samples (durationMultiplier = 2)

            NoiseExcitation pluck;
            pluck.prepare(44100.0);
            pluck.setBrightness(1.0f);
            pluck.setBowAmount(0.0f);
            pluck.setBaseDuration(baseDuration);

            NoiseExcitation bow;
            bow.prepare(44100.0);
            bow.setBrightness(1.0f);
            bow.setBowAmount(1.0f);
            bow.setBaseDuration(baseDuration);

            const auto pluckEarly = averageAbs(pluck, 20);
            const auto bowEarly = averageAbs(bow, 20);
            expect(bowEarly < pluckEarly, "a fully-bowed note's attack should still be rising while a plucked note's has already peaked");

            // Skip ahead well past the pluck's own decay time constant (400 samples) before
            // measuring "late" - the bow's decay time constant is on the order of a million
            // samples (30s at 44.1kHz), so it should barely have moved by comparison.
            renderAndDiscard(pluck, 1200);
            renderAndDiscard(bow, 1200);

            const auto pluckLate = averageAbs(pluck, 50);
            const auto bowLate = averageAbs(bow, 50);
            expect(pluckLate < 0.1 * pluckEarly, "a plucked note's decay should have mostly died out by this point");
            expect(bowLate > 2.0 * pluckLate, "a fully-bowed note should retain meaningfully more energy than an already-decayed plucked note at the same elapsed time");
        }
    }
};

static KarplunkExcitationTests karplunkExcitationTests;
