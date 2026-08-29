#include "../IntruderFDNEngine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // Runs an impulse through the engine and returns the peak-normalised abs(L)+abs(R) energy per
    // sample, for however many samples are requested.
    std::vector<float> renderImpulseEnvelope(IntruderFDNEngine& engine, int numSamples)
    {
        std::vector<float> left(static_cast<size_t>(numSamples), 0.0f);
        std::vector<float> right(static_cast<size_t>(numSamples), 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;

        engine.processStereo(left.data(), right.data(), numSamples);

        std::vector<float> envelope(static_cast<size_t>(numSamples));
        for (int i = 0; i < numSamples; ++i)
            envelope[(size_t) i] = std::abs(left[(size_t) i]) + std::abs(right[(size_t) i]);
        return envelope;
    }

    bool hasNaNOrInf(const std::vector<float>& v)
    {
        for (auto x : v)
            if (!std::isfinite(x))
                return true;
        return false;
    }

    // An impulse response is sparse/discrete early on (individual FDN round trips before the tank
    // has built up to a dense diffuse field), so a single sample index can easily land exactly
    // between arrivals and read as zero even while the response is clearly "active" there.
    // Windowed peak avoids that fragility.
    float peakInWindow(const std::vector<float>& envelope, double centerSeconds, double sampleRate, double windowSeconds)
    {
        const auto center = (int) (centerSeconds * sampleRate);
        const auto half = (int) (windowSeconds * 0.5 * sampleRate);
        const auto lo = std::max(0, center - half);
        const auto hi = std::min((int) envelope.size(), center + half);
        float peak = 0.0f;
        for (int i = lo; i < hi; ++i)
            peak = std::max(peak, envelope[(size_t) i]);
        return peak;
    }
}

class IntruderFDNEngineTests : public juce::UnitTest
{
public:
    IntruderFDNEngineTests() : juce::UnitTest("IntruderFDNEngine", "Intruder") {}

    void runTest() override
    {
        constexpr double sampleRate = 44100.0;

        beginTest("An impulse produces a non-trivial, finite, decaying tail");
        {
            IntruderFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setDecaySeconds(1.0f);
            engine.setPreDelayMs(0.0f);
            engine.setTilt(0.0f);
            engine.setSpacingMultiplier(1.0f);

            constexpr int numSamples = (int) sampleRate; // 1 second
            const auto envelope = renderImpulseEnvelope(engine, numSamples);

            expect(!hasNaNOrInf(envelope), "output must be finite everywhere");

            const auto earlyEnergy = peakInWindow(envelope, 0.06, sampleRate, 0.08);
            const auto lateEnergy = peakInWindow(envelope, 0.9, sampleRate, 0.08);
            expect(earlyEnergy > 0.0f, "should have audible early energy after the impulse");
            expect(lateEnergy < earlyEnergy, "tail should have decayed well below the initial tank buildup by 0.9s");
        }

        beginTest("Silence in produces silence out (no self-noise, no runaway)");
        {
            IntruderFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setDecaySeconds(2.0f);

            constexpr int numSamples = 4096;
            std::vector<float> left(static_cast<size_t>(numSamples), 0.0f);
            std::vector<float> right(static_cast<size_t>(numSamples), 0.0f);
            engine.processStereo(left.data(), right.data(), numSamples);

            float maxAbs = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                maxAbs = std::max({maxAbs, std::abs(left[(size_t) i]), std::abs(right[(size_t) i])});

            expect(maxAbs < 1.0e-6f, "silence in should stay silent (engine has no free-running oscillation)");
        }

        beginTest("Longer Decay setting sustains audible energy longer than a shorter one");
        {
            IntruderFDNEngine shortEngine, longEngine;
            shortEngine.prepare(sampleRate);
            longEngine.prepare(sampleRate);
            shortEngine.setDecaySeconds(0.2f);
            longEngine.setDecaySeconds(5.0f);

            constexpr int numSamples = (int) sampleRate; // 1 second
            const auto shortEnv = renderImpulseEnvelope(shortEngine, numSamples);
            const auto longEnv = renderImpulseEnvelope(longEngine, numSamples);

            const auto shortAt06 = peakInWindow(shortEnv, 0.6, sampleRate, 0.08);
            const auto longAt06 = peakInWindow(longEnv, 0.6, sampleRate, 0.08);
            expect(longAt06 > shortAt06,
                "at 0.6s, the 5s-Decay setting should be audibly louder than the 0.2s-Decay setting");
        }

        beginTest("Feedback stays stable (bounded output) at the maximum Decay setting");
        {
            IntruderFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setDecaySeconds(10.0f); // max of the exposed parameter range

            constexpr int numSamples = (int) (2.0 * sampleRate);
            const auto envelope = renderImpulseEnvelope(engine, numSamples);

            expect(!hasNaNOrInf(envelope), "output must stay finite at the longest Decay setting");
            for (auto e : envelope)
                expect(e < 100.0f, "output must stay bounded (no runaway feedback) even at max Decay");
        }

        beginTest("A second impulse after silence retriggers the envelope to near-full level");
        {
            IntruderFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setDecaySeconds(0.3f); // short, so the tail fully dies out before the retrigger

            constexpr int gapSamples = (int) (2.0 * sampleRate);
            auto firstEnvelope = renderImpulseEnvelope(engine, gapSamples);
            const auto firstPeak = *std::max_element(firstEnvelope.begin(), firstEnvelope.end());

            constexpr int secondNumSamples = 2000;
            const auto secondEnvelope = renderImpulseEnvelope(engine, secondNumSamples);
            const auto secondPeak = *std::max_element(secondEnvelope.begin(), secondEnvelope.end());

            expect(secondPeak > firstPeak * 0.5f,
                "a fresh impulse after the tail has died out should retrigger close to full level, not stay gated low");
        }
    }
};

static IntruderFDNEngineTests intruderFDNEngineTests;
