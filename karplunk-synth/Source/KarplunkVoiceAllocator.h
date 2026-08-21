#pragma once

#include <array>
#include <cstdint>

// Voice-to-MIDI-note bookkeeping for a fixed-size voice pool: which voice slot a new note-on
// should use, and which slot a note-off should target. Deliberately separate from
// SingleLineKarplunkVoice (or any of the four experimental-area classes) - this is pure
// allocation policy, not signal processing, and has no notion of audio/DSP at all. Framework-free
// (no JUCE include, just <array>/<cstdint>) so it's independently testable, matching this
// catalog's convention for isolating non-polymorphic DSP-adjacent logic (see
// gradient-pitch/GradientDelayBuffer.h for the same pattern applied to a signal-processing class).
//
// MaxVoices is a compile-time constant (not a constructor argument) specifically so every member
// is a fixed-size std::array - this class never allocates, on construction or on any call, which
// matters because allocateVoiceForNoteOn()/findVoiceForNoteOff() are called from
// PluginProcessor's MIDI dispatch on the audio thread.
template <int MaxVoices>
class KarplunkVoiceAllocator
{
public:
    KarplunkVoiceAllocator() noexcept { reset(); }

    void reset() noexcept
    {
        voiceMidiNote.fill(-1);
        voiceAge.fill(0);
        nextAge = 0;
    }

    // Picks a voice for a new note-on: prefers any voice not currently sounding (per the
    // caller-supplied isActive flags - this class has no way to know that itself, since it
    // doesn't touch the real Voice objects). If every voice is active, steals the OLDEST
    // triggered voice (basic oldest-voice-stealing, not release-aware - a voice that's had its
    // note released isn't preferred over one still held down; that's a reasonable future
    // refinement, not implemented here to keep this pass simple). Marks the returned slot as
    // belonging to midiNoteNumber - the caller is responsible for actually calling noteOn() on
    // the real Voice at that index.
    int allocateVoiceForNoteOn(int midiNoteNumber, const std::array<bool, MaxVoices>& isActive) noexcept
    {
        int chosen = -1;
        for (int i = 0; i < MaxVoices; ++i)
        {
            if (!isActive[(size_t) i])
            {
                chosen = i;
                break;
            }
        }

        if (chosen < 0)
        {
            chosen = 0;
            for (int i = 1; i < MaxVoices; ++i)
                if (voiceAge[(size_t) i] < voiceAge[(size_t) chosen])
                    chosen = i;
        }

        voiceMidiNote[(size_t) chosen] = midiNoteNumber;
        voiceAge[(size_t) chosen] = nextAge++;
        return chosen;
    }

    // Finds which voice a note-off should target: the MOST RECENTLY allocated voice still
    // tagged with this note number (handles the rapid-retrigger case - the same note struck
    // twice without an intervening note-off ends up on two different voices; a single note-off
    // should release the newer one, not both). Returns -1 if no voice is currently tagged with
    // this note (a stray note-off, or the tagged voice has since been stolen by a later note-on).
    // Clears that slot's tag either way it's found - the caller is responsible for actually
    // calling noteOff() on the real Voice at that index.
    int findVoiceForNoteOff(int midiNoteNumber) noexcept
    {
        int bestIndex = -1;
        for (int i = 0; i < MaxVoices; ++i)
        {
            if (voiceMidiNote[(size_t) i] == midiNoteNumber
                && (bestIndex < 0 || voiceAge[(size_t) i] > voiceAge[(size_t) bestIndex]))
            {
                bestIndex = i;
            }
        }

        if (bestIndex >= 0)
            voiceMidiNote[(size_t) bestIndex] = -1;

        return bestIndex;
    }

private:
    std::array<int, MaxVoices> voiceMidiNote{};
    std::array<uint32_t, MaxVoices> voiceAge{};
    uint32_t nextAge = 0;
};
