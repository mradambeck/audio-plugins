#include "../KarplunkWaveshaper.h"

#include <juce_core/juce_core.h>

#include <cmath>

class KarplunkWaveshaperTests : public juce::UnitTest
{
public:
    KarplunkWaveshaperTests() : juce::UnitTest("KarplunkWaveFolder", "Karplunk") {}

    void runTest() override
    {
        beginTest("Zero input always folds to exactly zero, at any amount");
        {
            KarplunkWaveFolder folder;
            for (float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                expectWithinAbsoluteError(folder.process(0.0f, amount), 0.0f, 1.0e-6f);
        }

        beginTest("Output is unconditionally bounded to +-1, regardless of input magnitude or amount");
        {
            // The core safety property this class exists for: a wavefolder inside a feedback
            // loop must not be able to blow up, however hard it's driven. Sweeps well beyond any
            // musically realistic input.
            KarplunkWaveFolder folder;
            for (float amount : { 0.0f, 0.5f, 1.0f })
                for (float x : { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f, 50.0f, 1000.0f, -1000.0f })
                {
                    const auto y = folder.process(x, amount);
                    expect(std::abs(y) <= 1.0f + 1.0e-5f,
                           "wavefolder output must never exceed +-1, at any drive/input combination");
                }
        }

        beginTest("Odd/symmetric: folding -x gives exactly -(folding x), at any amount");
        {
            KarplunkWaveFolder folder;
            for (float amount : { 0.0f, 0.3f, 0.7f, 1.0f })
                for (float x : { 0.1f, 0.5f, 1.3f, 3.7f })
                {
                    const auto yPos = folder.process(x, amount);
                    const auto yNeg = folder.process(-x, amount);
                    expectWithinAbsoluteError(yNeg, -yPos, 1.0e-5f, "a symmetric fold should be an odd function");
                }
        }

        beginTest("At amount=0 (minimum drive), a small input passes through nearly unchanged");
        {
            // minDrive=1 and threshold=1 means small inputs stay well under the fold point even
            // with zero amount - asin(sin(t)) ~ t for small t, so this should be close to a
            // transparent passthrough, not an already-folded signal. (The real bypass at exactly
            // amount=0 lives in SingleLineKarplunkVoice, which never calls process() at all in
            // that case - this test is about the class's OWN behavior in isolation.)
            KarplunkWaveFolder folder;
            const auto y = folder.process(0.1f, 0.0f);
            expectWithinAbsoluteError(y, 0.1f, 0.01f);
        }

        beginTest("At full drive, a strong enough input actually FOLDS (output stops increasing and reverses) - not just clips/flattens");
        {
            // The defining behavioral difference between folding and clipping: a clipper's output
            // magnitude is monotonically non-decreasing then flat as input grows; a folder's
            // output reverses direction once input passes a reflection point. Confirmed by
            // sweeping input and checking the output is NOT monotonic across the sweep.
            KarplunkWaveFolder folder;
            constexpr float amount = 1.0f;

            float previous = 0.0f;
            bool sawIncrease = false;
            bool sawDecrease = false;
            for (int i = 1; i <= 200; ++i)
            {
                const auto x = (float) i * 0.02f; // sweeps 0.02 up to 4.0
                const auto y = folder.process(x, amount);
                if (i > 1)
                {
                    if (y > previous + 1.0e-4f) sawIncrease = true;
                    if (y < previous - 1.0e-4f) sawDecrease = true;
                }
                previous = y;
            }

            expect(sawIncrease && sawDecrease,
                   "sweeping input at full drive should show both rising and falling output - proof of actual folding, not just saturation");
        }

        beginTest("prepare()/reset() don't crash and leave the folder usable (stateless, but part of the required seam contract)");
        {
            KarplunkWaveFolder folder;
            folder.prepare(44100.0);
            folder.reset();
            expectWithinAbsoluteError(folder.process(0.3f, 0.5f), folder.process(0.3f, 0.5f), 1.0e-6f);
        }
    }
};

static KarplunkWaveshaperTests karplunkWaveshaperTests;

namespace
{
    // KarplunkFuzz has real per-sample filter state (unlike KarplunkWaveFolder) - process() reads
    // whatever updateFilter() last computed, so tests need to drive the filter to a settled
    // steady state for a given (x, amount) before checking process()'s output, matching how
    // SingleLineKarplunkVoice actually uses it (updateFilter() every sample, not once).
    float settledFuzzOutput(KarplunkFuzz& fuzz, float x, float amount, float driveCompensation = 1.0f)
    {
        for (int i = 0; i < 200; ++i) // far more than enough for a 6kHz one-pole at 44.1kHz to settle
            fuzz.updateFilter(x, amount);
        return fuzz.process(amount, driveCompensation);
    }
}

class KarplunkFuzzTests : public juce::UnitTest
{
public:
    KarplunkFuzzTests() : juce::UnitTest("KarplunkFuzz", "Karplunk") {}

