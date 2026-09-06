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
// loopStart/loopEnd/loopEnabled/reverse/tune/level/pan/chokeGroup/output), plus oneShot (added
// after Phase 5, same non-automatable "zone-list state, not an APVTS parameter" category as the
// rest of this list - a real per-machine-preset trait some drum-oriented samplers hardwired, not
// something anyone would automate mid-performance) - persisted in PluginProcessor's zones
// ValueTree, never as APVTS parameters (see that section for why). reverse/loopEnabled/chokeGroup/
// output are schema-complete but not yet acted on by ConcreteVoice in Phase 1: there's no UI yet
// to set loop points (that's Phase 8) or a reason to use choke groups/non-zero output destinations
// (Phase 6/multi-zone work) or reverse playback.
struct ConcreteSampleZone
{
    // The WORKING buffer - what ConcreteVoice actually plays. Phase 4's capture pass
    // (ConcreteCapturePass) derives this from sourceBuffer below; before Phase 4 (or with the
    // capture pass bypassed), it's an exact copy of sourceBuffer. Shared (not copied) with any
    // voice currently playing it, so a capture-pass re-bake can swap this pointer without
    // invalidating an in-flight voice's read position - see ConcreteSampleSet's own reference-
    // counting for the analogous whole-zone-list swap.
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;

    // The SOURCE buffer - exactly as loaded from disk or decoded from embedded FLAC, never
    // modified (see concrete-sampler-plugin-plan.md's Architecture #2: "the source buffer is
    // never modified"). ConcreteSampleIO reads/writes ONLY this field, never `buffer` - session
    // persistence/embedding is defined entirely in terms of the source, since the working buffer
    // is regenerated from it on load rather than persisted itself. ConcreteAudioProcessor is what
    // derives `buffer` from this + the current capture-pass settings after any load/relocate/
    // state-restore.
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;

    // How many total semitones ConcreteCapturePass actually baked into `buffer` the last time it
    // ran (0 if bypassed or not yet baked) - NOT necessarily the CURRENT capture-transpose
    // parameter value, since a parameter change and its background re-bake completing are not
    // atomic. ConcreteVoice::startNote() subtracts this (when auto-compensate is on) to bring a
    // resampled-up-for-capture buffer back to its original pitch/tempo at the zone's root note -
    // see concrete-sampler-plugin-plan.md's Phase 4.
    double captureTransposeSemitones = 0.0;

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

    // False (the existing, tested default): the zone plays only while the key/trigger is held,
    // stopping (with the amp envelope's release tail) on note-off - a "gated" sampler. True: a
    // note-on plays the whole zone through to its own end regardless of how long the key is held
    // or when note-off arrives - a "one-shot" sampler (the SP-1200/MPC drum-pad convention). See
    // ConcreteVoice::stopNote()'s isForced parameter for how choke groups still cut a one-shot
    // voice immediately even though an ordinary note-off can't.
    bool oneShot = false;

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
