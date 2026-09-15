#pragma once

#include "ConcreteSampleSet.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

// File loading and session persistence for sample zones - see concrete-sampler-plugin-plan.md's
// Architecture #2. Not real-time safe (file I/O, FLAC encode/decode, allocation) - every function
// here is meant to be called from a background/message thread in the real plugin, or synchronously
// from tooling (RenderIR, tests) where there is no audio thread to protect; never from
// processBlock().
//
// The zone <-> ValueTree functions are deliberately separate from PluginProcessor so the embed/cap
// logic and the state round-trip are directly testable (ConcreteTests) without spinning up a real
// AudioProcessor.
namespace ConcreteSampleIO
{
    // Embedding caps from Architecture #2, applied to the POST-COMPRESSION (FLAC) byte size, not
    // the raw or base64-inflated size.
    constexpr juce::int64 maxEmbeddedBytesPerZone = 20LL * 1024 * 1024;
    constexpr juce::int64 maxEmbeddedBytesPerInstance = 100LL * 1024 * 1024;

    // Reads an entire audio file into a new zone spanning the whole keyboard (v1's only shape -
    // see Architecture #1). On failure (unreadable/unsupported file), returns a zone with
    // sourceMissing = true and no buffer, with sourcePath still set.
    ConcreteSampleZone loadZoneFromFile(juce::AudioFormatManager& formatManager, const juce::File& file,
                                         int rootNote = 60);

    // Re-reads sourcePath from a new location (the user relocating a missing file - Architecture
    // #2), preserving every other field (key/velocity range, tune/level/pan, chokeGroup, output,
    // oneShot). Start/end/loop points reset to the new file's full length, since the old points
    // were positions within different audio data.
    ConcreteSampleZone relocateZone(const ConcreteSampleZone& existing, juce::AudioFormatManager& formatManager,
                                     const juce::File& newFile);

    // FLAC-encodes a zone's buffer into memory (24-bit, JUCE's default quality index). Returns an
    // empty MemoryBlock if the buffer is null/empty or its sample rate isn't one FLAC supports -
    // callers must treat an empty result as "can't embed, fall back to path-only", not an error.
    juce::MemoryBlock encodeZoneAsFlac(const ConcreteSampleZone& zone);

    // Decodes FLAC bytes back into a buffer + its sample rate. Returns false on failure (including
    // an empty/malformed block).
    bool decodeFlacBlock(const juce::MemoryBlock& flacBytes, juce::AudioBuffer<float>& outBuffer, double& outSampleRate);

    // Architecture #2's embed decision: embed if forceEmbed is set (bypasses both caps entirely -
    // that's the whole point of the "Embed samples in session" override), otherwise only if under
    // both the per-zone cap and the remaining per-instance budget.
    bool shouldEmbed(juce::int64 flacByteSize, juce::int64 alreadyEmbeddedBytesInInstance, bool forceEmbed) noexcept;

    // One <ZONE> element for a zone at the given index. alreadyEmbeddedBytesInInstance is both
    // read (for the cap decision) and updated with this zone's contribution if it gets embedded -
    // see sampleSetToValueTree, which owns the running total across a whole zone list.
    juce::ValueTree zoneToValueTree(const ConcreteSampleZone& zone, int index, bool forceEmbed,
                                     juce::int64& alreadyEmbeddedBytesInInstance);

    // The inverse: rebuilds a zone from a <ZONE> element. Embedded audio (if present) is decoded
    // directly; otherwise sourcePath is (re)read from disk. If neither works, returns a zone with
    // sourceMissing = true and no buffer - every other field still loads correctly (Architecture
    // #2: "everything else about the instance still loads").
    ConcreteSampleZone valueTreeToZone(const juce::ValueTree& zoneTree, juce::AudioFormatManager& formatManager);

    // Whole-set convenience wrappers, used for PluginProcessor's <ZONES> child of apvts.state.
    juce::ValueTree sampleSetToValueTree(const ConcreteSampleSet& set, bool embedOverride);
    ConcreteSampleSet::Ptr valueTreeToSampleSet(const juce::ValueTree& zonesTree, juce::AudioFormatManager& formatManager);
}

namespace ConcreteZoneIDs
{
    inline const juce::Identifier zones { "ZONES" };
    inline const juce::Identifier zone { "ZONE" };
    inline const juce::Identifier embedOverride { "embedOverride" };
    inline const juce::Identifier index { "index" };
    inline const juce::Identifier path { "path" };
    inline const juce::Identifier root { "root" };
    inline const juce::Identifier keyLo { "keyLo" };
    inline const juce::Identifier keyHi { "keyHi" };
    inline const juce::Identifier velLo { "velLo" };
    inline const juce::Identifier velHi { "velHi" };
    inline const juce::Identifier start { "start" };
    inline const juce::Identifier end { "end" };
    inline const juce::Identifier loopStart { "loopStart" };
    inline const juce::Identifier loopEnd { "loopEnd" };
    inline const juce::Identifier loopEnabled { "loopEnabled" };
    inline const juce::Identifier reverse { "reverse" };
    inline const juce::Identifier oneShot { "oneShot" };
    inline const juce::Identifier tune { "tune" };
    inline const juce::Identifier fineTune { "fineTune" };
    inline const juce::Identifier level { "level" };
    inline const juce::Identifier pan { "pan" };
    inline const juce::Identifier chokeGroup { "chokeGroup" };
    inline const juce::Identifier output { "output" };
    inline const juce::Identifier sourceSampleRate { "sourceSampleRate" };
    inline const juce::Identifier embeddedAudio { "embeddedAudio" };
}
