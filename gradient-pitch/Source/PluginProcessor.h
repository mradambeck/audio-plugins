#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"
#include "GradientPitchShiftEngine.h"

// Two-tap crossfading pitch shifter. Dual mode: engineA and engineB are both always constructed;
// engineB simply isn't processed when Dual Mode is off (avoids conditional-construction complexity,
// per the implementation plan). Width (6c) is a simple output-stage mid/side blend, no engine
// involvement. Link (6b): when on, B's live Pitch/Delay for the block are derived from A's values
// plus a fixed interval, overriding B's own Pitch/Delay knobs for that block (structurally the same
// override pattern as Flux's Sync/Rate) - B's other parameters (Feedback, Splice mode, Drift, Mix,
// Output) stay independently settable regardless of Link. Cross-feedback (6d): the processor holds
// single-sample state (lastOutputA/B, each engine's getLastWetSample() from the PREVIOUS sample) -
// both engines' outputs are computed first from that old state, then both lastOutputX are updated
// together, never interleaved (updating one before the other engine processes would give an
// asymmetric one-sample-delay loop - an easy bug that wouldn't be obvious by ear).
class GradientAudioProcessor : public juce::AudioProcessor
{
public:
    GradientAudioProcessor();
    ~GradientAudioProcessor() override;

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

    juce::AudioProcessorValueTreeState apvts;

    // The tempo-synced delay time actually in use this block, in ms - read by the editor so the
    // Delay knobs can visually track the synced value instead of the (unused, while synced) raw
    // time parameter. Written on the audio thread, read on the message thread.
    float getCurrentDelayMsA() const noexcept { return currentDelayMsA.load(std::memory_order_relaxed); }
    float getCurrentDelayMsB() const noexcept { return currentDelayMsB.load(std::memory_order_relaxed); }

    static const juce::StringArray& getSubdivisionChoices();

    // Test/tooling-only - see GradientPitchShiftEngine::setDriftSeedForTesting()'s comment. Never
    // called from the live audio path.
    void setDriftSeedForTesting(uint32_t seedA, uint32_t seedB) noexcept
    {
        engineA.setDriftSeedForTesting(seedA);
        engineB.setDriftSeedForTesting(seedB);
    }

    // Parameter IDs get an A/B suffix from the start (per the implementation plan) even though
    // unit B doesn't exist until Milestone 6 - naming this now avoids a mass rename later.
    static constexpr auto pitchSemitonesAParamID = "pitchSemitonesA";
    static constexpr auto pitchFineCentsAParamID = "pitchFineCentsA";
    static constexpr auto delayTimeMsAParamID = "delayTimeMsA";
    static constexpr auto delaySyncEnabledAParamID = "delaySyncEnabledA";
    static constexpr auto delaySubdivisionAParamID = "delaySubdivisionA";
    static constexpr auto feedbackPercentAParamID = "feedbackPercentA";
    static constexpr auto spliceModeAParamID = "spliceModeA";
    static constexpr auto crossfadeLengthMsAParamID = "crossfadeLengthMsA";
    static constexpr auto driftAmountAParamID = "driftAmountA";
    static constexpr auto mixPercentAParamID = "mixPercentA";
    static constexpr auto outputTrimDbAParamID = "outputTrimDbA";

    static constexpr auto pitchSemitonesBParamID = "pitchSemitonesB";
    static constexpr auto pitchFineCentsBParamID = "pitchFineCentsB";
    static constexpr auto delayTimeMsBParamID = "delayTimeMsB";
    static constexpr auto delaySyncEnabledBParamID = "delaySyncEnabledB";
    static constexpr auto delaySubdivisionBParamID = "delaySubdivisionB";
    static constexpr auto feedbackPercentBParamID = "feedbackPercentB";
    static constexpr auto spliceModeBParamID = "spliceModeB";
    static constexpr auto crossfadeLengthMsBParamID = "crossfadeLengthMsB";
    static constexpr auto driftAmountBParamID = "driftAmountB";
    static constexpr auto mixPercentBParamID = "mixPercentB";
    static constexpr auto outputTrimDbBParamID = "outputTrimDbB";

    static constexpr auto dualModeEnabledParamID = "dualModeEnabled";
    static constexpr auto widthPercentParamID = "widthPercent";

    static constexpr auto linkEnabledParamID = "linkEnabled";
    static constexpr auto linkPitchIntervalSemitonesParamID = "linkPitchIntervalSemitones";
    static constexpr auto linkDelayIntervalMsParamID = "linkDelayIntervalMs";

    static constexpr auto crossFeedbackEnabledParamID = "crossFeedbackEnabled";

    static constexpr auto bypassParamID = "bypass";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reads the host's current tempo, falling back to a sensible default when no playhead or no
    // tempo is available (e.g. the Standalone app with nothing else driving transport).
    double getCurrentBpm() const;

    std::atomic<float>* pitchSemitonesAParam = nullptr;
    std::atomic<float>* pitchFineCentsAParam = nullptr;
    std::atomic<float>* delayTimeMsAParam = nullptr;
    std::atomic<float>* delaySyncEnabledAParam = nullptr;
    std::atomic<float>* delaySubdivisionAParam = nullptr;
    std::atomic<float>* feedbackPercentAParam = nullptr;
    std::atomic<float>* spliceModeAParam = nullptr;
    std::atomic<float>* crossfadeLengthMsAParam = nullptr;
    std::atomic<float>* driftAmountAParam = nullptr;
    std::atomic<float>* mixPercentAParam = nullptr;
    std::atomic<float>* outputTrimDbAParam = nullptr;

    std::atomic<float>* pitchSemitonesBParam = nullptr;
    std::atomic<float>* pitchFineCentsBParam = nullptr;
    std::atomic<float>* delayTimeMsBParam = nullptr;
    std::atomic<float>* delaySyncEnabledBParam = nullptr;
    std::atomic<float>* delaySubdivisionBParam = nullptr;
    std::atomic<float>* feedbackPercentBParam = nullptr;
    std::atomic<float>* spliceModeBParam = nullptr;
    std::atomic<float>* crossfadeLengthMsBParam = nullptr;
    std::atomic<float>* driftAmountBParam = nullptr;
    std::atomic<float>* mixPercentBParam = nullptr;
    std::atomic<float>* outputTrimDbBParam = nullptr;

    std::atomic<float>* dualModeEnabledParam = nullptr;
    std::atomic<float>* widthPercentParam = nullptr;

    std::atomic<float>* linkEnabledParam = nullptr;
    std::atomic<float>* linkPitchIntervalSemitonesParam = nullptr;
    std::atomic<float>* linkDelayIntervalMsParam = nullptr;

    std::atomic<float>* crossFeedbackEnabledParam = nullptr;

    std::atomic<float>* bypassParam = nullptr;

    // See common/Presets/FactoryPreset.h - getNumPrograms()/getCurrentProgram()/setCurrentProgram()/
    // getProgramName() above just forward to this.
    wildjag::FactoryPresetList factoryPresets;
    double sampleRateHz = 44100.0;

    GradientPitchShiftEngine engineA;
    GradientPitchShiftEngine engineB;

    // Single-sample cross-feedback state - see the class comment for why both must be updated
    // together, after both engines have processed, not interleaved.
    float lastOutputA = 0.0f;
    float lastOutputB = 0.0f;

    std::atomic<float> currentDelayMsA { 0.0f };
    std::atomic<float> currentDelayMsB { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GradientAudioProcessor)
};
