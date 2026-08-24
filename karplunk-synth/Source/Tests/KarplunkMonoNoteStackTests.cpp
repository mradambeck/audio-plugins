#include "../KarplunkMonoNoteStack.h"

#include <juce_core/juce_core.h>

class KarplunkMonoNoteStackTests : public juce::UnitTest
{
public:
    KarplunkMonoNoteStackTests() : juce::UnitTest("KarplunkMonoNoteStack", "Karplunk") {}

    void runTest() override
    {
        beginTest("noteOn() always returns the note+velocity just pressed");
        {
            KarplunkMonoNoteStack<8> stack;
            const auto event = stack.noteOn(60, 0.8f);
            expect(event.note == 60);
            expectWithinAbsoluteError(event.velocity01, 0.8f, 1e-6f);
        }

        beginTest("The core behavior this class exists for: hold A, hold B, release B -> A retriggers");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);  // A
            stack.noteOn(64, 0.9f);  // B, now on top

            const auto result = stack.noteOff(64); // release B
            expect(result.stillHeld, "A should still be held after releasing B");
            expect(result.event.note == 60, "releasing B should retrigger A, the note still held");
            expectWithinAbsoluteError(result.event.velocity01, 0.5f, 1e-6f,
                                       "the retrigger should use A's OWN original velocity, not B's");
        }

        beginTest("Releasing the only held note reports nothing left held");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);
            const auto result = stack.noteOff(60);
            expect(!result.stillHeld, "no notes remain held, caller should release the voice");
        }

        beginTest("Releasing A while B (pressed after) is still held does NOT retrigger - B keeps sounding");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);  // A
            stack.noteOn(64, 0.9f);  // B, now on top

            const auto result = stack.noteOff(60); // release A (not on top)
            expect(result.stillHeld, "B should still be held");
            expect(result.event.note == 64, "the still-sounding note is B, unaffected by releasing A");
        }

        beginTest("Three-deep stack: releasing the top falls back to the next-most-recent, not the oldest");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.1f); // A
            stack.noteOn(64, 0.2f); // B
            stack.noteOn(67, 0.3f); // C, on top

            auto result = stack.noteOff(67); // release C
            expect(result.stillHeld);
            expect(result.event.note == 64, "should fall back to B (the next most recent), not A");

            result = stack.noteOff(64); // release B
            expect(result.stillHeld);
            expect(result.event.note == 60, "should fall back to A, the only one left");

            result = stack.noteOff(60); // release A
            expect(!result.stillHeld, "nothing left held");
        }

        beginTest("A duplicate note-on for an already-held note moves it to the top, not a second entry");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.1f); // A
            stack.noteOn(64, 0.2f); // B
            stack.noteOn(60, 0.9f); // A pressed again (no note-off in between) - moves to top, velocity updates

            const auto result = stack.noteOff(60); // release A (currently on top)
            expect(result.stillHeld);
            expect(result.event.note == 64, "B should be revealed - A must not have been duplicated below it");

            const auto result2 = stack.noteOff(64);
            expect(!result2.stillHeld, "nothing should remain - confirms A wasn't left behind as a stale duplicate");
        }

        beginTest("noteOff() for a note that isn't held is a safe no-op, doesn't disturb the stack");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);
            const auto result = stack.noteOff(99); // never held
            expect(result.stillHeld, "the actually-held note should be unaffected/still reported held");
            expect(result.event.note == 60);
        }

        beginTest("reset() clears all held notes");
        {
            KarplunkMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);
            stack.noteOn(64, 0.5f);
            stack.reset();
            expect(stack.empty());
            const auto result = stack.noteOff(60);
            expect(!result.stillHeld, "reset should have cleared everything");
        }

        beginTest("Exceeding MaxNotes drops the oldest held note rather than overflowing");
        {
            KarplunkMonoNoteStack<2> stack;
            stack.noteOn(60, 0.1f); // oldest, will be dropped
            stack.noteOn(64, 0.2f);
            stack.noteOn(67, 0.3f); // pushes out note 60

            const auto result = stack.noteOff(60); // no longer tracked - safe no-op
            expect(result.stillHeld, "the two still-tracked notes should remain reported as held");
            expect(result.event.note == 67, "most recent (67) should still be on top");
        }
    }
};

static KarplunkMonoNoteStackTests karplunkMonoNoteStackTests;
