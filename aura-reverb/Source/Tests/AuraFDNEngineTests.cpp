#include "../AuraFDNEngine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    std::vector<float> renderImpulseEnvelope(AuraFDNEngine& engine, int numSamples)
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
    // has built up to a dense diffuse field - see ml-toolkit/effects/ambience/findings.md's
    // discussion of short-Time captures), so a single sample index can easily land exactly
    // between arrivals and read as zero even while the response is clearly "active" there.
    // Windowed peak avoids that fragility - same idiom as IntruderFDNEngineTests.
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

class AuraFDNEngineTests : public juce::UnitTest
{
public:
    AuraFDNEngineTests() : juce::UnitTest("AuraFDNEngine", "Aura") {}

    void runTest() override
    {
        constexpr double sampleRate = 44100.0;

        beginTest("An impulse produces a non-trivial, finite, decaying tail");
        {
            AuraFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setBandGains(0.9f, 0.85f);
            engine.setDampingWeight(0.3f);
            engine.setPreDelayMs(0.0f);

            constexpr int numSamples = (int) (2.0 * sampleRate);
            const auto envelope = renderImpulseEnvelope(engine, numSamples);

            expect(!hasNaNOrInf(envelope), "output must be finite everywhere");

            const auto earlyEnergy = peakInWindow(envelope, 0.05, sampleRate, 0.08);
            const auto lateEnergy = peakInWindow(envelope, 1.8, sampleRate, 0.08);
            expect(earlyEnergy > 0.0f, "should have audible early energy after the impulse");
            expect(lateEnergy < earlyEnergy, "tail should have decayed well below the initial tank buildup by 1.8s");
        }

        beginTest("Silence in produces silence out (no self-noise, no runaway)");
        {
            AuraFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setBandGains(0.95f, 0.9f);
            engine.setDampingWeight(0.2f);

            constexpr int numSamples = 4096;
            std::vector<float> left(static_cast<size_t>(numSamples), 0.0f);
            std::vector<float> right(static_cast<size_t>(numSamples), 0.0f);
            engine.processStereo(left.data(), right.data(), numSamples);

            float maxAbs = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                maxAbs = std::max({maxAbs, std::abs(left[(size_t) i]), std::abs(right[(size_t) i])});

            expect(maxAbs < 1.0e-6f, "silence in should stay silent (engine has no free-running oscillation)");
        }

        beginTest("Higher band gains sustain audible energy longer than lower ones");
        {
            AuraFDNEngine lowGainEngine, highGainEngine;
            lowGainEngine.prepare(sampleRate);
            highGainEngine.prepare(sampleRate);
            lowGainEngine.setBandGains(0.5f, 0.4f);
            lowGainEngine.setDampingWeight(0.5f);
            highGainEngine.setBandGains(0.97f, 0.95f);
            highGainEngine.setDampingWeight(0.2f);

            constexpr int numSamples = (int) sampleRate; // 1 second
            const auto lowEnv = renderImpulseEnvelope(lowGainEngine, numSamples);
            const auto highEnv = renderImpulseEnvelope(highGainEngine, numSamples);

            const auto lowAt06 = peakInWindow(lowEnv, 0.6, sampleRate, 0.08);
            const auto highAt06 = peakInWindow(highEnv, 0.6, sampleRate, 0.08);
            expect(highAt06 > lowAt06,
                "at 0.6s, the higher-gain setting should be audibly louder than the lower-gain setting");
        }

        beginTest("Feedback stays stable (bounded output) even at the maximum gain ceiling");
        {
            AuraFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setBandGains(1.0f, 1.0f); // above the 0.985 ceiling - must be clamped internally
            engine.setDampingWeight(1.0f); // above the 0.99 ceiling - must be clamped internally

            constexpr int numSamples = (int) (3.0 * sampleRate);
            const auto envelope = renderImpulseEnvelope(engine, numSamples);

            expect(!hasNaNOrInf(envelope), "output must stay finite even when setters are called with out-of-range values");
            for (auto e : envelope)
                expect(e < 100.0f, "output must stay bounded (no runaway feedback) even at the gain ceiling");
        }

        beginTest("Pre-delay pushes the first tank arrival later without changing its shape");
        {
            AuraFDNEngine noPreDelay, withPreDelay;
            noPreDelay.prepare(sampleRate);
            withPreDelay.prepare(sampleRate);
            noPreDelay.setBandGains(0.8f, 0.7f);
            noPreDelay.setDampingWeight(0.3f);
            withPreDelay.setBandGains(0.8f, 0.7f);
            withPreDelay.setDampingWeight(0.3f);
            withPreDelay.setPreDelayMs(50.0f);

            constexpr int numSamples = 8000;
            const auto noPreDelayEnv = renderImpulseEnvelope(noPreDelay, numSamples);
            const auto withPreDelayEnv = renderImpulseEnvelope(withPreDelay, numSamples);

            // Even with zero pre-delay, the tank's own shortest line (~30ms) means there is no
            // output at all before that - checked directly against a per-sample scan before
            // writing this test, not assumed. The window below (25-40ms) is chosen to land on
            // that first arrival for the no-pre-delay case, where the 50ms-pre-delay case should
            // still be silent (its own first arrival isn't until ~80ms).
            const auto firstArrivalNoPreDelay = peakInWindow(noPreDelayEnv, 0.0325, sampleRate, 0.015);
            const auto sameWindowWithPreDelay = peakInWindow(withPreDelayEnv, 0.0325, sampleRate, 0.015);
            expect(firstArrivalNoPreDelay > 0.0f, "the no-pre-delay case should have real energy at its first (~30ms) tank arrival");
            expect(sameWindowWithPreDelay < firstArrivalNoPreDelay * 0.01f,
                "at 50ms pre-delay, the same ~30ms window should still be essentially silent (first arrival not until ~80ms)");
        }

        beginTest("Input tilt affects onset content, not just the tail (regression test for the Phase D bug)");
        {
            // Real bug found via Phase D validation: feedbackShelf alone produced ZERO onset-tilt
            // difference between settings (measured identical on a real render) because it only
            // touches the signal after a feedback round trip, while the dry input is injected
            // unshelved on its first pass. This locks in that setInputTilt() actually reaches the
            // very first tank arrival - see AuraFDNEngine.h's class comment for the full story.
            AuraFDNEngine neutralTilt, darkTilt;
            neutralTilt.prepare(sampleRate);
            darkTilt.prepare(sampleRate);
            neutralTilt.setBandGains(0.8f, 0.7f);
            neutralTilt.setDampingWeight(0.3f);
            neutralTilt.setInputTilt(0.0f);
            darkTilt.setBandGains(0.8f, 0.7f);
            darkTilt.setDampingWeight(0.3f);
            darkTilt.setInputTilt(-8.0f); // bass up / treble down

            constexpr int numSamples = 4000;
            std::vector<float> neutralL(numSamples, 0.0f), neutralR(numSamples, 0.0f);
            std::vector<float> darkL(numSamples, 0.0f), darkR(numSamples, 0.0f);
            neutralL[0] = 1.0f; neutralR[0] = 1.0f;
            darkL[0] = 1.0f; darkR[0] = 1.0f;
            neutralTilt.processStereo(neutralL.data(), neutralR.data(), numSamples);
            darkTilt.processStereo(darkL.data(), darkR.data(), numSamples);

            // Crude one-pole 2kHz split of the first arrival window, just to confirm the two
            // renders' bass/treble balance genuinely differs at onset - doesn't need to be exact,
            // only needs to detect "some difference" vs the pre-fix "exactly zero difference".
            // Starts at ~30ms (the shortest line's own delay - see the pre-delay test above for
            // why there is no real tank output at all before that), not sample 0 - an earlier
            // version of this test measured mostly pre-arrival silence and failed for that
            // reason, not because the fix didn't work.
            auto highToLowEnergyRatio = [sampleRate](const std::vector<float>& x)
            {
                double loE = 0.0, hiE = 0.0;
                float lp = 0.0f;
                const auto w = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 2000.0f / (float) sampleRate);
                const auto start = (int) (0.029 * sampleRate);
                const auto win = (int) (0.02 * sampleRate);
                for (int i = start; i < start + win && i < (int) x.size(); ++i)
                {
                    lp += w * (x[(size_t) i] - lp);
                    const auto hi = x[(size_t) i] - lp;
                    loE += (double) lp * lp;
                    hiE += (double) hi * hi;
                }
                return hiE / (loE + 1.0e-20);
            };

            const auto neutralRatio = highToLowEnergyRatio(neutralL);
            const auto darkRatio = highToLowEnergyRatio(darkL);
            expect(darkRatio < neutralRatio * 0.9,
                "a -8dB input tilt should measurably darken the very first tank arrival, not just the tail");
        }
    }
};

static AuraFDNEngineTests auraFDNEngineTests;
