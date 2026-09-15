#pragma once

#include "ConcreteSampleZone.h"

#include <juce_core/juce_core.h>

#include <limits>
#include <vector>

// The zone list a ConcreteAudioProcessor plays through - see concrete-sampler-plugin-plan.md's
// Architecture #1: this ONE structure expresses a single repitched sample (v1: one zone spanning
// the whole keyboard), a kit (many zones, one note each), or a multisampled instrument (many
// zones spread across the keyboard). There is no separate "mode" - only how many zones this list
// holds and what key ranges they cover.
//
// Reference-counted so the audio thread and whatever thread loads a new sample can safely swap
// the whole zone list without the audio thread blocking or an in-flight voice's buffer being
// invalidated mid-note (see PluginProcessor's currentSampleSet/sampleSetLock).
// Note: Ptr is a ReferenceCountedObjectPtr<ConcreteSampleSet>, not <const ConcreteSampleSet> -
// juce::ReferenceCountedObject's incReferenceCount()/decReferenceCountWithoutDeleting() aren't
// const, so a const-templated pointer won't compile against it. Treat every published
// ConcreteSampleSet as immutable by CONVENTION instead: once handed to a voice or swapped into
// PluginProcessor's currentSampleSet, nothing may mutate it - only ever replace the whole pointer
// with a newly-built instance (see PluginProcessor's publish path).
class ConcreteSampleSet : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<ConcreteSampleSet>;

    std::vector<ConcreteSampleZone> zones;

    // Index of the NARROWEST-ranged zone whose key/velocity range contains (midiNote, velocity),
    // or -1 if none matches - "most specific mapping wins", the standard convention when a sampler
    // lets zones overlap (Kontakt/EXS24/etc). Ties (equal range size) go to the first (lowest-
    // index) match - see ConcreteSampleSetTests.cpp's "returns the FIRST matching zone when ranges
    // overlap" for the pinned case this preserves. This is what lets a per-pad override (Phase 8's
    // per-pad sample loading - see PluginProcessor::assignSampleToPad(), a single-note keyLo==keyHi
    // zone) take priority over the v1 main zone's whole-keyboard range for that one note, while
    // leaving v1's own single-zone behavior (only one candidate, so this is trivially satisfied)
    // and every existing multi-zone test (non-overlapping kick/snare-style ranges, where each note
    // still only has exactly one candidate) completely unchanged.
    int lookup(int midiNote, int velocity) const noexcept
    {
        int bestIndex = -1;
        int bestRangeSize = std::numeric_limits<int>::max();
        for (int i = 0; i < (int) zones.size(); ++i)
        {
            const auto& zone = zones[(size_t) i];
            if (!zone.matches(midiNote, velocity))
                continue;
            const auto rangeSize = (zone.keyHi - zone.keyLo + 1) * (zone.velHi - zone.velLo + 1);
            if (rangeSize < bestRangeSize)
            {
                bestRangeSize = rangeSize;
                bestIndex = i;
            }
        }
        return bestIndex;
    }
};
