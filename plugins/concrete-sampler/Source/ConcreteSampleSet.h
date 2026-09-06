#pragma once

#include "ConcreteSampleZone.h"

#include <juce_core/juce_core.h>

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

    // Index of the first zone whose key/velocity range contains (midiNote, velocity), or -1 if
    // none matches. v1 has exactly one zone spanning the whole keyboard, so this is trivially
    // satisfied - but it's a real lookup, not a hardcoded buffer access, so multi-zone kits are a
    // data change only (see Architecture #1).
    int lookup(int midiNote, int velocity) const noexcept
    {
        for (int i = 0; i < (int) zones.size(); ++i)
            if (zones[(size_t) i].matches(midiNote, velocity))
                return i;
        return -1;
    }
};
