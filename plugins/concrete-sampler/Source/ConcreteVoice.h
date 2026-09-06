#pragma once

#include "ConcretePitchEngine.h"
#include "ConcreteQuantizer.h"
#include "ConcreteSampleSet.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

// One note's playback: a pitch engine (Phase 1's reference interpolation, or one of Phase 2's
// three machine modes - see ConcretePitchEngine.h), Phase 3's quantizer (see ConcreteQuantizer.h),
// and a basic ADSR amp envelope. The pitch engine, base rate, coarse/fine tune, and quantizer
// mode/bit depth are all snapshotted once at startNote() and held fixed for the voice's lifetime,
// the same way Phase 1 already snapshots the zone/velocity - matches this catalog's precedent
// (Strike's Mono/Topology switches) for "a mode selection is captured at note-on, not smoothly
// live-updated mid-note" rather than introducing a second, inconsistent live-parameter convention
// just for pitch/quantization.
//
// Holds a ConcreteSampleSet::Ptr (not just a pointer to the one zone it's playing) for the whole
// note, so a background sample reload mid-note can't invalidate the buffer this voice is reading
// from - see concrete-sampler-plugin-plan.md's Architecture #2 ("voices hold a ref to the set they
// started on"). ReferenceCountedObjectPtr's copy is a single atomic increment, not a lock, so this
// is safe to do from the audio thread.
class ConcreteVoice
{
public:
    void prepare(double sampleRateIn) noexcept;

    // zoneIndex must be a valid index into set->zones - the caller (PluginProcessor) is
    // responsible for having already resolved it via ConcreteSampleSet::lookup(). coarseTune is in
    // semitones, fineTune in cents - both combine with the zone's own tuneSemitones and the
    // played note's distance from the zone's root note into one total pitch ratio.
    // effectiveSourceRateHz is the zone's own sourceSampleRate for Mode::reference, or the
    // baseRate parameter for the three machine modes (see ConcretePitchEngine.h's header comment
    // on why those are different things and which one applies when). quantizerMode/bitDepthBits
    // are Phase 3's quantization stage (see ConcreteQuantizer.h) - captured at note-on like the
    // pitch engine mode, not smoothly live-updated mid-note.
    void startNote(ConcreteSampleSet::Ptr set, int zoneIndex, int midiNote, float velocity01,
                    ConcretePitchEngine::Mode mode, double effectiveSourceRateHz,
                    int coarseTuneSemitones, float fineTuneCents,
                    ConcreteQuantizer::Mode quantizerMode, int bitDepthBits) noexcept;

    // Matches the juce::SynthesiserVoice convention this catalog's other instruments already
    // follow (Strike, Alloy): allowTailOff true lets the ADSR release play out; false silences
    // immediately (a hard stop - Phase 1's "basic" envelope doesn't soften this with its own
    // fade, unlike a real note release).
    void stopNote(bool allowTailOff) noexcept;

    bool isActive() const noexcept { return active; }
    int getCurrentMidiNote() const noexcept { return currentMidiNote; }

    // The zone's output destination (for ConcreteBusRouter) - 0 (main bus) if no zone is
    // currently assigned. See PluginProcessor::processBlock() for how this is used.
    int getOutputDestination() const noexcept { return zone != nullptr ? zone->output : 0; }

    // Adds this voice's output into outputBuffer starting at startSample, for numSamples - adds
    // rather than overwrites, so multiple voices can be summed by the caller. Channel-count-aware
    // per concrete-sampler-plugin-plan.md's Architecture #1 (all four mono/stereo source-vs-output
    // combinations must render with no dropped or duplicated channel): a mono zone replicates to
    // every output channel, a stereo zone reads L/R independently into a stereo output, and a
    // stereo zone reads as an averaged mono mixdown into a mono output (not just its left channel
    // - dropping the right channel there would fail exactly the case the plan calls out).
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) noexcept;

private:
    double sampleRate = 44100.0;

    ConcreteSampleSet::Ptr sampleSet; // keeps zone->buffer alive for the whole note
    const ConcreteSampleZone* zone = nullptr;
    int currentMidiNote = -1;
    bool active = false;

    // One engine per possible zone channel (max 2 - see Architecture #1's mono/stereo
    // requirement). Fed identical control parameters, so their getSourcePhase() stays in
    // lockstep without needing to be threaded through externally - see ConcretePitchEngine.h.
    std::array<ConcretePitchEngine, 2> pitchEngines;
    ConcretePitchEngine::Mode pitchMode = ConcretePitchEngine::Mode::reference;
    double pitchRatio = 1.0;
    double effectiveSourceRateHz = 44100.0;
    juce::int64 zoneEndSample = 0;

    ConcreteQuantizer::Mode quantizerMode = ConcreteQuantizer::Mode::linear;
    int bitDepthBits = 16;

    float velocityGain = 1.0f;
    juce::ADSR adsr;
};
