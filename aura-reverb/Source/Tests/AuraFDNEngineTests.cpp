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

        beginTest("Bit Depth at 24 is a genuine bypass (bit-identical output)");
        {
            // setBitDepth(24) must not just be "very transparent" - float32's own mantissa is 24
            // bits, so round(x * 2^23) / 2^23 is NOT bit-identical to skipping the stage near full
            // scale. This locks in the explicit bitDepthActive bypass (see AuraFDNEngine.h).
            AuraFDNEngine untouched, explicitOff;
            untouched.prepare(sampleRate);
            explicitOff.prepare(sampleRate);
            untouched.setBandGains(0.9f, 0.85f);
            untouched.setDampingWeight(0.3f);
            explicitOff.setBandGains(0.9f, 0.85f);
            explicitOff.setDampingWeight(0.3f);
            explicitOff.setBitDepth(24.0f);

            constexpr int numSamples = 4000;
            std::vector<float> aL(numSamples, 0.0f), aR(numSamples, 0.0f);
            std::vector<float> bL(numSamples, 0.0f), bR(numSamples, 0.0f);
            aL[0] = 1.0f; aR[0] = 1.0f;
            bL[0] = 1.0f; bR[0] = 1.0f;
            untouched.processStereo(aL.data(), aR.data(), numSamples);
            explicitOff.processStereo(bL.data(), bR.data(), numSamples);

            float maxAbsDiff = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                maxAbsDiff = std::max({maxAbsDiff, std::abs(aL[(size_t) i] - bL[(size_t) i]),
                                        std::abs(aR[(size_t) i] - bR[(size_t) i])});
            expectEquals(maxAbsDiff, 0.0f, "setBitDepth(24) must render bit-identically to never calling setBitDepth at all");
        }

        beginTest("Bit Depth below 24 quantizes output onto an exact step lattice");
        {
            AuraFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setBandGains(0.9f, 0.85f);
            engine.setDampingWeight(0.3f);
            engine.setBitDepth(8.0f); // coarse enough that step size is easy to check exactly

            constexpr int numSamples = 4000;
            const auto envelope = renderImpulseEnvelope(engine, numSamples);
            expect(!hasNaNOrInf(envelope), "quantized output must stay finite");

            constexpr float levels = 128.0f; // 2^(8-1)
            constexpr float step = 1.0f / levels;
            std::vector<float> left(numSamples, 0.0f), right(numSamples, 0.0f);
            left[0] = 1.0f; right[0] = 1.0f;
            AuraFDNEngine reQuantized;
            reQuantized.prepare(sampleRate);
            reQuantized.setBandGains(0.9f, 0.85f);
            reQuantized.setDampingWeight(0.3f);
            reQuantized.setBitDepth(8.0f);
            reQuantized.processStereo(left.data(), right.data(), numSamples);

            bool allOnLattice = true;
            for (int i = 0; i < numSamples && allOnLattice; ++i)
            {
                const auto steps = left[(size_t) i] / step;
                allOnLattice = std::abs(steps - std::round(steps)) < 1.0e-4f;
            }
            expect(allOnLattice, "every output sample at 8-bit depth must be an exact multiple of 1/128");
        }

        beginTest("A quantized tail still decays to silence (no limit cycle)");
        {
            // Guards against ever moving the quantizer in-loop: quantizing inside the feedback
            // path risks a tail settling into a nonzero +-1 LSB oscillation instead of reaching
            // true silence. Placement is output-stage-only today (see AuraFDNEngine.h) - this test
            // documents why, and would catch a regression if that ever changed.
            AuraFDNEngine engine;
            engine.prepare(sampleRate);
            engine.setBandGains(0.94538f, 0.94538f); // AuraDecayGainData's own Time=5.5s value
            engine.setDampingWeight(0.98602f);
            engine.setBitDepth(8.0f); // coarsest available depth - worst case for limit cycles

            constexpr int numSamples = (int) (10.0 * sampleRate);
            const auto envelope = renderImpulseEnvelope(engine, numSamples);
            expect(!hasNaNOrInf(envelope), "quantized long tail must stay finite");

            const auto tailPeak = peakInWindow(envelope, 9.75, sampleRate, 0.5);
            expect(tailPeak < 1.0e-3f, "a 10s render at Time=5.5s settings must have decayed to silence by the last 0.5s, even quantized");
        }

        beginTest("Low Cut at 0Hz is a genuine bypass (bit-identical output)");
        {
            // Mirrors the Bit Depth bypass test: setLowCutHz(0) must not just be "very
            // transparent" - it must be an explicit no-op (AuraFDNEngine::lowCutActive), not a
            // filter running with a near-zero coefficient.
            AuraFDNEngine untouched, explicitOff;
            untouched.prepare(sampleRate);
            explicitOff.prepare(sampleRate);
            untouched.setBandGains(0.9f, 0.85f);
            untouched.setDampingWeight(0.3f);
            explicitOff.setBandGains(0.9f, 0.85f);
            explicitOff.setDampingWeight(0.3f);
            explicitOff.setLowCutHz(0.0f);

            constexpr int numSamples = 4000;
            std::vector<float> aL(numSamples, 0.0f), aR(numSamples, 0.0f);
            std::vector<float> bL(numSamples, 0.0f), bR(numSamples, 0.0f);
            aL[0] = 1.0f; aR[0] = 1.0f;
            bL[0] = 1.0f; bR[0] = 1.0f;
            untouched.processStereo(aL.data(), aR.data(), numSamples);
            explicitOff.processStereo(bL.data(), bR.data(), numSamples);

            float maxAbsDiff = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                maxAbsDiff = std::max({maxAbsDiff, std::abs(aL[(size_t) i] - bL[(size_t) i]),
                                        std::abs(aR[(size_t) i] - bR[(size_t) i])});
            expectEquals(maxAbsDiff, 0.0f, "setLowCutHz(0) must render bit-identically to never calling setLowCutHz at all");
        }

        beginTest("Low Cut removes low-frequency content from the very first tank arrival");
        {
            // Applied before pre-delay/the tank (see AuraFDNEngine.h's setLowCutHz() comment), so
            // - unlike the old, in-loop-only feedbackShelf tilt bug this project already hit once
            // (see the input-tilt regression test above) - this MUST show up at the very first
            // arrival, not just in the tail.
            AuraFDNEngine noCut, withCut;
            noCut.prepare(sampleRate);
            withCut.prepare(sampleRate);
            noCut.setBandGains(0.8f, 0.7f);
            noCut.setDampingWeight(0.3f);
            withCut.setBandGains(0.8f, 0.7f);
            withCut.setDampingWeight(0.3f);
            withCut.setLowCutHz(300.0f); // top of the range - maximum expected attenuation

            constexpr int numSamples = 4000;
            std::vector<float> noCutL(numSamples, 0.0f), noCutR(numSamples, 0.0f);
            std::vector<float> withCutL(numSamples, 0.0f), withCutR(numSamples, 0.0f);
            noCutL[0] = 1.0f; noCutR[0] = 1.0f;
            withCutL[0] = 1.0f; withCutR[0] = 1.0f;
            noCut.processStereo(noCutL.data(), noCutR.data(), numSamples);
            withCut.processStereo(withCutL.data(), withCutR.data(), numSamples);

            // Crude 150Hz one-pole lowpass energy over the first tank arrival window - well below
            // the 300Hz cutoff, so a real high-pass should leave very little low-band energy there.
            // Same onset window (~29-49ms) as the input-tilt regression test above.
            auto lowBandEnergy = [sampleRate](const std::vector<float>& x)
            {
                double energy = 0.0;
                float lp = 0.0f;
                const auto w = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * 150.0f / (float) sampleRate);
                const auto start = (int) (0.029 * sampleRate);
                const auto win = (int) (0.02 * sampleRate);
                for (int i = start; i < start + win && i < (int) x.size(); ++i)
                {
                    lp += w * (x[(size_t) i] - lp);
                    energy += (double) lp * lp;
                }
                return energy;
            };

            const auto noCutEnergy = lowBandEnergy(noCutL);
            const auto withCutEnergy = lowBandEnergy(withCutL);
            // A crude one-pole energy proxy (like highToLowEnergyRatio above), not a true spectral
            // filter, and the highpass itself is a gentle first-order (6dB/octave) slope, not a
            // brickwall - checked directly (0.44 measured here) before picking this threshold, the
            // same discipline as the input-tilt test above ("detect some difference", not an exact
            // number).
            expect(withCutEnergy < noCutEnergy * 0.6,
                "a 300Hz Low Cut should measurably reduce low-band energy at the very first tank arrival, not just the tail");
        }
    }
};

static AuraFDNEngineTests auraFDNEngineTests;
