#include "../StrikeRingModulator.h"

#include <juce_core/juce_core.h>

#include <cmath>

class StrikeRingModulatorTests : public juce::UnitTest
{
public:
    StrikeRingModulatorTests() : juce::UnitTest("StrikeRingModulator", "Strike") {}

    void runTest() override
    {
        beginTest("amount01=0 is a bit-exact no-op, at any input/frequency, even without ever calling updateOscillator()");
        {
            // gain = 1 + 0*(oscValue-1) = 1 regardless of oscValue, so this should hold even for a
            // freshly-constructed instance whose oscillator has never been advanced - matching how
            // StrikeStringLineChannel::renderChannelSample() skips calling updateOscillator() entirely
            // when ringModAmount is 0.
            StrikeRingModulator ringMod;
            ringMod.prepare(44100.0);
            for (float x : { 0.0f, 0.3f, -0.7f, 1.0f, -1.0f, 5.0f })
                expectWithinAbsoluteError(ringMod.process(x, 0.0f), x, 1.0e-6f);
        }

        beginTest("Ring modulation can only ever shrink or invert a signal, never amplify it");
        {
            // The core safety property this class exists for (see process()'s own comment): unlike
            // every waveshaping curve, there's no driveCompensation-style split needed here at all,
            // because |gain| <= 1 for any amount01 in [0,1] and any oscillator value in [-1,1].
            StrikeRingModulator ringMod;
            ringMod.prepare(44100.0);
            for (float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                for (float freq : { 20.0f, 200.0f, 2000.0f, 5000.0f })
                    for (float x : { 0.1f, 0.5f, 1.0f, 2.0f, 50.0f, 1000.0f, -1000.0f })
                    {
                        ringMod.updateOscillator(freq);
                        const auto y = ringMod.process(x, amount);
                        expect(std::abs(y) <= std::abs(x) + 1.0e-4f,
                               "ring-modulated output magnitude should never exceed the input's own magnitude");
                    }
        }

        beginTest("At amount01=1, output matches input * sin(2*pi*phase) exactly at known phase points");
        {
            // Rather than trust the implementation's own formula, hand-derive expected values at
            // phase=0 (sin=0, output should be exactly 0) and after enough samples to reach
            // phase=0.25 (sin=1, output should equal the input exactly) for a simple case where the
            // phase increment per sample is easy to reason about by hand.
            StrikeRingModulator ringMod;
            constexpr double sampleRate = 100.0;
            ringMod.prepare(sampleRate);
            constexpr float frequencyHz = 25.0f; // phase advances by 0.25 every sample at this rate

            ringMod.updateOscillator(frequencyHz); // uses phase=0 for THIS call, then advances
            expectWithinAbsoluteError(ringMod.process(1.0f, 1.0f), 0.0f, 1.0e-5f,
                                       "sin(2*pi*0) = 0, so output should be exactly 0 at the first call");

            ringMod.updateOscillator(frequencyHz); // now uses phase=0.25 (sin=1)
            expectWithinAbsoluteError(ringMod.process(0.7f, 1.0f), 0.7f, 1.0e-4f,
                                       "sin(2*pi*0.25) = 1, so output should equal input exactly at the second call");
        }

        beginTest("The oscillator's own phase wraps correctly (bounded, periodic) over many samples at a range of frequencies");
        {
            // Not a tight closed-form check like the test above - a broader sweep confirming the
            // oscillator stays well-behaved (bounded, doesn't drift/blow up) over a long run, since
            // phase is accumulated every sample via floating-point addition.
            for (float freq : { 20.0f, 440.0f, 5000.0f })
            {
                StrikeRingModulator ringMod;
                ringMod.prepare(44100.0);
                for (int i = 0; i < 44100 * 4; ++i)
                {
                    ringMod.updateOscillator(freq);
                    const auto y = ringMod.process(1.0f, 1.0f);
                    expect(std::abs(y) <= 1.0f + 1.0e-4f, "oscillator output should stay bounded to +-1 over a long run");
                }
            }
        }

        beginTest("prepare()/reset() don't crash, and reset() clears phase back to 0");
        {
            StrikeRingModulator ringMod;
            ringMod.prepare(44100.0);
            for (int i = 0; i < 1000; ++i)
                ringMod.updateOscillator(440.0f); // advance phase somewhere non-zero
            ringMod.reset();

            // Immediately after reset(), the next updateOscillator() call should use phase=0 (same
            // "sin(2*pi*0)=0" reasoning as the closed-form test above).
            ringMod.updateOscillator(440.0f);
            expectWithinAbsoluteError(ringMod.process(1.0f, 1.0f), 0.0f, 1.0e-5f,
                                       "reset() should clear phase back to 0, not leave it wherever it was");
        }
    }
};

static StrikeRingModulatorTests strikeRingModulatorTests;
