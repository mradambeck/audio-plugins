#include "../ConcreteVoiceAllocator.h"

#include <juce_core/juce_core.h>

#include <algorithm>

// Adapted from plugins/strike-synth/Source/Tests/StrikeVoiceAllocatorTests.cpp - same allocation/
// stealing policy, plus coverage for the zoneIndex/chokeGroup bookkeeping Architecture #1 adds
// (see ConcreteVoiceAllocator.h's class comment for why those exist even though v1 never sets a
// non-zero chokeGroup).
//
// Note on all tests below: isActive flags must be updated by the test itself after each
// allocation, exactly as the real caller (PluginProcessor, querying the real Voice objects'
// isActive()) would - the allocator has no way to know a voice became active on its own, since it
// never touches the real Voice objects at all.
class ConcreteVoiceAllocatorTests : public juce::UnitTest
{
public:
    ConcreteVoiceAllocatorTests() : juce::UnitTest("ConcreteVoiceAllocator", "Concrete") {}

    void runTest() override
    {
        beginTest("Allocates free (inactive) voices in order before stealing anything");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(64, 0, 0, isActive, 4);
            isActive[(size_t) v1] = true;
            const auto v2 = allocator.allocateVoiceForNoteOn(67, 0, 0, isActive, 4);
            isActive[(size_t) v2] = true;

            expect(v0 == 0);
            expect(v1 == 1);
            expect(v2 == 2);
        }

        beginTest("Skips voices already reported active, even if not tagged with a note yet");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> voice0Active{true, false, false, false};

