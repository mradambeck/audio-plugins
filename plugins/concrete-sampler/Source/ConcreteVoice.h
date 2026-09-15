#pragma once

#include "ConcreteContourEnvelope.h"
#include "ConcreteFilterModels.h"
#include "ConcretePitchEngine.h"
#include "ConcreteSampleSet.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

// Phase 6's amp-envelope shape selector: adsr is the flat-sustain juce::ADSR every earlier phase
// used; contoured swaps in the K250-style continuously-decaying ConcreteContourEnvelope.h shape.
// A free enum rather than a mode nested inside either envelope class, since it picks between two
// entirely separate implementations rather than configuring one of them.
enum class ConcreteAmpEnvelopeMode { adsr, contoured };

// One note's playback: a pitch engine (Phase 1's reference interpolation, or one of Phase 2's
// three machine modes - see ConcretePitchEngine.h), Phase 5's playback-side filter (see
// ConcreteFilterModels.h), a filter envelope (fixed-shape ADSR), and an amp envelope that's either
// a fixed-shape ADSR or Phase 6's contoured shape (see ConcreteAmpEnvelopeMode above). Phase 3's
// quantizer (bit depth/
// companding - see ConcreteQuantizer.h) and Phase 4's capture pass (resample/drive - see
// ConcreteCapturePass.h) are BAKED into the zone's working buffer offline rather than applied live
// here (see concrete-sampler-plugin-plan.md's Architecture #2/#3 and Phase 4) - this class just
// plays back whatever is in that buffer, then filters it. The pitch engine, base rate, coarse/fine
// tune, and filter mode/cutoff/resonance/env-amount/key-track are all snapshotted once at
// startNote() and held fixed for the voice's lifetime, the same way Phase 1 already snapshots the
// zone/velocity - matches this catalog's precedent (Strike's Mono/Topology switches) for "a mode
// selection is captured at note-on, not smoothly live-updated mid-note" rather than introducing a
// second, inconsistent live-parameter convention just for pitch/filtering.
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
    // responsible for having already resolved it via ConcreteSampleSet::lookup(). The zone's own
    // tuneSemitones (Coarse, rounded to the nearest semitone - see startNote()'s own .cpp comment)
    // and fineTuneCents (Fine, in cents) combine with the played note's distance from the zone's
    // root note into one total pitch ratio - both are per-zone state (Architecture #1), not a
    // performer-facing global control, so tuning one sample can never bleed into another's pitch.
    // effectiveSourceRateHz is the zone's own sourceSampleRate for Mode::reference, or the
    // baseRate parameter for the three machine modes - used ONLY to shape artifact character for
    // those three (how coarse the zero-order hold/decimation is), never root-pitch playback speed,
    // which always tracks the zone's real sourceSampleRate regardless of mode (see
    // ConcretePitchEngine.h's header comment on processSample()'s fileSourceRateHz parameter, and
    // readModeA()'s own comment on the real bug this fixed). autoCompensate, when true,
    // subtracts zone->captureTransposeSemitones (whatever Phase 4's capture pass actually baked
    // into this zone's buffer, 0 if none) from the note's total pitch, bringing a resampled-up-
    // for-capture buffer back to its original pitch/tempo at the zone's root note - see
    // ConcreteCapturePass.h. false plays the baked-in pitch-up directly, uncompensated.
    // filterMode/filterCutoffHz/filterResonance01 select and configure Phase 5's playback filter
    // (see ConcreteFilterModels.h). filterEnvAmountOctaves is the filter envelope's modulation
    // depth in octaves (bipolar - negative sweeps the cutoff down instead of up); filterKeyTrack01
    // is 0 (no tracking) to 1 (cutoff scales with the played note's distance from C3 exactly like
    // pitch would). ampEnvelopeMode picks between the flat-sustain ADSR every earlier phase used
    // and Phase 6's K250-style continuously-decaying contour (see ConcreteAmpEnvelopeMode above).
    void startNote(ConcreteSampleSet::Ptr set, int zoneIndex, int midiNote, float velocity01,
                    ConcretePitchEngine::Mode mode, double effectiveSourceRateHz, bool autoCompensate,
                    ConcreteFilterModel::Mode filterMode, float filterCutoffHz, float filterResonance01,
                    float filterEnvAmountOctaves, float filterKeyTrack01,
                    ConcreteAmpEnvelopeMode ampEnvelopeMode) noexcept;

    // allowTailOff matches the juce::SynthesiserVoice convention this catalog's other instruments
    // already follow (Strike, Alloy): true lets the ADSR release play out; false silences
    // immediately (a hard stop - Phase 1's "basic" envelope doesn't soften this with its own fade,
    // unlike a real note release). isForced distinguishes a genuine, unconditional stop (voice
    // stealing, a choke group cutting this voice - always executes) from an ordinary note-off (the
    // key/trigger being released - a no-op if the zone's oneShot is set, since a one-shot zone
    // plays through to its own end regardless of how long the key was held; see
    // ConcreteSampleZone::oneShot). Every OTHER caller of stopNote() in this codebase besides a
    // literal note-off should pass isForced=true.
    void stopNote(bool allowTailOff, bool isForced) noexcept;

    bool isActive() const noexcept { return active; }
    int getCurrentMidiNote() const noexcept { return currentMidiNote; }
    int getZoneIndex() const noexcept { return playingZoneIndex; }

    // The Sample page's waveform playhead (ConcreteScreen) - this voice's current position within
    // its own zone, as a 0..1 fraction of [zone start, zone end). Purely a UI display query, meant
    // to be called from a UI timer rather than the audio thread: pitchEngines[0].getSourcePhase()
    // is a plain double with no synchronization here, so a torn/stale read is at worst one
    // visually-glitched frame, self-correcting the next timer tick - never a real-time-safety or
    // playback-correctness concern, since nothing about actual audio rendering reads this. Returns
    // -1 if this voice isn't currently active (nothing to show).
    float getPlaybackProgress01() const noexcept
    {
        if (!active || zone == nullptr)
            return -1.0f;
        const auto span = (double) zoneEndSample - (double) zone->start;
        if (span <= 0.0)
            return -1.0f;
        return (float) juce::jlimit(0.0, 1.0, (pitchEngines[0].getSourcePhase() - (double) zone->start) / span);
    }

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

    // Called once per host block (not per MIDI segment) for every ACTIVE voice, from
    // PluginProcessor::processBlock() - the ONE deliberate exception to "everything is snapshotted
    // once at startNote() and held fixed" (see this class's own header comment above). A voice
    // that's currently looping reads as an open-ended, continuously-playing sound rather than a
    // one-shot capture, so two things about it are meant to be genuinely live instead of frozen:
    // turning Loop off should actually stop it from looping (rather than silently doing nothing,
    // a real reported bug), and changing machines should actually retune its character instead of
    // it staying stuck on whichever machine was selected when it started.
    //
    // `liveZone` is the CURRENT published zone for whatever note this voice is playing - the
    // caller resolves it via ConcreteSampleSet::lookup(getCurrentMidiNote(), ...), the same
    // "resolve by note, not a cached index" pattern ConcreteScreen already uses, since a zone-list
    // mutation (a pad's sample being cleared, a new main sample loaded) can shift indices out from
    // under a raw one; pass nullptr if that note no longer resolves to any zone at all. The
    // live*In arguments are the CURRENT machine-derived settings (pitch engine mode, effective
    // source rate, filter model, amp envelope mode) - everything else a machine sets
    // (bitDepth/quantizer/captureTranspose) bakes into the buffer offline instead and deliberately
    // does NOT reach an already-playing voice, unchanged from Architecture #2's "a background
    // reload can't invalidate the buffer this voice is reading from."
    //
    // A voice that isn't currently looping (liveZone == nullptr, or its own loopEnabled is false)
    // only has its loop-tracking state updated - none of the live*In machine parameters apply,
    // preserving the normal "snapshotted at note-on" behavior for every ordinary one-shot/gated
    // note exactly as before.
    void refreshLiveLoopState(const ConcreteSampleZone* liveZone, ConcretePitchEngine::Mode livePitchMode,
                               double liveEffectiveSourceRateHz, ConcreteFilterModel::Mode liveFilterMode,
                               ConcreteAmpEnvelopeMode liveAmpEnvelopeMode) noexcept;

