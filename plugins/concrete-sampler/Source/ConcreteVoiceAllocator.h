#pragma once

#include <algorithm>
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
    // refinement, not implemented here). Marks the returned slot as belonging to midiNoteNumber/
    // zoneIndex/chokeGroup - the caller is responsible for actually calling noteOn() on the real
    // Voice at that index.
    //
    // activeVoiceLimit is Phase 6's per-preset polyphony cap (Architecture #1 - "running out of
    // voices was an audible, characteristic part of playing these machines, so it's emulation, not
    // a limitation to design around"): clamped to [1, MaxVoices]. Enforced by comparing the TOTAL
    // number of currently-active voices against the limit, not by restricting which INDICES are
    // considered - free-slot and steal searches always scan the whole pool. An earlier version of
    // this restricted both searches to indices [0, limit), which had a real, reported bug: if the
    // limit was ever higher when a voice landed in a high-index slot (e.g. the default of 8,
    // before the user turned Voice Count down), lowering the limit afterward made that slot
    // permanently invisible to future stealing - it could never be reclaimed again no matter how
    // many more notes were played, silently letting the audible voice count exceed the configured
    // limit for as long as that orphaned voice kept sounding (reported as "1-4 chokes properly,
    // but after 5 it's as if I can play as many notes as I want" - by the time Voice Count reached
    // 5, one or more voices had already been stranded above that window from an earlier, higher
    // setting). Comparing the total active count instead means every voice in the pool stays
    // reachable by stealing regardless of index, so lowering the limit can never strand one.
    int allocateVoiceForNoteOn(int midiNoteNumber, int zoneIndex, int chokeGroup,
                                const std::array<bool, MaxVoices>& isActive,
                                int activeVoiceLimit) noexcept
    {
        const auto limit = std::clamp(activeVoiceLimit, 1, MaxVoices);
        const auto activeCount = std::count(isActive.begin(), isActive.end(), true);

        int chosen = -1;
        if (activeCount < limit)
        {
            for (int i = 0; i < MaxVoices; ++i)
            {
                if (!isActive[(size_t) i])
                {
                    chosen = i;
                    break;
                }
            }
        }

        if (chosen < 0)
        {
            // At or over budget (or, degenerately, no free slot existed at all) - steal the
            // globally oldest ACTIVE voice, wherever it is. Falls back to slot 0 if nothing is
            // active yet (activeCount == 0 but limit's clamp guarantees limit >= 1, so this only
            // happens if the caller's isActive array is all-false while also reporting
            // activeCount >= limit, which can't occur - kept only as a defensive, unreachable-in-
            // practice fallback rather than leaving chosen at -1).
            for (int i = 0; i < MaxVoices; ++i)
                if (isActive[(size_t) i] && (chosen < 0 || voiceAge[(size_t) i] < voiceAge[(size_t) chosen]))
                    chosen = i;
            if (chosen < 0)
                chosen = 0;
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
    // returns an all-false mask (0 means "no choke", per Architecture #1) - v1's UI never assigns a
    // non-zero chokeGroup to any zone (that's a Phase 8 zone-editing control), so this is exercised
    // by unit tests and by ConcreteProcessorTests's end-to-end Phase 6 choke test, which builds a
    // two-zone set by hand rather than through the normal one-zone-only load flow.
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
