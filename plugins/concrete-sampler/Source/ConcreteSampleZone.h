#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <memory>

// One playable region within a ConcreteSampleSet (see that file). A v1 instance always has
// exactly one zone spanning the whole keyboard; multi-zone kits/multisampled instruments (see
// concrete-sampler-plugin-plan.md's Architecture #1) are the same struct repeated with different
// key ranges - there is no separate "mode".
//
// Every field here is either loaded straight from disk (buffer, sourcePath) or is part of the
// zone-list state schema Architecture #1 specifies (root/keyLo/keyHi/velLo/velHi/start/end/
// loopStart/loopEnd/loopEnabled/reverse/tune/level/pan/chokeGroup/output) - persisted in
// PluginProcessor's zones ValueTree, never as APVTS parameters (see that section for why).
// reverse/loopEnabled/chokeGroup/output are schema-complete but not yet acted on by ConcreteVoice
// in Phase 1: there's no UI yet to set loop points (that's Phase 8) or a reason to use choke
// groups/non-zero output destinations (Phase 6/multi-zone work) or reverse playback.
struct ConcreteSampleZone
{
    // Shared (not copied) with any voice currently playing it, so a future capture-pass re-bake
    // (Phase 4) can swap this pointer without invalidating an in-flight voice's read position -
    // see ConcreteSampleSet's own reference-counting for the analogous whole-zone-list swap.
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;

    juce::String sourcePath;      // always stored, even when embedded (Architecture #2)
    bool sourceMissing = false;   // true when sourcePath no longer resolves and there's no embedded copy

    // The buffer's own native rate (from AudioFormatReader::sampleRate at load time), NOT
    // necessarily the host's processing rate. Not one of Architecture #1's enumerated schema
    // attributes, but required for correct pitch: without it, a file recorded at a different rate
    // than the current session would play back at the wrong speed/pitch. ConcreteVoice's
    // phaseIncrement compensates using this against its own prepare()'d sample rate.
    double sourceSampleRate = 44100.0;

    int rootNote = 60;
    int keyLo = 0, keyHi = 127;
    int velLo = 0, velHi = 127;

    juce::int64 start = 0;
    juce::int64 end = 0;          // exclusive; set to the buffer's length on load
    juce::int64 loopStart = 0;
    juce::int64 loopEnd = 0;
    bool loopEnabled = false;
    bool reverse = false;

    float tuneSemitones = 0.0f;
    float level = 1.0f;
    float pan = 0.0f;              // -1 (left) .. +1 (right)
    int chokeGroup = 0;            // 0 = no choke (Phase 6 tests this; unused in v1)
    int output = 0;                // destination index for ConcreteBusRouter; v1 always 0

    bool matches(int midiNote, int velocity) const noexcept
    {
        return midiNote >= keyLo && midiNote <= keyHi && velocity >= velLo && velocity <= velHi;
    }
};
