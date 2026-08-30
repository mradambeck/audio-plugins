#include "../StrikeVoiceAllocator.h"

#include <juce_core/juce_core.h>

// Note on all tests below: isActive flags must be updated by the test itself after each
// allocation, exactly as the real caller (PluginProcessor, querying the real Voice objects'
// isActive()) would - the allocator has no way to know a voice became active on its own, since
// it never touches the real Voice objects at all (see class comment). A static, never-updated
// isActive array would make every call see the same (stale) state, which is a caller bug, not
// an allocator bug - found exactly this mistake while first writing these tests.
class StrikeVoiceAllocatorTests : public juce::UnitTest
{
public:
    StrikeVoiceAllocatorTests() : juce::UnitTest("StrikeVoiceAllocator", "Strike") {}

    void runTest() override
    {
        beginTest("Allocates free (inactive) voices in order before stealing anything");
        {
            StrikeVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(60, isActive);
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(64, isActive);
            isActive[(size_t) v1] = true;
            const auto v2 = allocator.allocateVoiceForNoteOn(67, isActive);
            isActive[(size_t) v2] = true;

            expect(v0 == 0);
            expect(v1 == 1);
            expect(v2 == 2);
        }

        beginTest("Skips voices already reported active, even if not tagged with a note yet");
        {
            StrikeVoiceAllocator<4> allocator;
            std::array<bool, 4> voice0Active{true, false, false, false};

            const auto chosen = allocator.allocateVoiceForNoteOn(60, voice0Active);
            expect(chosen != 0, "should not choose a voice reported as already active");
        }

        beginTest("Steals the oldest-triggered voice once every voice is active");
        {
            StrikeVoiceAllocator<3> allocator;
            std::array<bool, 3> isActive{false, false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(10, isActive); // oldest
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(20, isActive);
            isActive[(size_t) v1] = true;
            const auto v2 = allocator.allocateVoiceForNoteOn(30, isActive); // newest
            isActive[(size_t) v2] = true;

            // Every voice is now active - a new note-on must steal, and the oldest (v0,
            // triggered first) should be the one stolen.
            const auto stolen = allocator.allocateVoiceForNoteOn(40, isActive);
            expect(stolen == v0, "should steal the oldest-triggered voice, not an arbitrary one");
        }

        beginTest("A stolen voice becomes the newest, so repeated stealing cycles through voices");
        {
            StrikeVoiceAllocator<2> allocator;
            std::array<bool, 2> isActive{false, false};

            const auto v0 = allocator.allocateVoiceForNoteOn(1, isActive);
            isActive[(size_t) v0] = true;
            const auto v1 = allocator.allocateVoiceForNoteOn(2, isActive);
            isActive[(size_t) v1] = true;

            // Both voices active - stealing must pick v0 (oldest).
            const auto steal1 = allocator.allocateVoiceForNoteOn(3, isActive);
            expect(steal1 == v0, "the oldest voice should be stolen first");

            // v0 was just re-triggered (now the newest), v1 is now the oldest - the next steal
            // should pick v1, not v0 again.
            const auto steal2 = allocator.allocateVoiceForNoteOn(4, isActive);
            expect(steal2 == v1, "the other voice is now the oldest and should be stolen next");
        }

        beginTest("findVoiceForNoteOff() returns the voice tagged with that note, or -1");
        {
            StrikeVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto voiceForNote60 = allocator.allocateVoiceForNoteOn(60, isActive);
            const auto found = allocator.findVoiceForNoteOff(60);
            expect(found == voiceForNote60);

            const auto notFound = allocator.findVoiceForNoteOff(99);
            expect(notFound == -1, "a note with no matching voice should return -1");
        }

        beginTest("findVoiceForNoteOff() targets the most recently allocated voice on rapid retrigger");
        {
            StrikeVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            const auto first = allocator.allocateVoiceForNoteOn(60, isActive);
            isActive[(size_t) first] = true;
            // Same note again, no note-off in between - must land on a different voice since
            // the first is now reported active.
            const auto second = allocator.allocateVoiceForNoteOn(60, isActive);
            expect(first != second, "a second note-on for the same pitch without a note-off should use a different voice");

            const auto found = allocator.findVoiceForNoteOff(60);
            expect(found == second, "note-off should target the most recently triggered voice for that note");
        }

        beginTest("findVoiceForNoteOff() clears the tag so a later duplicate note-off finds nothing");
        {
            StrikeVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            allocator.allocateVoiceForNoteOn(60, isActive);
            allocator.findVoiceForNoteOff(60);
            const auto secondCall = allocator.findVoiceForNoteOff(60);
            expect(secondCall == -1, "a duplicate note-off for an already-released note should find nothing");
        }

        beginTest("reset() clears all note tags and ages");
        {
            StrikeVoiceAllocator<4> allocator;
            std::array<bool, 4> isActive{false, false, false, false};

            allocator.allocateVoiceForNoteOn(60, isActive);
            allocator.reset();

            expect(allocator.findVoiceForNoteOff(60) == -1, "reset() should clear existing note tags");
        }
    }
};

static StrikeVoiceAllocatorTests strikeVoiceAllocatorTests;