            const auto chosen = allocator.allocateVoiceForNoteOn(60, 0, 0, voice0Active, 4);
            expect(chosen != 0, "should not choose a voice reported as already active");
        }

        beginTest("Steals the oldest-triggered voice once every voice is active");
        {
            ConcreteVoiceAllocator<3> allocator;
            std::array<bool, 3> isActive{false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(10, 0, 0, isActive, 3); // oldest
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(20, 0, 0, isActive, 3);
            isActive[(size_t) v1] = true;
            const auto v2 = allocator.allocateVoiceForNoteOn(30, 0, 0, isActive, 3); // newest
            isActive[(size_t) v2] = true;

            const auto stolen = allocator.allocateVoiceForNoteOn(40, 0, 0, isActive, 3);
            expect(stolen == v0, "should steal the oldest-triggered voice, not an arbitrary one");
        }

        beginTest("A stolen voice becomes the newest, so repeated stealing cycles through voices");
        {
            ConcreteVoiceAllocator<2> allocator;
            std::array<bool, 2> isActive{false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(1, 0, 0, isActive, 2);
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(2, 0, 0, isActive, 2);
            isActive[(size_t) v1] = true;

            const auto steal1 = allocator.allocateVoiceForNoteOn(3, 0, 0, isActive, 2);
            expect(steal1 == v0, "the oldest voice should be stolen first");

            const auto steal2 = allocator.allocateVoiceForNoteOn(4, 0, 0, isActive, 2);
            expect(steal2 == v1, "the other voice is now the oldest and should be stolen next");
        }

        beginTest("findVoiceForNoteOff() returns the voice tagged with that note, or -1");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto voiceForNote60 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            const auto found = allocator.findVoiceForNoteOff(60);
            expect(found == voiceForNote60);

            const auto notFound = allocator.findVoiceForNoteOff(99);
            expect(notFound == -1, "a note with no matching voice should return -1");
        }

        beginTest("findVoiceForNoteOff() targets the most recently allocated voice on rapid retrigger");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto first = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            isActive[(size_t) first] = true;
            const auto second = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            expect(first != second, "a second note-on for the same pitch without a note-off should use a different voice");

            const auto found = allocator.findVoiceForNoteOff(60);
            expect(found == second, "note-off should target the most recently triggered voice for that note");
        }

        beginTest("findVoiceForNoteOff() clears the tag so a later duplicate note-off finds nothing");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            allocator.findVoiceForNoteOff(60);
            const auto secondCall = allocator.findVoiceForNoteOff(60);
            expect(secondCall == -1, "a duplicate note-off for an already-released note should find nothing");
        }

        beginTest("reset() clears all note tags and ages");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            allocator.reset();

            expect(allocator.findVoiceForNoteOff(60) == -1, "reset() should clear existing note tags");
        }

        beginTest("getChokeMask() always returns all-false for group 0 (Architecture #1: 0 = no choke)");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            isActive[(size_t) v0] = true;

            const auto mask = allocator.getChokeMask(0, isActive);
            expect(std::none_of(mask.begin(), mask.end(), [](bool b) { return b; }),
                   "group 0 must never choke anything, even an active voice");
        }

        beginTest("getChokeMask() finds every active voice sharing a non-zero choke group");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto openHat = allocator.allocateVoiceForNoteOn(46, 1, 5, isActive, 4);
            isActive[(size_t) openHat] = true;
            const auto other = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 4);
            isActive[(size_t) other] = true;

            const auto mask = allocator.getChokeMask(5, isActive);
            expect(mask[(size_t) openHat], "the voice in choke group 5 should be in the mask");
            expect(! mask[(size_t) other], "a voice in a different (or no) choke group should not be");
        }

        beginTest("getChokeMask() ignores voices that aren't currently active");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(46, 1, 5, isActive, 4);
            // Deliberately never marked active - simulates a voice slot that was allocated but
            // whose real Voice has already finished/released by the time of the query.

            const auto mask = allocator.getChokeMask(5, isActive);
            expect(! mask[(size_t) v0], "an inactive voice should not be reported as needing a choke");
        }

        // Phase 6: a per-preset voice-count limit lower than the pool's compile-time capacity (see
        // concrete-sampler-plugin-plan.md's Phase 6 "Voice count 4, six overlapping notes: exactly
        // 4 sound, stealing deterministic"). The allocator's own capacity here is 8, deliberately
        // larger than the 4-voice limit under test, so these tests actually exercise the limit
        // itself rather than the array bound.
        beginTest("Under the active-voice limit, a free slot anywhere in the pool may be used, not just low indices");
        {
            ConcreteVoiceAllocator<8> allocator;
            // Slots 0-3 already busy (simulating voices started under some earlier, higher
            // limit), slots 4-7 free. 4 active < a limit of 6, so this is still under budget.
            std::array<bool, 8> isActive{true, true, true, true, false, false, false, false};

            const auto v = allocator.allocateVoiceForNoteOn(90, 0, 0, isActive, 6);
            expect(v >= 4, "a free slot at a higher index must still be usable when under budget - "
                           "the limit is not an index window");
        }

        // Regression test for a real reported bug: "1-4 chokes properly, but after 5 it's as if I
        // can play as many notes as I want." Root cause: an earlier version of allocateVoiceForNoteOn()
        // restricted BOTH the free-slot search and the steal search to indices [0, activeVoiceLimit)
        // - once a voice landed in a high-index slot under a higher limit (e.g. the default of 8,
        // before Voice Count was turned down), lowering the limit made that slot permanently
        // invisible to future stealing, since the search window no longer reached it. That voice
        // then kept sounding forever regardless of how many more notes were played, silently
        // letting the audible voice count exceed the configured limit. The fix compares the TOTAL
        // active count against the limit and searches the WHOLE pool for both free slots and
        // steal candidates, so every voice stays reachable no matter its index.
        beginTest("Lowering the active-voice limit after voices already occupy high-index slots must "
                  "still allow those slots to be stolen (regression)");
        {
            ConcreteVoiceAllocator<8> allocator;
            std::array<bool, 8> isActive{};
            isActive.fill(false);

            // Fill all 8 slots while the limit is 8 - notes 60-67 land in slots 0-7 respectively.
            for (int i = 0; i < 8; ++i)
            {
                const auto v = allocator.allocateVoiceForNoteOn(60 + i, 0, 0, isActive, 8);
                isActive[(size_t) v] = true;
            }

            // The user now turns Voice Count down to 5 and keeps playing. Under the old, buggy
            // index-windowed search, slots 5/6/7 (notes 65/66/67) could never be selected again
            // once the window shrank below their index - trigger plenty more notes (more than the
            // full pool size, so a correct implementation is guaranteed to have cycled through
            // every originally-allocated slot at least once - see the "repeated stealing cycles
            // through voices" test above for why a strictly-increasing age counter guarantees
            // that) and confirm they eventually get reclaimed.
            for (int i = 0; i < 20; ++i)
            {
                const auto v = allocator.allocateVoiceForNoteOn(100 + i, 0, 0, isActive, 5);
                isActive[(size_t) v] = true; // the stolen slot is still sounding - now a new note
            }

            expect(allocator.findVoiceForNoteOff(65) == -1, "note 65 (originally stranded in slot 5) must eventually be stolen");
            expect(allocator.findVoiceForNoteOff(66) == -1, "note 66 (originally stranded in slot 6) must eventually be stolen");
            expect(allocator.findVoiceForNoteOff(67) == -1, "note 67 (originally stranded in slot 7) must eventually be stolen");

            // Lowering the limit isn't retroactive (nothing here ever released a voice, so the
            // pool never shrinks on its own) - the fix's guarantee is reachability, not an
            // instant forced cut. What matters is the count never GREW past what was already
            // sounding when the limit dropped.
            const auto totalActive = std::count(isActive.begin(), isActive.end(), true);
            expect(totalActive == 8, "stealing replaces one active voice with another - the total must not grow beyond the pre-existing 8");
        }

        beginTest("Six overlapping notes under a 4-voice limit: exactly 4 end up sounding, deterministically");
        {
            ConcreteVoiceAllocator<8> allocator;
            std::array<bool, 8> isActive{false, false, false, false, false, false, false, false};

            for (int i = 0; i < 6; ++i)
            {
                const auto v = allocator.allocateVoiceForNoteOn(60 + i, 0, 0, isActive, 4);
                isActive[(size_t) v] = true;
            }

            const auto soundingCount = std::count(isActive.begin(), isActive.end(), true);
            expect(soundingCount == 4, "no more than the configured limit should ever be marked active");

            // Deterministic oldest-first stealing: notes 60 and 61 (triggered first) are the two
            // that get stolen from, in that order, so the voices ending up tagged with 64 and 65
            // are exactly the ones that used to be tagged with 60 and 61. Notes 62/63 (never
            // stolen) and 64/65 (the survivors of the theft) are the four actually sounding.
            // findVoiceForNoteOff() both queries AND clears a tag, so each note is checked once.
            expect(allocator.findVoiceForNoteOff(60) == -1, "note 60 should have been stolen and no longer be tagged");
            expect(allocator.findVoiceForNoteOff(61) == -1, "note 61 should have been stolen and no longer be tagged");
            expect(allocator.findVoiceForNoteOff(62) != -1, "note 62 was never stolen and should still be sounding");
            expect(allocator.findVoiceForNoteOff(63) != -1, "note 63 was never stolen and should still be sounding");
            expect(allocator.findVoiceForNoteOff(64) != -1, "note 64 (the first thief) should still be sounding");
            expect(allocator.findVoiceForNoteOff(65) != -1, "note 65 (the second thief) should still be sounding");
        }

        beginTest("activeVoiceLimit is clamped to at least 1, even if given 0 or a negative value");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive, 0);
            expect(v0 == 0, "a limit of 0 should still allocate voice 0, not crash or return an out-of-range index");
            isActive[(size_t) v0] = true;

            const auto v1 = allocator.allocateVoiceForNoteOn(61, 0, 0, isActive, -3);
            expect(v1 == 0, "a negative limit should behave the same as a limit of 1 (steal the only allowed voice)");
        }

        beginTest("activeVoiceLimit is clamped to at most MaxVoices, even if given a larger value");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            for (int i = 0; i < 4; ++i)
            {
                const auto v = allocator.allocateVoiceForNoteOn(60 + i, 0, 0, isActive, 100);
                isActive[(size_t) v] = true;
            }
            expect(std::all_of(isActive.begin(), isActive.end(), [](bool b) { return b; }),
                   "an oversized limit should still only ever touch the real MaxVoices=4 slots");
        }
    }
};

static ConcreteVoiceAllocatorTests concreteVoiceAllocatorTests;
