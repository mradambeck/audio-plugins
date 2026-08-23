#pragma once

#include <array>

// Classic last-note-priority mono note stack: tracks currently-held notes in press order so a
// single voice always sounds the MOST RECENTLY pressed held note, and - the behavior this class
// exists for - releasing that note while an earlier one is still held RETRIGGERS the earlier note
// (hold A, hold B, release B -> A re-plucks), rather than just letting it continue ringing
// silently. Framework-free (no JUCE include, just <array>), matching KarplunkVoiceAllocator.h's
// convention for isolating non-DSP MIDI-adjacent policy so it's independently testable. MaxNotes
// is a compile-time constant so every member is a fixed-size std::array - never allocates, since
// PluginProcessor calls this from the audio thread's MIDI dispatch.
template <int MaxNotes>
class KarplunkMonoNoteStack
{
public:
    struct NoteEvent
    {
        int note = -1;
        float velocity01 = 0.0f;
    };

    void reset() noexcept { size = 0; }

    bool empty() const noexcept { return size == 0; }

    // Always the note+velocity the caller should (re)trigger - a mono voice retriggers on every
    // noteOn() no differently than a poly voice does (see SingleLineKarplunkVoice::noteOn()'s own
    // comment: "each MIDI note-on is physically a fresh pluck"). If this note is already held
    // (a duplicate note-on with no intervening note-off), it moves to the top instead of
    // duplicating - there's still only ever one held instance of a given note number.
    NoteEvent noteOn(int note, float velocity01) noexcept
    {
        removeIfPresent(note);
        if (size >= MaxNotes)
            removeAt(0); // drop the oldest to make room - should not happen with human playing,
                         // but must stay well-defined (not UB) if it somehow does.
        notes[(size_t) size] = note;
        velocities[(size_t) size] = velocity01;
        ++size;
        return { note, velocity01 };
    }

    struct NoteOffResult
    {
        // true: another held note should now sound (retrigger `event`). false: no notes remain
        // held (the caller should release the voice via its own noteOff()).
        bool stillHeld = false;
        NoteEvent event;
    };

    NoteOffResult noteOff(int note) noexcept
    {
        removeIfPresent(note);
        if (size == 0)
            return { false, {} };
        return { true, { notes[(size_t) size - 1], velocities[(size_t) size - 1] } };
    }

private:
    void removeIfPresent(int note) noexcept
    {
        for (int i = 0; i < size; ++i)
        {
            if (notes[(size_t) i] == note)
            {
                removeAt(i);
                return;
            }
        }
    }

    // Shifts everything above `index` down by one slot, preserving press order.
    void removeAt(int index) noexcept
    {
        for (int i = index; i < size - 1; ++i)
        {
            notes[(size_t) i] = notes[(size_t) i + 1];
            velocities[(size_t) i] = velocities[(size_t) i + 1];
        }
        --size;
    }

    std::array<int, MaxNotes> notes{};
    std::array<float, MaxNotes> velocities{};
    int size = 0;
};
