#include "../GradientDelayBuffer.h"

#include <juce_core/juce_core.h>

// Milestone 1's deliberate exception to this catalog's usual by-ear-only verification: an
// interpolation/wrap-boundary bug here is subtle and hard to diagnose by ear, and every later
// milestone depends on this class being exactly right.
class GradientDelayBufferTests : public juce::UnitTest
{
public:
    GradientDelayBufferTests() : juce::UnitTest("GradientDelayBuffer", "Gradient") {}

    void runTest() override
    {
        beginTest("Zero delay returns the most recently written sample");
        {
            GradientDelayBuffer buf;
            buf.setSize(16);
            for (int i = 0; i < 5; ++i)
                buf.write((float) i);
            expectWithinAbsoluteError(buf.readInterpolated(0.0f), 4.0f, 1.0e-6f);
        }

        beginTest("Integer delays return exact past samples");
        {
            GradientDelayBuffer buf;
            buf.setSize(16);
            for (int i = 0; i < 10; ++i)
                buf.write((float) i);
            // Most recent write (9) is delay 0, the write before it (8) is delay 1, etc.
            for (int delay = 0; delay <= 9; ++delay)
                expectWithinAbsoluteError(buf.readInterpolated((float) delay), (float) (9 - delay), 1.0e-6f);
        }

        beginTest("Fractional delays interpolate linearly between neighbouring samples");
        {
            GradientDelayBuffer buf;
            buf.setSize(16);
            for (int i = 0; i < 10; ++i)
                buf.write((float) i);
            // delay 0 = 9, delay 1 = 8 -> halfway should be 8.5
            expectWithinAbsoluteError(buf.readInterpolated(0.5f), 8.5f, 1.0e-4f);
            // delay 4 = 5, delay 5 = 4 -> halfway should be 4.5
            expectWithinAbsoluteError(buf.readInterpolated(4.5f), 4.5f, 1.0e-4f);
            // delay 2 = 7, delay 3 = 6 -> 25% of the way from 7 toward 6 should be 6.75
            expectWithinAbsoluteError(buf.readInterpolated(2.25f), 6.75f, 1.0e-4f);
        }

        beginTest("Writing past capacity wraps and correctly overwrites the oldest data");
        {
            GradientDelayBuffer buf;
            buf.setSize(8);
            for (int i = 0; i < 20; ++i) // more than double the buffer's capacity
                buf.write((float) i);
            // Most recent write (19) is delay 0; only the last 8 writes (12..19) are still valid.
            for (int delay = 0; delay <= 7; ++delay)
                expectWithinAbsoluteError(buf.readInterpolated((float) delay), (float) (19 - delay), 1.0e-6f);
        }

        beginTest("Negative delays read 'ahead' of the write head and still wrap correctly");
        {
            // Protects the branch-based index wrap (a single conditional add/subtract, replacing
            // true modulo) added as a performance optimization: unlike %, it's only correct within
            // one buffer-length of [0, size), so a negative delay - which pushes the raw index past
            // size, needing the upper-bound wrap branch, not just the lower-bound one every other
            // test here exercises - is exactly the case most likely to expose an off-by-one in that
            // replacement (e.g. `>` instead of `>=` at the boundary).
            GradientDelayBuffer buf;
            buf.setSize(8);
            for (int i = 0; i < 5; ++i) // writes 0..4 to indices 0-4; writeIndex lands on 5
                buf.write((float) i);

            // readPosition = (writeIndex-1) - delay = 4 - (-5) = 9 -> floorPosition 9, which is
            // size(8)+1 -> wrapIndex must subtract size once to land on index 1 (value 1.0).
            expectWithinAbsoluteError(buf.readInterpolated(-5.0f), 1.0f, 1.0e-6f);

            // readPosition = 4 - (-4.5) = 8.5 -> floorPosition exactly AT size (8), the critical
            // boundary for >= vs > in the wrap branch; index0 must wrap to 0 (value 0.0), index1 to
            // 1 (value 1.0), interpolating halfway to 0.5.
            expectWithinAbsoluteError(buf.readInterpolated(-4.5f), 0.5f, 1.0e-4f);
        }

        beginTest("reset() clears history to silence");
        {
            GradientDelayBuffer buf;
            buf.setSize(8);
            for (int i = 0; i < 8; ++i)
                buf.write(1.0f);
            buf.reset();
            for (int delay = 0; delay <= 6; ++delay)
                expectWithinAbsoluteError(buf.readInterpolated((float) delay), 0.0f, 1.0e-6f);
        }
    }
};

static GradientDelayBufferTests gradientDelayBufferTests;