    void runTest() override
    {
        beginTest("Zero input always clips to exactly zero, at any amount");
        {
            KarplunkFuzz fuzz;
            fuzz.prepare(44100.0);
            for (float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                expectWithinAbsoluteError(settledFuzzOutput(fuzz, 0.0f, amount), 0.0f, 1.0e-6f);
        }

        beginTest("Output is unconditionally bounded to +-1, regardless of input magnitude or amount");
        {
            // The core safety property this class exists for, same as KarplunkWaveFolder: a
            // nonlinearity inside a feedback loop must not be able to blow up, however hard it's
            // driven. Sweeps well beyond any musically realistic input.
            KarplunkFuzz fuzz;
            fuzz.prepare(44100.0);
            for (float amount : { 0.0f, 0.5f, 1.0f })
                for (float x : { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f, 50.0f, 1000.0f, -1000.0f })
                {
                    const auto y = settledFuzzOutput(fuzz, x, amount);
                    expect(std::abs(y) <= 1.0f + 1.0e-5f,
                           "fuzz output must never exceed +-1, at any drive/input combination");
                }
        }

        beginTest("Genuinely asymmetric: positive and negative half-cycles clip differently, not a mirror image");
        {
            // This is what actually gives KarplunkFuzz its character (see its own class comment)
            // - unlike KarplunkWaveFolder's deliberately ODD/symmetric fold, a real transistor
            // fuzz's asymmetry is the point (it's what introduces even harmonics on top of tanh's
            // own odd-harmonic saturation). Confirmed at high drive, where the two curves'
            // effective saturation clearly diverges. driveCompensation=0 (matching the
            // perceptually-relevant OUTPUT path, see process()'s own comment) - at full
            // compensation (the default), both values get divided down by the same large drive
            // factor, shrinking their absolute difference toward an easily-missed rounding-sized
            // gap even though the underlying curves are genuinely asymmetric (found exactly this
            // while first writing this test, the same lesson KarplunkWaveFolder's own tests
            // learned the hard way). A fresh instance per measurement, so each settles to ITS OWN
            // steady state rather than inheriting the previous input's filter history.
            bool sawRealAsymmetry = false;
            for (float x : { 0.1f, 0.3f, 0.6f, 1.0f, 2.0f })
            {
                KarplunkFuzz fuzzPos;
                fuzzPos.prepare(44100.0);
                const auto yPos = settledFuzzOutput(fuzzPos, x, 1.0f, 0.0f);

                KarplunkFuzz fuzzNeg;
                fuzzNeg.prepare(44100.0);
                const auto yNeg = settledFuzzOutput(fuzzNeg, -x, 1.0f, 0.0f);

                if (std::abs(std::abs(yNeg) - std::abs(yPos)) > 1.0e-3f)
                    sawRealAsymmetry = true;
            }
            expect(sawRealAsymmetry, "positive and negative half-cycles should clip to measurably different magnitudes");
        }

        beginTest("At amount=0 (minimum drive), a small input passes through nearly unchanged");
        {
            // minDrive=1 means a small input stays well under tanh's saturation region even with
            // zero amount - tanh(t) ~ t for small t, so this should be close to a transparent
            // passthrough (once the lowpass has settled - at DC/a steady input, a one-pole
            // lowpass converges to unity gain, so this doesn't test the filter, just the curve).
            // (The real bypass at exactly amount=0 lives in SingleLineKarplunkVoice, which never
            // calls process() at all in that case - this test is about the class's OWN behavior.)
            KarplunkFuzz fuzz;
            fuzz.prepare(44100.0);
            const auto y = settledFuzzOutput(fuzz, 0.1f, 0.0f);
            expectWithinAbsoluteError(y, 0.1f, 0.01f);
        }

        beginTest("At full drive, output stays monotonic (clips/flattens) - it does NOT fold like KarplunkWaveFolder");
        {
            // The defining behavioral difference from folding: a clipper's output magnitude is
            // monotonically non-decreasing then flat as input grows, never reversing direction.
            // Confirms this is a genuinely different KIND of nonlinearity, not just a differently-
            // tuned fold. A one-pole lowpass of a monotonically non-decreasing sequence is itself
            // guaranteed non-decreasing (each step is a convex combination of the previous output
            // and a larger-or-equal target), so this property survives the added filter without
            // needing to let it settle at every step - only one updateFilter() call per swept x,
            // matching how a real, continuously-changing signal would drive it.
            KarplunkFuzz fuzz;
            fuzz.prepare(44100.0);
            constexpr float amount = 1.0f;

            float previous = 0.0f;
            bool everDecreased = false;
            for (int i = 1; i <= 200; ++i)
            {
                const auto x = (float) i * 0.02f; // sweeps 0.02 up to 4.0
                fuzz.updateFilter(x, amount);
                const auto y = fuzz.process(amount);
                if (i > 1 && y < previous - 1.0e-4f)
                    everDecreased = true;
                previous = y;
            }

            expect(!everDecreased, "a clipper's output should never decrease as a positive input keeps increasing");
        }

        beginTest("prepare()/reset() don't crash, and reset() clears filter history back to zero");
        {
            KarplunkFuzz fuzz;
            fuzz.prepare(44100.0);
            for (int i = 0; i < 200; ++i)
                fuzz.updateFilter(0.8f, 1.0f); // settle to a loud, non-zero steady state
            fuzz.reset();

            fuzz.updateFilter(0.0f, 1.0f); // one tick of silence right after reset
            expectWithinAbsoluteError(fuzz.process(1.0f), 0.0f, 1.0e-6f,
                                       "reset() should clear filter history, not leave the previous loud state behind");
        }
    }
};

static KarplunkFuzzTests karplunkFuzzTests;
