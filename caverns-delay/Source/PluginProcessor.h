#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class CavernsAudioProcessor : public juce::AudioProcessor
{
public:
    CavernsAudioProcessor();
    ~CavernsAudioProcessor() override;

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
    // L/R Time knobs can visually track the synced value instead of the (unused, while synced)
    // raw time parameter. Written on the audio thread, read on the message thread.
    float getCurrentLeftDelayMs() const noexcept { return currentLeftDelayMs.load(std::memory_order_relaxed); }
    float getCurrentRightDelayMs() const noexcept { return currentRightDelayMs.load(std::memory_order_relaxed); }

    static const juce::StringArray& getSubdivisionChoices();

    static constexpr auto bypassParamID = "bypass";
    static constexpr auto syncParamID = "sync";
    static constexpr auto linkParamID = "link";
    static constexpr auto leftSubdivisionParamID = "leftSubdivision";
    static constexpr auto rightSubdivisionParamID = "rightSubdivision";
    static constexpr auto leftTimeParamID = "leftTime";
    static constexpr auto rightTimeParamID = "rightTime";
    static constexpr auto feedbackParamID = "feedback";
    static constexpr auto dryParamID = "dry";
    static constexpr auto wetParamID = "wet";
    static constexpr auto lowCutParamID = "lowCut";
    static constexpr auto highCutParamID = "highCut";
    static constexpr auto modSpeedParamID = "modSpeed";
    static constexpr auto modDepthParamID = "modDepth";
    static constexpr auto degradeParamID = "degrade";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reads the host's current tempo, falling back to a sensible default when no playhead or no
    // tempo is available (e.g. the Standalone app with nothing else driving transport).
    double getCurrentBpm() const;

    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* linkParam = nullptr;
    std::atomic<float>* leftSubdivisionParam = nullptr;
    std::atomic<float>* rightSubdivisionParam = nullptr;
    std::atomic<float>* leftTimeParam = nullptr;
    std::atomic<float>* rightTimeParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* modSpeedParam = nullptr;
    std::atomic<float>* modDepthParam = nullptr;
    std::atomic<float>* degradeParam = nullptr;

    int currentProgramIndex = 0;

    double sampleRateHz = 44100.0;

    // Two independent mono delay lines rather than one stereo line, since the left and right
    // channels can run at completely different delay times.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLineL { 1 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLineR { 1 };

    // The bucket-brigade character: every time a repeat goes back through the feedback loop, it
    // passes through this fixed darkening filter and a soft saturator again - never the dry tap
    // itself. That's what makes each successive echo a little warmer and duller than the last,
    // rather than every echo sounding identically processed.
    juce::dsp::IIR::Filter<float> feedbackDarkenerL, feedbackDarkenerR;

    // Cache guards for the three IIR::Coefficients::make*() calls in processBlock() (each does a
    // genuine std::tan() plus a heap allocation - see that call site's comment) - skip recompute
    // when the Hz value hasn't changed since the last block. -1.0f sentinels (Hz is always
    // positive) reset in prepareToPlay(), not just at construction - these coefficients also
    // depend on sampleRateHz, so a cached "unchanged Hz" value from a prior session at a different
    // sample rate must not survive a prepareToPlay() call at a new rate.
    float lastDegradeDarkenerHz = -1.0f;
    float lastLowCutHz = -1.0f;
    float lastHighCutHz = -1.0f;

    // A tiny modulated delay sitting only in the feedback return path (after saturation, before
    // the signal rejoins the main delay line) - separate from the user-facing Mod Speed/Depth
    // knobs, which wobble the whole line uniformly. Because this only touches the returning
    // signal, its flutter compounds with every additional pass through the loop, same as the
    // drive and darkening above. Driven entirely by the Degrade knob; silent/passthrough at
    // Degrade = 0.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> degradeWobbleDelayL { 1 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> degradeWobbleDelayR { 1 };

    // User-controlled tone shaping applied to the combined delay output, independent of the
    // per-repeat darkening above - this shapes the overall window of the repeats, not how they
    // change from one to the next.
    juce::dsp::IIR::Filter<float> lowCutFilterL, lowCutFilterR;
    juce::dsp::IIR::Filter<float> highCutFilterL, highCutFilterR;

    std::atomic<float> currentLeftDelayMs { 350.0f };
    std::atomic<float> currentRightDelayMs { 350.0f };

    // Delay time changes ramp rather than jump - a sudden change would click, and a gliding pitch
    // bend as the time moves is itself an authentic trait of analog (clock-rate-driven) delays.
    juce::SmoothedValue<float> leftDelaySamplesSmoothed;
    juce::SmoothedValue<float> rightDelaySamplesSmoothed;

    // Running phase of the delay-time modulation LFO, in radians. Kept rolling continuously
    // (even at zero depth) so there's no phase discontinuity when depth is raised mid-playback.
    double modPhase = 0.0;

    // Upper bound for the modulated delay length, derived from the delay lines' actual capacity -
    // keeps the LFO from ever pushing the read position past what was allocated in prepareToPlay.
    float maxDelaySamplesLimit = 0.0f;

    // Phase for the fixed-rate degrade-wobble LFO (see degradeWobbleDelayL/R above), and the
    // sample ceiling for how far it's allowed to push the feedback-path flutter at full Degrade.
    double degradeWobblePhase = 0.0;
    float maxDegradeWobbleSamples = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CavernsAudioProcessor)
};
