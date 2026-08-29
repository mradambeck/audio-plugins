#include "../KarplunkWaveshaper.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <vector>

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
            // amount=0 lives in KarplunkStringLineChannel, which never calls process() at all in
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

class KarplunkBitCrushTests : public juce::UnitTest
{
public:
    KarplunkBitCrushTests() : juce::UnitTest("KarplunkBitCrush", "Karplunk") {}

    void runTest() override
    {
        beginTest("Zero input always crushes to exactly zero, at any amount");
        {
            for (float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                KarplunkBitCrush bitCrush;
                bitCrush.prepare(44100.0);
                bitCrush.updateFilter(0.0f, amount);
                expectWithinAbsoluteError(bitCrush.process(), 0.0f, 1.0e-6f);
            }
        }

        beginTest("Output is unconditionally bounded to +-1, regardless of input magnitude or amount");
        {
            // No settling needed (unlike KarplunkFuzz/KarplunkSaturator's real lowpass filter
            // state) - a fresh instance's holdCounter starts at 0, so a single updateFilter() call
            // already reflects the true quantized value.
            for (float amount : { 0.0f, 0.5f, 1.0f })
                for (float x : { 0.0f, 0.5f, 1.0f, 2.0f, 5.0f, 50.0f, 1000.0f, -1000.0f })
                {
                    KarplunkBitCrush bitCrush;
                    bitCrush.prepare(44100.0);
                    bitCrush.updateFilter(x, amount);
                    const auto y = bitCrush.process();
                    expect(std::abs(y) <= 1.0f + 1.0e-5f,
                           "bitcrush output must never exceed +-1, at any crush amount/input combination - "
                           "the one class in this file where an unbounded input could otherwise round further "
                           "from zero rather than saturate toward it, see the class's own comment");
                }
        }

        beginTest("At amount=0 (finest quantization, no hold), a small input passes through nearly unchanged");
        {
            KarplunkBitCrush bitCrush;
            bitCrush.prepare(44100.0);
            bitCrush.updateFilter(0.1f, 0.0f);
            expectWithinAbsoluteError(bitCrush.process(), 0.1f, 0.001f);
        }

        beginTest("At high amount, quantization genuinely collapses distinct nearby inputs to a small set of output levels");
        {
            // The defining property of bit-depth reduction: many different inputs map to the same
            // output. Confirmed by feeding a dense sweep and counting distinct outputs, not just
            // checking any one value looks "crushed."
            constexpr float amount = 1.0f;
            std::vector<float> outputs;
            for (int i = 0; i <= 200; ++i)
            {
                KarplunkBitCrush bitCrush;
                bitCrush.prepare(44100.0);
                const auto x = -1.0f + (float) i * (2.0f / 200.0f); // sweeps -1.0 to 1.0
                bitCrush.updateFilter(x, amount);
                outputs.push_back(bitCrush.process());
            }

            std::sort(outputs.begin(), outputs.end());
            outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());

            expect(outputs.size() < 50,
                   "201 distinct nearby inputs at max crush should collapse to well under 50 distinct outputs");
        }

        beginTest("Sample-and-hold: at high amount, the output stays fixed across several ticks even as input changes, then updates");
        {
            KarplunkBitCrush bitCrush;
            bitCrush.prepare(44100.0);
            constexpr float amount = 1.0f; // maxHoldSamples worth of hold

            bitCrush.updateFilter(0.5f, amount);
            const auto firstHeld = bitCrush.process();

            bool sawUnchangedWhileInputMoved = false;
            for (int i = 0; i < 5; ++i)
            {
                bitCrush.updateFilter(-0.5f, amount); // input changes, but should still be holding
                if (std::abs(bitCrush.process() - firstHeld) < 1.0e-9f)
                    sawUnchangedWhileInputMoved = true;
            }

            expect(sawUnchangedWhileInputMoved,
                   "at high crush amount, the held output should stay fixed for several ticks despite the input changing");
        }

        beginTest("prepare()/reset() don't crash, and reset() clears hold state (next tick re-quantizes immediately)");
        {
            KarplunkBitCrush bitCrush;
            bitCrush.prepare(44100.0);
            for (int i = 0; i < 40; ++i)
                bitCrush.updateFilter(0.8f, 1.0f); // settle into a loud held state
            bitCrush.reset();

            bitCrush.updateFilter(0.0f, 1.0f); // one tick of silence right after reset
            expectWithinAbsoluteError(bitCrush.process(), 0.0f, 1.0e-6f,
                                       "reset() should clear hold state, not leave the previous loud value behind");
        }
    }
};

static KarplunkBitCrushTests karplunkBitCrushTests;
