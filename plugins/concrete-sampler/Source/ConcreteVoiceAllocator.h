#pragma once

#include <array>
#include <cstdint>

// Voice-to-MIDI-note bookkeeping for a fixed-size voice pool: which voice slot a new note-on
// should use, and which slot a note-off should target. Deliberately separate from ConcreteVoice -
// this is pure allocation policy, not signal processing, and has no notion of audio/DSP at all.
// Framework-free (no JUCE include, just <array>/<cstdint>), matching this catalog's convention for
// isolating non-polymorphic DSP-adjacent logic (see plugins/strike-synth/Source/
// StrikeVoiceAllocator.h, which this is adapted from).
//
// Two things Strike's allocator didn't need, which this one carries from day one per
// concrete-sampler-plugin-plan.md's Architecture #1, even though v1 never exercises either:
//   - a zoneIndex per allocated voice, since a note-on has to know which zone in a
//     ConcreteSampleSet it's playing (v1 has exactly one zone, but a kit/multisample doesn't);
//   - a chokeGroup per allocated voice (0 = no choke), so a kit's closed-hat-cuts-open-hat
//     behavior is a getChokeMask() query away rather than an allocator API change. Phase 6 is
//     where a zone first sets chokeGroup != 0 and where this gets real test coverage.
//
// MaxVoices is a compile-time constant (not a constructor argument) specifically so every member
// is a fixed-size std::array - this class never allocates, on construction or on any call, which
// matters because allocateVoiceForNoteOn()/findVoiceForNoteOff() are called from
// PluginProcessor's MIDI dispatch on the audio thread.
template <int MaxVoices>
class ConcreteVoiceAllocator
{
public:
    ConcreteVoiceAllocator() noexcept { reset(); }

    void reset() noexcept
    {
        voiceMidiNote.fill(-1);
        voiceZoneIndex.fill(-1);
        voiceChokeGroup.fill(0);
        voiceAge.fill(0);
        nextAge = 0;
    }

    // Picks a voice for a new note-on: prefers any voice not currently sounding (per the
    // caller-supplied isActive flags - this class has no way to know that itself, since it
    // doesn't touch the real Voice objects). If every voice is active, steals the OLDEST
    // triggered voice (basic oldest-voice-stealing, not release-aware - a reasonable future
    // refinement, formally owned by Phase 6, not implemented here). Marks the returned slot as
    // belonging to midiNoteNumber/zoneIndex/chokeGroup - the caller is responsible for actually
    // calling noteOn() on the real Voice at that index.
    int allocateVoiceForNoteOn(int midiNoteNumber, int zoneIndex, int chokeGroup,
                                const std::array<bool, MaxVoices>& isActive) noexcept
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
        voiceZoneIndex[(size_t) chosen] = zoneIndex;
        voiceChokeGroup[(size_t) chosen] = chokeGroup;
        voiceAge[(size_t) chosen] = nextAge++;
        return chosen;
    }

    // Finds which voice a note-off should target: the MOST RECENTLY allocated voice still tagged
    // with this note number (handles rapid-retrigger - the same note struck twice without an
    // intervening note-off ends up on two different voices; a single note-off should release the
    // newer one, not both). Returns -1 if no voice is currently tagged with this note. Clears
    // that slot's tags either way - the caller is responsible for actually calling noteOff() on
    // the real Voice at that index.
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
        {
            voiceMidiNote[(size_t) bestIndex] = -1;
            voiceZoneIndex[(size_t) bestIndex] = -1;
            voiceChokeGroup[(size_t) bestIndex] = 0;
        }

        return bestIndex;
    }

    // Indices of every currently-active voice sharing the given choke group. Group 0 always
    // returns an all-false mask (0 means "no choke", per Architecture #1) - v1 never assigns a
    // non-zero chokeGroup to any zone, so this is exercised only by unit tests until Phase 6.
    std::array<bool, MaxVoices> getChokeMask(int chokeGroup, const std::array<bool, MaxVoices>& isActive) const noexcept
    {
        std::array<bool, MaxVoices> mask{};
        if (chokeGroup != 0)
            for (int i = 0; i < MaxVoices; ++i)
                mask[(size_t) i] = isActive[(size_t) i] && voiceChokeGroup[(size_t) i] == chokeGroup;
        return mask;
    }

private:
    std::array<int, MaxVoices> voiceMidiNote{};
    std::array<int, MaxVoices> voiceZoneIndex{};
    std::array<int, MaxVoices> voiceChokeGroup{};
    std::array<uint32_t, MaxVoices> voiceAge{};
    uint32_t nextAge = 0;
};
