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

            const auto v0 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(64, 0, 0, isActive);
            isActive[(size_t) v1] = true;
            const auto v2 = allocator.allocateVoiceForNoteOn(67, 0, 0, isActive);
            isActive[(size_t) v2] = true;

            expect(v0 == 0);
            expect(v1 == 1);
            expect(v2 == 2);
        }

        beginTest("Skips voices already reported active, even if not tagged with a note yet");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> voice0Active{true, false, false, false};

            const auto chosen = allocator.allocateVoiceForNoteOn(60, 0, 0, voice0Active);
            expect(chosen != 0, "should not choose a voice reported as already active");
        }

        beginTest("Steals the oldest-triggered voice once every voice is active");
        {
            ConcreteVoiceAllocator<3> allocator;
            std::array<bool, 3> isActive{false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(10, 0, 0, isActive); // oldest
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(20, 0, 0, isActive);
            isActive[(size_t) v1] = true;
            const auto v2 = allocator.allocateVoiceForNoteOn(30, 0, 0, isActive); // newest
            isActive[(size_t) v2] = true;

            const auto stolen = allocator.allocateVoiceForNoteOn(40, 0, 0, isActive);
            expect(stolen == v0, "should steal the oldest-triggered voice, not an arbitrary one");
        }

        beginTest("A stolen voice becomes the newest, so repeated stealing cycles through voices");
        {
            ConcreteVoiceAllocator<2> allocator;
            std::array<bool, 2> isActive{false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(1, 0, 0, isActive);
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(2, 0, 0, isActive);
            isActive[(size_t) v1] = true;

            const auto steal1 = allocator.allocateVoiceForNoteOn(3, 0, 0, isActive);
            expect(steal1 == v0, "the oldest voice should be stolen first");

            const auto steal2 = allocator.allocateVoiceForNoteOn(4, 0, 0, isActive);
            expect(steal2 == v1, "the other voice is now the oldest and should be stolen next");
        }

        beginTest("findVoiceForNoteOff() returns the voice tagged with that note, or -1");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto voiceForNote60 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            const auto found = allocator.findVoiceForNoteOff(60);
            expect(found == voiceForNote60);

            const auto notFound = allocator.findVoiceForNoteOff(99);
            expect(notFound == -1, "a note with no matching voice should return -1");
        }

        beginTest("findVoiceForNoteOff() targets the most recently allocated voice on rapid retrigger");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto first = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            isActive[(size_t) first] = true;
            const auto second = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            expect(first != second, "a second note-on for the same pitch without a note-off should use a different voice");

            const auto found = allocator.findVoiceForNoteOff(60);
            expect(found == second, "note-off should target the most recently triggered voice for that note");
        }

        beginTest("findVoiceForNoteOff() clears the tag so a later duplicate note-off finds nothing");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            allocator.findVoiceForNoteOff(60);
            const auto secondCall = allocator.findVoiceForNoteOff(60);
            expect(secondCall == -1, "a duplicate note-off for an already-released note should find nothing");
        }

        beginTest("reset() clears all note tags and ages");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            allocator.reset();

            expect(allocator.findVoiceForNoteOff(60) == -1, "reset() should clear existing note tags");
        }

        beginTest("getChokeMask() always returns all-false for group 0 (Architecture #1: 0 = no choke)");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            isActive[(size_t) v0] = true;

            const auto mask = allocator.getChokeMask(0, isActive);
            expect(std::none_of(mask.begin(), mask.end(), [](bool b) { return b; }),
                   "group 0 must never choke anything, even an active voice");
        }

        beginTest("getChokeMask() finds every active voice sharing a non-zero choke group");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto openHat = allocator.allocateVoiceForNoteOn(46, 1, 5, isActive);
            isActive[(size_t) openHat] = true;
            const auto other = allocator.allocateVoiceForNoteOn(60, 0, 0, isActive);
            isActive[(size_t) other] = true;

            const auto mask = allocator.getChokeMask(5, isActive);
            expect(mask[(size_t) openHat], "the voice in choke group 5 should be in the mask");
            expect(! mask[(size_t) other], "a voice in a different (or no) choke group should not be");
        }

        beginTest("getChokeMask() ignores voices that aren't currently active");
        {
            ConcreteVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(46, 1, 5, isActive);
            // Deliberately never marked active - simulates a voice slot that was allocated but
            // whose real Voice has already finished/released by the time of the query.

            const auto mask = allocator.getChokeMask(5, isActive);
            expect(! mask[(size_t) v0], "an inactive voice should not be reported as needing a choke");
        }
    }
};

static ConcreteVoiceAllocatorTests concreteVoiceAllocatorTests;
