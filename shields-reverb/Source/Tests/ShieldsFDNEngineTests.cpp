#include "../ShieldsFDNEngine.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <limits>
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

        // The cases below all guard fixes made while chasing CPU-overload dropouts and a crash in
        // Logic; each one reproduces a specific failure mode the suite above did not cover.

        beginTest("Processing before prepare() does not crash");
        {
            // The delay lines are empty vectors until prepare() allocates them, and the read/write
            // path does `% (int) buffer.size()` - an integer modulo by zero, i.e. SIGFPE, not a
            // silently wrong sample. A host is not supposed to do this, but it must not take the
            // whole application down if it does.
            ShieldsFDNEngine engine;
            std::vector<float> left(256, 0.0f), right(256, 0.0f);
            engine.processStereo(left.data(), right.data(), (int) left.size());

            for (auto s : left) expect(std::isfinite(s));
            for (auto s : right) expect(std::isfinite(s));
        }

        beginTest("A non-finite input sample does not permanently wedge the network");
        {
            // Every line feeds every other through the Hadamard mix, so one NaN entering the tank
            // used to poison all eight lines within a sample and stay there forever - the instance
            // went silent and could only be recovered by deleting and re-creating it.
            ShieldsFDNEngine engine;
            engine.prepare(testSampleRate);
            engine.setFeedback(0.9f);

            std::vector<float> left(512, 0.0f), right(512, 0.0f);
            left[10] = std::numeric_limits<float>::quiet_NaN();
            right[10] = std::numeric_limits<float>::infinity();
            engine.processStereo(left.data(), right.data(), (int) left.size());

            // Feed real audio afterwards and require the engine to still respond to it.
            std::vector<float> l2(4096, 0.0f), r2(4096, 0.0f);
            l2[0] = 1.0f;
            r2[0] = 1.0f;
            engine.processStereo(l2.data(), r2.data(), (int) l2.size());

            float energy = 0.0f;
            for (size_t i = 0; i < l2.size(); ++i)
            {
                expect(std::isfinite(l2[i]), "output must be finite after a NaN input");
                expect(std::isfinite(r2[i]), "output must be finite after an Inf input");
                energy += std::abs(l2[i]);
            }
            expect(energy > 0.0f, "engine must still pass audio after a non-finite input");
        }

        beginTest("Maximum damping still decays rather than killing the tail");
        {
            // At damping == 1.0 the one-pole update term is multiplied by zero, so dampingState
            // froze at its reset value of 0 and every line fed silence back into the tank: the
            // reverb died entirely a fraction of a second in. See setDamping()'s comment.
            ShieldsFDNEngine engine;
            engine.prepare(testSampleRate);
            engine.setFeedback(0.9f);
            engine.setDamping(1.0f);

            // Must look well past the initial burst: the burst bank injects into the tank directly,
            // so even with the damping filter dead there is still energy for roughly the first two
            // seconds. The broken build measured EXACTLY 0.0 from ~2s onward, so sample the tail
            // from 3s to 5s where only the (damped) recirculating tank can contribute.
            const auto numSamples = (int) (testSampleRate * 5.0);
            const auto out = renderImpulseLeft(engine, numSamples);

            float lateEnergy = 0.0f;
            for (size_t i = (size_t) (testSampleRate * 3.0); i < out.size(); ++i)
            {
                expect(std::isfinite(out[i]));
                lateEnergy += std::abs(out[i]);
            }
            expect(lateEnergy > 0.0f, "max damping must still produce a decaying tail, not silence");
        }

        beginTest("A Size change mid-stream is applied even if it lands during a crossfade");
        {
            // updateLineLengths()/updateBurstLines() are now skipped while sizeMultiplier is
            // unchanged, which is what removed ~half the DSP cost - but a length change that
            // arrives while a previous crossfade is still running is DEFERRED by design, so the
            // skip has to keep retrying until it lands (see lengthUpdateDeferred). Without that,
            // a deferred change would be dropped forever once the glide settled.
            ShieldsFDNEngine engine;
            engine.prepare(testSampleRate);
            engine.setFeedback(0.85f);
            engine.setSize(1.0f);

            std::vector<float> left(1024, 0.0f), right(1024, 0.0f);
            left[0] = 1.0f;
            engine.processStereo(left.data(), right.data(), (int) left.size());

            // Move Size repeatedly in quick succession so later changes land mid-crossfade.
            for (float size : { 2.0f, 3.5f, 1.25f, 4.0f })
            {
                engine.setSize(size);
                std::vector<float> l(256, 0.0f), r(256, 0.0f);
                engine.processStereo(l.data(), r.data(), (int) l.size());
            }

            // Let the glide fully settle, then confirm the engine is still live and finite.
            std::vector<float> l((size_t) (testSampleRate * 1.0), 0.0f);
            std::vector<float> r(l.size(), 0.0f);
            l[0] = 1.0f;
            r[0] = 1.0f;
            engine.processStereo(l.data(), r.data(), (int) l.size());

            float energy = 0.0f;
            for (size_t i = 0; i < l.size(); ++i)
            {
                expect(std::isfinite(l[i]), "output must stay finite across rapid Size changes");
                energy += std::abs(l[i]);
            }
            expect(energy > 0.0f, "engine must still pass audio after rapid Size changes");
        }

        beginTest("Varying block sizes produce the same samples as one continuous block");
        {
            // Guards the processor-side change that stopped resizing the wet scratch buffer inside
            // processBlock(). Splitting the same input across differently-sized calls must not
            // alter a single sample - the engine carries all its state across calls.
            const int total = 8192;

            std::vector<float> refL((size_t) total, 0.0f), refR((size_t) total, 0.0f);
            refL[0] = 1.0f;
            refR[0] = 1.0f;
            {
                ShieldsFDNEngine engine;
                engine.prepare(testSampleRate);
                engine.processStereo(refL.data(), refR.data(), total);
            }

            std::vector<float> chunkedL((size_t) total, 0.0f), chunkedR((size_t) total, 0.0f);
            chunkedL[0] = 1.0f;
            chunkedR[0] = 1.0f;
            {
                ShieldsFDNEngine engine;
                engine.prepare(testSampleRate);

                int pos = 0;
                for (int blockSize : { 1, 7, 64, 1, 512, 333, 2048, 4096 })
                {
                    const auto n = std::min(blockSize, total - pos);
                    if (n <= 0) break;
                    engine.processStereo(chunkedL.data() + pos, chunkedR.data() + pos, n);
                    pos += n;
                }
                if (pos < total)
                    engine.processStereo(chunkedL.data() + pos, chunkedR.data() + pos, total - pos);
            }

            for (size_t i = 0; i < refL.size(); ++i)
            {
                expectWithinAbsoluteError(chunkedL[i], refL[i], 0.0f);
                expectWithinAbsoluteError(chunkedR[i], refR[i], 0.0f);
            }
        }
    }
};

static ShieldsFDNEngineTests shieldsFDNEngineTests;
