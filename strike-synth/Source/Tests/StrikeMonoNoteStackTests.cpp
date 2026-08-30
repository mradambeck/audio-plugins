#include "../StrikeMonoNoteStack.h"

#include <juce_core/juce_core.h>

class StrikeMonoNoteStackTests : public juce::UnitTest
{
public:
    StrikeMonoNoteStackTests() : juce::UnitTest("StrikeMonoNoteStack", "Strike") {}

    void runTest() override
    {
        beginTest("noteOn() always returns the note+velocity just pressed");
        {
            StrikeMonoNoteStack<8> stack;
            const auto event = stack.noteOn(60, 0.8f);
            expect(event.note == 60);
            expectWithinAbsoluteError(event.velocity01, 0.8f, 1e-6f);
        }

        beginTest("The core behavior this class exists for: hold A, hold B, release B -> A retriggers");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);  // A
            stack.noteOn(64, 0.9f);  // B, now on top

            const auto result = stack.noteOff(64); // release B (the one sounding)
            expect(result.action == Action::Retrigger, "A should retrigger after releasing B");
            expect(result.event.note == 60, "releasing B should retrigger A, the note still held");
            expectWithinAbsoluteError(result.event.velocity01, 0.5f, 1e-6f,
                                       "the retrigger should use A's OWN original velocity, not B's");
        }

        beginTest("Releasing the only held note reports nothing left held");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);
            const auto result = stack.noteOff(60);
            expect(result.action == Action::Release, "no notes remain held, caller should release the voice");
        }

        beginTest("Releasing A while B (pressed after) is still held does NOT retrigger - B keeps sounding");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);  // A
            stack.noteOn(64, 0.9f);  // B, now on top and sounding

            const auto result = stack.noteOff(60); // release A (not the one sounding)
            expect(result.action == Action::NoChange,
                   "A wasn't the sounding note, so releasing it must not touch B at all");
        }

        beginTest("Three-deep stack: releasing the top falls back to the next-most-recent, not the oldest");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.1f); // A
            stack.noteOn(64, 0.2f); // B
            stack.noteOn(67, 0.3f); // C, on top

            auto result = stack.noteOff(67); // release C (sounding)
            expect(result.action == Action::Retrigger);
            expect(result.event.note == 64, "should fall back to B (the next most recent), not A");

            result = stack.noteOff(64); // release B (now sounding)
            expect(result.action == Action::Retrigger);
            expect(result.event.note == 60, "should fall back to A, the only one left");

            result = stack.noteOff(60); // release A (now sounding)
            expect(result.action == Action::Release, "nothing left held");
        }

        beginTest("A duplicate note-on for an already-held note moves it to the top, not a second entry");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.1f); // A
            stack.noteOn(64, 0.2f); // B
            stack.noteOn(60, 0.9f); // A pressed again (no note-off in between) - moves to top, velocity updates

            const auto result = stack.noteOff(60); // release A (currently on top/sounding)
            expect(result.action == Action::Retrigger);
            expect(result.event.note == 64, "B should be revealed - A must not have been duplicated below it");

            const auto result2 = stack.noteOff(64);
            expect(result2.action == Action::Release,
                   "nothing should remain - confirms A wasn't left behind as a stale duplicate");
        }

        beginTest("noteOff() for a note that isn't held is a safe no-op, doesn't disturb the stack");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);
            const auto result = stack.noteOff(99); // never held, and not the sounding note either
            expect(result.action == Action::NoChange, "the actually-sounding note (60) must be left alone");
        }

        beginTest("reset() clears all held notes");
        {
            using Action = StrikeMonoNoteStack<8>::NoteOffResult::Action;
            StrikeMonoNoteStack<8> stack;
            stack.noteOn(60, 0.5f);
            stack.noteOn(64, 0.5f);
            stack.reset();
            expect(stack.empty());
            const auto result = stack.noteOff(60);
            expect(result.action == Action::NoChange, "reset should have cleared everything - 60 isn't sounding any more");
        }

        beginTest("Exceeding MaxNotes drops the oldest held note rather than overflowing");
        {
            using Action = StrikeMonoNoteStack<2>::NoteOffResult::Action;
            StrikeMonoNoteStack<2> stack;
            stack.noteOn(60, 0.1f); // oldest, will be dropped
            stack.noteOn(64, 0.2f);
            stack.noteOn(67, 0.3f); // pushes out note 60

            const auto result = stack.noteOff(60); // no longer tracked, and wasn't sounding - safe no-op
            expect(result.action == Action::NoChange, "67 is the one sounding and must be left alone");
        }
    }
};

static StrikeMonoNoteStackTests strikeMonoNoteStackTests;
