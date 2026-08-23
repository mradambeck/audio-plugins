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
