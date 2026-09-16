#include "../ConcreteContourEnvelope.h"

#include <juce_core/juce_core.h>

class ConcreteContourEnvelopeTests : public juce::UnitTest
{
public:
    ConcreteContourEnvelopeTests() : juce::UnitTest("ConcreteContourEnvelope", "Concrete") {}

    void runTest() override
    {
        beginTest("Silent and inactive before any noteOn()");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            expect(! env.isActive());
            expect(env.getNextSample() == 0.0f);
        }

        beginTest("Ramps up to full scale during the attack stage, then starts decaying");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();

            float peak = 0.0f;
            for (int i = 0; i < (int) (44100.0 * 0.02); ++i) // 20ms - comfortably past the 2ms attack
                peak = juce::jmax(peak, env.getNextSample());

            expect(peak > 0.99f, "should reach (very close to) full scale during attack");
            expect(env.isActive(), "should still be active immediately after attack completes");
        }

        beginTest("Keeps decaying while held - level at 1s is lower than level at 50ms (no flat sustain)");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();

            float levelAt50ms = 0.0f;
            for (int i = 0; i < (int) (44100.0 * 0.05); ++i)
                levelAt50ms = env.getNextSample();

            float levelAt1s = levelAt50ms;
            for (int i = 0; i < (int) (44100.0 * 0.95); ++i)
                levelAt1s = env.getNextSample();

            expect(levelAt1s < levelAt50ms,
                   "a continuously-decaying contour must be quieter later while still held, unlike a flat ADSR sustain");
            expect(levelAt1s > 0.0f, "should not have reached silence yet at 1 second");
        }

        beginTest("Decay1 stage settles at its floor level, then decay2 continues below it");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();

            // Run well past decay1's ~150ms but nowhere near decay2's ~3s tail.
            float level = 0.0f;
            for (int i = 0; i < (int) (44100.0 * 0.3); ++i)
                level = env.getNextSample();
            const auto levelAfterDecay1 = level;

            for (int i = 0; i < (int) (44100.0 * 2.0); ++i)
                level = env.getNextSample();

            expect(level < levelAfterDecay1, "decay2 should keep reducing level well past decay1's own floor");
        }

        beginTest("Eventually reaches silence and goes inactive on its own, even with no noteOff()");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();

            bool becameInactive = false;
            for (int i = 0; i < (int) (44100.0 * 10.0) && ! becameInactive; ++i)
            {
                env.getNextSample();
                if (! env.isActive())
                    becameInactive = true;
            }

            expect(becameInactive, "the contour should decay all the way to silence within a reasonable time, unattended");
        }

        beginTest("noteOff() fades out from the current level rather than jumping or holding");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();

            for (int i = 0; i < (int) (44100.0 * 0.05); ++i)
                env.getNextSample();

            env.noteOff();
            const auto justAfterRelease = env.getNextSample();
            expect(justAfterRelease > 0.0f, "should not jump straight to silence on noteOff()");

            bool becameInactive = false;
            for (int i = 0; i < (int) (44100.0 * 0.5) && ! becameInactive; ++i)
            {
                env.getNextSample();
                if (! env.isActive())
                    becameInactive = true;
            }
            expect(becameInactive, "should reach silence well within the fixed release time");
        }

        beginTest("reset() silences immediately with no release fade");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();
            for (int i = 0; i < 100; ++i)
                env.getNextSample();

            env.reset();
            expect(! env.isActive());
            expect(env.getNextSample() == 0.0f);
        }

        beginTest("A fresh noteOn() after reset() starts a clean new attack from zero");
        {
            ConcreteContourEnvelope env;
            env.setSampleRate(44100.0);
            env.noteOn();
            for (int i = 0; i < 1000; ++i)
                env.getNextSample();
            env.reset();

            env.noteOn();
            expect(env.getNextSample() < 0.5f, "a new note should re-attack from near zero, not resume mid-decay");
        }
    }
};

static ConcreteContourEnvelopeTests concreteContourEnvelopeTests;
