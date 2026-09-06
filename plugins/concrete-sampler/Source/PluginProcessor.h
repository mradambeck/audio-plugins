#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"
#include "ConcreteBusRouter.h"
#include "ConcreteCapturePass.h"
#include "ConcreteFilterModels.h"
#include "ConcretePitchEngine.h"
#include "ConcreteQuantizer.h"
#include "ConcreteSampleIO.h"
#include "ConcreteSampleSet.h"
#include "ConcreteVoice.h"
#include "ConcreteVoiceAllocator.h"

#include <array>

// Vintage sampler emulation instrument (see concrete-sampler-plugin-plan.md for the full design).
// Phase 1 added sample loading, a fixed voice pool, and Architecture #2's session-persistence
// behavior. Phase 2 added the three pitch-engine modes (ConcretePitchEngine.h) plus the base-rate/
// coarse-tune/fine-tune parameters. Phase 3 added bit-depth reduction and companding
// (ConcreteQuantizer.h). Phase 4 added the capture pass (ConcreteCapturePass.h): resample -> drive
// -> quantize, baked offline into each zone's working buffer rather than applied live - Phase 3's
// quantizer moved from ConcreteVoice into this bake (see ConcreteCapturePass.h's own comment on
// why quantization is part of this pipeline even when the resample/drive technique is bypassed).
// Phase 5 adds the playback-side filter models (ConcreteFilterModels.h, live per-voice - see
// ConcreteVoice.h) plus the capture pass's "double smear" option (baked, see
// ConcreteCapturePass.h's own comment on why it's the one deliberate exception to "the capture
// pass never touches filters").
class ConcreteAudioProcessor : public juce::AudioProcessor,
                                private juce::Thread,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    ConcreteAudioProcessor();
    ~ConcreteAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Loads a file into the (only, in v1) zone and republishes the sample set. Not real-time
    // safe (file I/O, allocation) - callers that run on the message thread (the editor's Load
    // button/drag-drop) are responsible for hopping to a background thread themselves if the file
    // is large enough that blocking the message thread would matter; tooling (RenderIR, tests)
    // calls this directly and synchronously, since there's no audio thread to protect there.
    // Returns false if the file couldn't be read (the previous sample set, if any, is unchanged).
    bool loadSample(const juce::File& file);

    // Changes the (only, in v1) zone's root note and republishes - see Architecture #1: this is
    // zone-list state, not an APVTS parameter.
    void setRootNoteForZone(int zoneIndex, int newRootNote);

    // Re-reads a zone's source from a new location (Architecture #2's relocate case) and
    // republishes, preserving that zone's other fields.
    bool relocateZone(int zoneIndex, const juce::File& newFile);

    // The "Embed samples in session" override (Architecture #2) - forces embedding regardless of
    // the size cap. Session-persisted state, not an APVTS parameter (it's a preference, not
    // something anyone automates).
    bool getEmbedSamplesOverride() const noexcept { return embedSamplesOverride; }
    void setEmbedSamplesOverride(bool shouldForceEmbed) { embedSamplesOverride = shouldForceEmbed; }

    // Thread-safe snapshot of the currently-published (capture-pass-baked) sample set, for
    // playback, the editor's waveform display, and tests. See currentSampleSet's own comment for
    // the locking rationale.
    ConcreteSampleSet::Ptr getCurrentSampleSet() const;

    // Thread-safe snapshot of the RAW (never capture-pass-baked) sample set - see rawSampleSet's
    // own comment. Exposed for tests that need to inspect source-space zone metadata directly.
    ConcreteSampleSet::Ptr getRawSampleSet() const;

    juce::AudioProcessorValueTreeState apvts;

    // Phase 2's first real automatable parameters. pitchEngineMode's raw value is the choice
    // INDEX (0..3) as a float - see pitchEngineModeFromParam() for the ConcretePitchEngine::Mode
    // conversion. baseRate is in Hz; coarseTune in semitones; fineTune in cents.
    static constexpr auto pitchEngineModeParamID = "pitchEngineMode";
    static constexpr auto baseRateParamID = "baseRate";
    static constexpr auto coarseTuneParamID = "coarseTune";
    static constexpr auto fineTuneParamID = "fineTune";

    // Phase 3's quantization stage (see ConcreteQuantizer.h). bitDepth is the storage word width
    // in bits, 1-16. quantizerMode's raw value is the choice INDEX (0=Linear, 1=Companded) as a
    // float, same convention as pitchEngineMode - see quantizerModeFromParam().
    static constexpr auto bitDepthParamID = "bitDepth";
    static constexpr auto quantizerModeParamID = "quantizerMode";

    // Phase 4's capture pass (see ConcreteCapturePass.h). captureTranspose is in semitones, 0-24,
    // default 5 ("the one people actually want," the 33->45rpm-style ratio - though it has no
    // effect until captureBypass is turned off). captureDrive is in dB, 0-24. captureBypass
    // defaults to true ("every preset ships with the capture pass off" - it's a technique the user
    // applies, not part of a machine's stock behavior); captureAutoCompensate defaults to true.
    // captureIterations is 1-4 (compounding degradation - see ConcreteCapturePass.h).
    static constexpr auto captureTransposeParamID = "captureTranspose";
    static constexpr auto captureDriveParamID = "captureDrive";
    static constexpr auto captureAutoCompensateParamID = "captureAutoCompensate";
    static constexpr auto captureBypassParamID = "captureBypass";
    static constexpr auto captureIterationsParamID = "captureIterations";

    // Phase 5's playback-side filter (see ConcreteFilterModels.h) - live, per-voice, never baked
    // (Architecture #3: filters are playback-side only). Defaults to Bypass/fully-open/no-
    // resonance/no-modulation, matching every other control's "no coloration until asked for"
    // convention. filterEnvAmount is bipolar octaves (negative sweeps down); filterKeyTrack is
    // 0 (no tracking) to 1 (full 1:1 tracking with the played note, like pitch).
    static constexpr auto filterModelParamID = "filterModel";
    static constexpr auto filterCutoffParamID = "filterCutoff";
    static constexpr auto filterResonanceParamID = "filterResonance";
    static constexpr auto filterEnvAmountParamID = "filterEnvAmount";
    static constexpr auto filterKeyTrackParamID = "filterKeyTrack";

    // Phase 5's "double smear" (Architecture #3) - an explicitly non-authentic option, off by
    // default, that bakes an extra filter pass into the END of the capture chain using its OWN
    // dedicated model/cutoff/resonance (deliberately separate from the live filter parameters
    // above, so turning THOSE never triggers a re-bake - only these do).
    static constexpr auto captureDoubleSmearParamID = "captureDoubleSmear";
    static constexpr auto doubleSmearFilterModelParamID = "doubleSmearFilterModel";
    static constexpr auto doubleSmearCutoffParamID = "doubleSmearCutoff";
    static constexpr auto doubleSmearResonanceParamID = "doubleSmearResonance";

    // Synchronously re-derives every zone's working buffer from its source buffer using the
    // CURRENT capture-pass parameter values, and republishes. The background bake thread (see the
    // private juce::Thread override below) runs this same logic asynchronously whenever a capture-
    // pass parameter changes during normal use; exposed publicly so tests/tooling can force a
    // deterministic, immediate re-bake instead of waiting on/polling a background thread.
    void rebakeNow();

    // Bound to the editor's on-screen keyboard (Phase 1's temporary playing surface - see
    // concrete-sampler-plugin-plan.md's Phase 1 deliverables; Phase 8 replaces it with the real
    // pad-grid/keyboard trigger surface). processBlock() merges this into the real MIDI buffer
    // every block, the same pattern JUCE's own examples use for embedding a
    // juce::MidiKeyboardComponent in a plugin editor.
    juce::MidiKeyboardState keyboardState;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleMidiMessage(const juce::MidiMessage& message, const ConcreteSampleSet::Ptr& sampleSet) noexcept;
    void publishSampleSet(ConcreteSampleSet::Ptr newSet);

    // Sets rawSampleSet to newRawSet and re-derives+publishes currentSampleSet from it via
    // bakeSampleSet() - the only way rawSampleSet should ever change. Every caller that produces a
    // genuinely new or changed zone list (loadSample, relocateZone, setRootNoteForZone,
    // setStateInformation) goes through this, never through publishSampleSet() directly, so
    // rawSampleSet's start/end/loop points always stay in source-buffer terms - see that member's
    // own comment for why this split exists.
    void publishRawSampleSet(ConcreteSampleSet::Ptr newRawSet);

    ConcreteCapturePass::Settings currentCapturePassSettings() const;

    // juce::Thread override: waits to be notify()'d (from parameterChanged() below) and then
    // calls rebakeNow(). Runs for the processor's whole lifetime, started in the constructor and
    // stopped in the destructor.
    void run() override;

    // juce::AudioProcessorValueTreeState::Listener override, registered for exactly the parameter
    // IDs that change what ConcreteCapturePass::apply() produces: bitDepth, quantizerMode,
    // captureTranspose, captureDrive, captureBypass, captureIterations, captureDoubleSmear,
    // doubleSmearFilterModel, doubleSmearCutoff, doubleSmearResonance - NOT captureAutoCompensate
    // or any of the live filterXxx parameters, none of which need a re-bake (see their own
    // declaration comments above and ConcreteVoice.h). May fire from ANY thread depending on the
    // host (worst case the audio thread itself, if a host applies automation from inside
    // processBlock()), so this does the absolute minimum: wake the bake thread. The actual
    // (expensive, allocating) re-bake work always happens on that thread, never here.
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // See common/Presets/FactoryPreset.h - getNumPrograms()/getCurrentProgram()/setCurrentProgram()/
    // getProgramName() above just forward to this.
    wildjag::FactoryPresetList factoryPresets;

    juce::AudioFormatManager formatManager;

    std::atomic<float>* pitchEngineModeParam = nullptr;
    std::atomic<float>* baseRateParam = nullptr;
    std::atomic<float>* coarseTuneParam = nullptr;
    std::atomic<float>* fineTuneParam = nullptr;
    std::atomic<float>* bitDepthParam = nullptr;
    std::atomic<float>* quantizerModeParam = nullptr;
    std::atomic<float>* captureTransposeParam = nullptr;
    std::atomic<float>* captureDriveParam = nullptr;
    std::atomic<float>* captureAutoCompensateParam = nullptr;
    std::atomic<float>* captureBypassParam = nullptr;
    std::atomic<float>* captureIterationsParam = nullptr;
    std::atomic<float>* filterModelParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    std::atomic<float>* filterResonanceParam = nullptr;
    std::atomic<float>* filterEnvAmountParam = nullptr;
    std::atomic<float>* filterKeyTrackParam = nullptr;
    std::atomic<float>* captureDoubleSmearParam = nullptr;
    std::atomic<float>* doubleSmearFilterModelParam = nullptr;
    std::atomic<float>* doubleSmearCutoffParam = nullptr;
    std::atomic<float>* doubleSmearResonanceParam = nullptr;

    // Set by parameterChanged(), cleared and acted on by run() - see that function's own comment.
    std::atomic<bool> bakeRequested { false };

    // Fixed at 8 for Phase 1 as a reasonable placeholder - Phase 6 makes this a real, per-machine-
    // preset voice count with tested stealing behavior (see concrete-sampler-plugin-plan.md's
    // Architecture #1 and Phase 6).
    static constexpr int maxVoices = 8;
    std::array<ConcreteVoice, maxVoices> voices;
    ConcreteVoiceAllocator<maxVoices> voiceAllocator;
    ConcreteBusRouter busRouter;

    double currentSampleRate = 44100.0;

    // The zone list voices currently play. Guarded by sampleSetLock for the brief pointer-copy/
    // refcount-bump needed to hand a stable reference to a starting voice or to the editor's
    // waveform display - never held during actual audio rendering. This is a deliberate, narrow
    // exception to "no locking on the audio thread": a juce::SpinLock held only around a
    // ReferenceCountedObjectPtr copy (a few non-blocking instructions, no syscalls, no
    // allocation) is the standard pragmatic pattern for this in real-world JUCE plugins, since
    // C++17 has no atomic<shared_ptr>-equivalent and a hand-rolled lock-free scheme risks a
    // genuine use-after-free bug for a benefit that doesn't matter in practice here (this lock is
    // essentially never contended - published once whenever the user loads/edits a sample). Only
    // ever REPLACED wholesale via publishSampleSet(); never mutated in place (see
    // ConcreteSampleSet.h's own comment on treating it as immutable by convention).
    mutable juce::SpinLock sampleSetLock;
    ConcreteSampleSet::Ptr currentSampleSet;

    // The zone list exactly as loaded/relocated/restored, BEFORE any capture-pass bake - its
    // zones' start/end/loopStart/loopEnd are always expressed in source-buffer terms, never
    // rescaled. rebakeNow() always re-bakes FROM this (never from currentSampleSet), which is the
    // fix for a real bug: bakeZone() rescales start/end by (newWorkingLength / sourceLength) each
    // time it runs, and rescaling AN ALREADY-RESCALED value on every subsequent re-bake compounds
    // multiplicatively - repeatedly nudging a capture-pass parameter (e.g. Capture Iterations or
    // Capture Drive, each triggering its own re-bake) would shrink the zone's audible span toward
    // nothing over several re-bakes, even though the actual audio content baked correctly every
    // time (only the start/end bookkeeping compounded). Keeping a stable, never-rescaled source-
    // space zone list for every re-bake to start from eliminates the compounding entirely. Guarded
    // by the same sampleSetLock as currentSampleSet since the two are always updated together (see
    // publishRawSampleSet()).
    ConcreteSampleSet::Ptr rawSampleSet;

    // Architecture #2's session-persistence override - see getEmbedSamplesOverride().
    bool embedSamplesOverride = false;
};
