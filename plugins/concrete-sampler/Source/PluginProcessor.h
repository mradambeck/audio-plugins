#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"
#include "ConcreteBusRouter.h"
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
// coarse-tune/fine-tune parameters. Phase 3 adds bit-depth reduction and companding
// (ConcreteQuantizer.h). Still no capture pass or filters yet (Phase 4 onward) - the "working
// buffer" a voice plays IS the loaded buffer until Phase 4 introduces the capture pass and a
// separate derived buffer; Phase 3's quantizer runs live per-sample in ConcreteVoice rather than
// as part of an offline bake, since that offline bake doesn't exist until Phase 4.
class ConcreteAudioProcessor : public juce::AudioProcessor
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

    // Thread-safe snapshot of the currently-published sample set, for the editor's waveform
    // display and for tests. See currentSampleSet's own comment for the locking rationale.
    ConcreteSampleSet::Ptr getCurrentSampleSet() const;

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

    // Architecture #2's session-persistence override - see getEmbedSamplesOverride().
    bool embedSamplesOverride = false;
};