private:
    double sampleRate = 44100.0;

    ConcreteSampleSet::Ptr sampleSet; // keeps zone->buffer alive for the whole note
    const ConcreteSampleZone* zone = nullptr;
    int playingZoneIndex = -1; // see getZoneIndex()'s own comment
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

    // Live loop-tracking state (see refreshLiveLoopState()'s own comment) - seeded from the
    // zone's own values at startNote() so a voice that starts already-looping works correctly even
    // before the first refresh call, then kept in sync with whatever the CURRENT published zone
    // says every block after that, unlike everything else on this voice.
    bool liveLoopEnabled = false;
    juce::int64 liveLoopStart = 0;
    juce::int64 liveLoopEnd = 0;

    float velocityGain = 1.0f;
    juce::ADSR adsr;
    ConcreteContourEnvelope contourEnvelope;
    ConcreteAmpEnvelopeMode ampEnvelopeMode = ConcreteAmpEnvelopeMode::adsr;

    // One filter per possible zone channel (max 2), matching pitchEngines above - a stereo zone's
    // L/R content must not share filter state. filterKeyTrackOctaveOffset folds key-tracking into
    // a fixed per-note octave offset computed once at startNote() (see that function's own
    // comment), combined with the live envelope's contribution every sample in renderNextBlock().
    std::array<ConcreteFilterModel, 2> filters;
    ConcreteFilterModel::Mode filterMode = ConcreteFilterModel::Mode::bypass;
    float filterBaseCutoffHz = 20000.0f;
    float filterResonance01 = 0.0f;
    float filterEnvAmountOctaves = 0.0f;
    float filterKeyTrackOctaveOffset = 0.0f;
    juce::ADSR filterEnvelope;
};
