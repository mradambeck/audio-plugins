#include "../ShieldsFDNEngine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr double testSampleRate = 44100.0;

    // Runs an impulse through a freshly-prepared engine and returns the wet stereo output.
    std::vector<float> renderImpulseLeft(ShieldsFDNEngine& engine, int numSamples)
    {
        std::vector<float> left((size_t) numSamples, 0.0f), right((size_t) numSamples, 0.0f);
        left[0] = 1.0f;
        engine.processStereo(left.data(), right.data(), numSamples);
        return left;
    }
}

// Correctness checks a by-ear-only pass can't catch: whether the network stays bounded at a high
// feedback setting (a stability bug here would only show up as an unpleasant surprise days into
// tuning), and whether the impulse response's echo density actually grows over time rather than
// being loudest immediately - the latter is Shields's entire reason for existing, so it's worth an
// automated regression check independent of the offline IR-comparison harness in tools/.
class ShieldsFDNEngineTests : public juce::UnitTest
{
public:
    ShieldsFDNEngineTests() : juce::UnitTest("ShieldsFDNEngine", "Shields") {}

    void runTest() override
    {
        beginTest("Silence in produces silence out once primed with default parameters");
        {
            ShieldsFDNEngine engine;
            engine.prepare(testSampleRate);

            std::vector<float> left(512, 0.0f), right(512, 0.0f);
            engine.processStereo(left.data(), right.data(), (int) left.size());

            for (auto s : left) expectWithinAbsoluteError(s, 0.0f, 1.0e-6f);
            for (auto s : right) expectWithinAbsoluteError(s, 0.0f, 1.0e-6f);
        }

        beginTest("A high-feedback impulse response stays bounded (no runaway growth)");
        {
            ShieldsFDNEngine engine;
            engine.prepare(testSampleRate);
            engine.setDiffusion(0.5f);
            engine.setFeedback(1.0f); // max - internally clamped below unity gain
            engine.setDamping(0.0f);  // no damping - the least forgiving case for stability
            engine.setSize(1.0f);

            const auto numSamples = (int) (testSampleRate * 4.0); // 4 seconds
            const auto out = renderImpulseLeft(engine, numSamples);

            for (auto s : out)
            {
                expect(std::isfinite(s));
                expect(std::abs(s) < 10.0f);
            }
        }

        beginTest("Echo density rises over the buildup window - the 'swell' itself");
        {
            // Per the spec, the actual signature of Bloom's buildup is echo DENSITY over time
            // (count of distinguishable reflections per window), not raw loudness contour - an
            // 8-line FDN's total energy is provably front-loaded at the moment of injection (an
            // orthogonal cross-mix scaled by a single feedback gain can only ever lose energy round
            // over round, confirmed empirically during tuning), so a handful of early, sparse,
            // full-amplitude first-order arrivals will always outweigh the much larger number of
            // much smaller later reflections in raw RMS terms. What genuinely does grow over time
            // is the COUNT of resolvable peaks per window, since decorrelated-length lines only
            // start compounding into a dense, noise-like texture after several round trips - this
            // is what tools/compare_irs.py will also measure against the real reference IRs.
            ShieldsFDNEngine engine;
            engine.prepare(testSampleRate);
            engine.setDiffusion(0.5f);
            engine.setFeedback(0.9f);
            engine.setDamping(0.3f);
            engine.setSize(2.0f);

            const auto numSamples = (int) (testSampleRate * 1.0);
            const auto out = renderImpulseLeft(engine, numSamples);

            constexpr int numWindows = 10;
            const auto windowSize = numSamples / numWindows;

            auto countPeaksInWindow = [&out, windowSize](int windowIndex)
            {
                const auto start = windowIndex * windowSize;
                float windowPeak = 0.0f;
                for (int i = 0; i < windowSize; ++i)
                    windowPeak = std::max(windowPeak, std::abs(out[(size_t) (start + i)]));

                if (windowPeak <= 0.0f)
                    return 0;

                const auto threshold = windowPeak * 0.2f;
                int count = 0;
                for (int i = 1; i < windowSize - 1; ++i)
                {
                    const auto a = std::abs(out[(size_t) (start + i)]);
                    if (a >= threshold
                        && a >= std::abs(out[(size_t) (start + i - 1)])
                        && a >= std::abs(out[(size_t) (start + i + 1)]))
                        ++count;
                }
                return count;
            };

            const auto earlyDensity = countPeaksInWindow(0);
            const auto midDensity = countPeaksInWindow(numWindows / 2);

            // By the buildup's midpoint, several generations of reflections have interleaved into
            // a measurably denser texture than the first window's sparser, more discrete arrivals.
            expect(midDensity > earlyDensity * 2,
                "expected echo density to at least double by the buildup's midpoint");
        }
    }
};

static ShieldsFDNEngineTests shieldsFDNEngineTests;
