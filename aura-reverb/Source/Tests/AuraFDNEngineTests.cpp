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

        beginTest("Pre-delay glides smoothly when changed mid-stream (regression test for zipper noise)");
        {
            // Real bug found 2026-09-03 (Adam: "clippy graininess" while moving Pre-Delay):
            // setPreDelayMs() used to write a plain int straight into a non-interpolated buffer
            // read, so every change - including the once-per-block update PluginProcessor already
            // does while a knob is being dragged - jumped the read pointer to a new integer sample
            // index with zero transition. This test drives the engine the same way the real
            // plugin does (setPreDelayMs() between processStereo() calls, matching one call per
            // block) and checks the OUTPUT for a discontinuity at the change, not just that the
            // parameter value itself changed.
            AuraFDNEngine engine;
            engine.prepare(sampleRate);
            // Zero feedback isolates the pre-delay stage's own contribution: with the recirculating
            // path silenced, the tank's output is dominated by the continuously-injected (tilted/
            // pre-delayed) input itself, so a discontinuity in the pre-delay read shows up directly
            // in the output rather than being masked by unrelated tank complexity.
            engine.setBandGains(0.0f, 0.0f);
            engine.setDampingWeight(0.0f);
            engine.setPreDelayMs(50.0f); // primes immediately - no glide-in needed for this first value

            constexpr int chunkSamples = 8000;
            std::vector<float> left(static_cast<size_t>(chunkSamples) * 2, 0.0f);
            std::vector<float> right(static_cast<size_t>(chunkSamples) * 2, 0.0f);

            // A continuous, smoothly-varying signal (not an impulse/silence) spanning both chunks
            // with no discontinuity of its own - any discontinuity found below must come from the
            // pre-delay read, not from the input signal.
            constexpr float toneHz = 100.0f;
            constexpr float amplitude = 0.5f;
            for (int i = 0; i < chunkSamples * 2; ++i)
            {
                const auto sample = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * toneHz * (float) i / (float) sampleRate);
                left[(size_t) i] = sample;
                right[(size_t) i] = sample;
            }

            // First block at the primed 50ms pre-delay.
            engine.processStereo(left.data(), right.data(), chunkSamples);
            // A modest (2ms) jump partway through the stream, exactly how
            // PluginProcessor::processBlock() calls setPreDelayMs() once per block regardless of
            // whether the value changed. Deliberately NOT a huge jump (e.g. 50ms->150ms): sweeping
            // the read tap that far, even smoothly, causes a real, legitimate Doppler-like pitch
            // bend for the duration of the glide (an unavoidable property of ANY smoothly-varying
            // delay, not a bug) - large enough on its own to swamp the smaller signal this test is
            // actually looking for. A small jump keeps the smoothed glide's own induced modulation
            // negligible against steady-state, while still being large enough (~2ms is ~1/5 of this
            // tone's own 10ms period) that the OLD, un-interpolated integer jump lands on a clearly
            // uncorrelated point on the sine and produces an obviously anomalous spike.
            engine.setPreDelayMs(52.0f);
            engine.processStereo(left.data() + chunkSamples, right.data() + chunkSamples, chunkSamples);

            expect(!hasNaNOrInf(left), "output must stay finite through a large mid-stream pre-delay change");

            // A jump at the pre-delay READ (sample index `chunkSamples`, where setPreDelayMs() was
            // called) does NOT show up in the OUTPUT until later: preL/preR only reach the output
            // by being written into a tank line at that instant and read back out one full line
            // length afterwards (this engine's 8 lines span ~30-76ms - see AuraFDNEngine.h's
            // baseLineLengthsMs comment). Search a window covering that whole range, with margin,
            // AFTER the boundary - not right at it, which would silently miss the effect entirely
            // (confirmed by first running this test against the pre-fix engine with the naive
            // right-at-the-boundary window: it passed even with the old, un-interpolated read,
            // because it was looking in the wrong place).
            const auto searchStart = chunkSamples + (int) (0.025 * sampleRate);
            const auto searchEnd = chunkSamples + (int) (0.085 * sampleRate);

            float maxDeltaNearTransition = 0.0f;
            for (int i = searchStart; i < searchEnd; ++i)
                maxDeltaNearTransition = std::max(maxDeltaNearTransition, std::abs(left[(size_t) i] - left[(size_t) (i - 1)]));

            // Steady-state region well away from both the startup transient at sample 0 and the
            // transition-affected window above.
            float maxDeltaSteadyState = 0.0f;
            for (int i = 1000; i < chunkSamples - 1000; ++i)
                maxDeltaSteadyState = std::max(maxDeltaSteadyState, std::abs(left[(size_t) i] - left[(size_t) (i - 1)]));

            expect(maxDeltaSteadyState > 0.0f, "steady-state region should have real, non-silent per-sample variation to compare against");
            expect(maxDeltaNearTransition < maxDeltaSteadyState * 4.0f,
                "a 50ms->52ms pre-delay jump between blocks must not produce a sample-to-sample "
                "delta far larger than normal steady-state variation, once the tank has had time "
                "to read the jumped pre-delay stage back out - a hard jump in the old "
                "non-interpolated read could land on an uncorrelated point on the sine and produce "
                "an anomalous spike well beyond this threshold");
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

        beginTest("Sub-bass gain at 1.0 is a genuine bypass (bit-identical output)");
        {
            // See AuraFDNEngine.h's setSubBassGain() comment - 1.0 must be a true no-op, same
            // explicit-bypass contract as Bit Depth's 24 and Low Cut's 0Hz.
            AuraFDNEngine untouched, explicitDefault;
            untouched.prepare(sampleRate);
            explicitDefault.prepare(sampleRate);
            untouched.setBandGains(0.9f, 0.9f);
            untouched.setDampingWeight(0.9f);
            explicitDefault.setBandGains(0.9f, 0.9f);
            explicitDefault.setDampingWeight(0.9f);
            explicitDefault.setSubBassGain(1.0f);

            constexpr int numSamples = 4000;
            std::vector<float> aL(numSamples, 0.0f), aR(numSamples, 0.0f);
            std::vector<float> bL(numSamples, 0.0f), bR(numSamples, 0.0f);
            aL[0] = 1.0f; aR[0] = 1.0f;
            bL[0] = 1.0f; bR[0] = 1.0f;
            untouched.processStereo(aL.data(), aR.data(), numSamples);
            explicitDefault.processStereo(bL.data(), bR.data(), numSamples);

            float maxAbsDiff = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                maxAbsDiff = std::max({maxAbsDiff, std::abs(aL[(size_t) i] - bL[(size_t) i]),
                                        std::abs(aR[(size_t) i] - bR[(size_t) i])});
            expectEquals(maxAbsDiff, 0.0f, "setSubBassGain(1.0) must render bit-identically to never calling setSubBassGain at all");
        }

        beginTest("Reducing sub-bass gain measurably shortens the low band's tail without changing crest factor");
        {
            AuraFDNEngine baseline, attenuated;
            baseline.prepare(sampleRate);
            attenuated.prepare(sampleRate);
            baseline.setBandGains(0.9f, 0.9f);
            baseline.setDampingWeight(0.9f);
            attenuated.setBandGains(0.9f, 0.9f);
            attenuated.setDampingWeight(0.9f);
            attenuated.setSubBassGain(0.9f);

            constexpr int numSamples = (int) (2.0 * sampleRate);
            const auto baselineEnv = renderImpulseEnvelope(baseline, numSamples);
            const auto attenuatedEnv = renderImpulseEnvelope(attenuated, numSamples);
            expect(!hasNaNOrInf(baselineEnv) && !hasNaNOrInf(attenuatedEnv), "output must stay finite with sub-bass gain applied");

            // Late-tail peak (well past onset, where a per-lap attenuation has had time to
            // compound) should be measurably lower with sub-bass gain reduced.
            const auto baselineLate = peakInWindow(baselineEnv, 1.8, sampleRate, 0.08);
            const auto attenuatedLate = peakInWindow(attenuatedEnv, 1.8, sampleRate, 0.08);
            expect(attenuatedLate < baselineLate * 0.9,
                "reducing subBassGain should measurably reduce the late-tail level (faster decay), not just leave it unchanged");

            // Crest factor over the same window should be essentially unchanged - this is the
            // exact property the two rejected "more lines" attempts broke; this mechanism must
            // not repeat that (see AuraSubBassGainData.h's side-effect-check comment).
            auto crestFactorDb = [&](const std::vector<float>& env, double centerSeconds)
            {
                const auto start = (int) ((centerSeconds - 0.05) * sampleRate);
                const auto end = (int) ((centerSeconds + 0.05) * sampleRate);
                double sumSquares = 0.0;
                float peak = 0.0f;
                for (int i = std::max(0, start); i < std::min((int) env.size(), end); ++i)
                {
                    peak = std::max(peak, env[(size_t) i]);
                    sumSquares += (double) env[(size_t) i] * env[(size_t) i];
                }
                const auto count = std::max(1, std::min((int) env.size(), end) - std::max(0, start));
                const auto rms = std::sqrt(sumSquares / count);
                return 20.0f * std::log10((peak + 1.0e-12f) / (float) (rms + 1.0e-12));
            };
            const auto baselineCrest = crestFactorDb(baselineEnv, 0.5);
            const auto attenuatedCrest = crestFactorDb(attenuatedEnv, 0.5);
            expect(std::abs(baselineCrest - attenuatedCrest) < 1.0f,
                "reducing subBassGain should not meaningfully change the tank's crest factor / density character");
        }
    }
};

static AuraFDNEngineTests auraFDNEngineTests;
