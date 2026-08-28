#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// A single first-order digital allpass stage (Zölzer/DAFX form): passes every frequency at equal
// gain, only shifting phase, with a break frequency where the shift crosses 90 degrees. This -
// not juce::dsp::IIR::Coefficients::makeAllPass, which is a 2nd-order/biquad shape with its own Q -
// is the building block a classic analog phaser cascades: mixed back with the dry signal, N
// cascaded first-order stages produce N/2 notches in the combined frequency response, which is
// exactly why real phaser stage counts (Phase 90 = 4, Phase 100 = 10, etc.) are always even.
class FluxAllpassStage
{
public:
    void reset() noexcept { x1 = 0.0f; y1 = 0.0f; }

    // coefficient must stay within (-1, 1) for stability; see coefficientForFrequency() below.
    void setCoefficient(float newCoefficient) noexcept { coefficient = newCoefficient; }

    float processSample(float x) noexcept
    {
        const auto y = coefficient * (x - y1) + x1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float coefficient = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};

class FluxAudioProcessor : public juce::AudioProcessor
{
public:
    FluxAudioProcessor();
    ~FluxAudioProcessor() override;

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

    // The tempo-synced LFO rate actually in use this block, in Hz - read by the editor so the
    // Rate knob can visually track the synced value instead of the (unused, while synced) raw
    // rate parameter. Written on the audio thread, read on the message thread.
    float getCurrentLfoRateHz() const noexcept { return currentLfoRateHz.load(std::memory_order_relaxed); }

    static const juce::StringArray& getDivisionChoices();

    // Stage-count choices, in the same order as the "stages" AudioParameterChoice and the editor's
    // 7 stage-select buttons - both read this so the mapping only lives in one place.
    static const juce::StringArray& getStageChoices();
    static int getStageCountForChoiceIndex(int index);

    static constexpr auto bypassParamID = "bypass";
    static constexpr auto rateParamID = "rate";
    static constexpr auto syncParamID = "sync";
    static constexpr auto divisionParamID = "division";
    static constexpr auto depthParamID = "depth";
    static constexpr auto shapeParamID = "shape";
    static constexpr auto stagesParamID = "stages";
    static constexpr auto offsetParamID = "offset";
    static constexpr auto feedbackParamID = "feedback";
    static constexpr auto brightnessParamID = "brightness";
    static constexpr auto gritParamID = "grit";
    static constexpr auto blendParamID = "blend";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reads the host's current tempo, falling back to a sensible default when no playhead or no
    // tempo is available (e.g. the Standalone app with nothing else driving transport).
    double getCurrentBpm() const;

    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* syncParam = nullptr;
    std::atomic<float>* divisionParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* stagesParam = nullptr;
    std::atomic<float>* offsetParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* brightnessParam = nullptr;
    std::atomic<float>* gritParam = nullptr;
    std::atomic<float>* blendParam = nullptr;

    int currentProgramIndex = 0;

    double sampleRateHz = 44100.0;

    // Up to the largest offered stage count (36), one chain per channel. Only the first
    // activeStageCount of each are actually run per sample; the rest sit idle (reset, coefficient
    // untouched) so switching Stages mid-playback never has to reallocate.
    static constexpr int maxStages = 36;
    std::array<FluxAllpassStage, maxStages> allpassL, allpassR;

    // Per-sample feedback tap: the allpass chain's own previous output, scaled by Feedback and
    // summed back into this sample's chain input - the resonant-notch character a phaser's
    // feedback knob is known for. Soft-clipped (tanh) so it stays self-limiting even at high
    // feedback rather than actually running away.
    float feedbackStateL = 0.0f, feedbackStateR = 0.0f;

    // LFO phase, 0-1, kept rolling continuously (even at zero depth) so raising Depth mid-playback
    // never produces a phase discontinuity.
    double lfoPhase = 0.0;

    std::atomic<float> currentLfoRateHz { 0.5f };

    juce::dsp::IIR::Filter<float> brightnessFilterL, brightnessFilterR;

    // A peaking cut tied to the Grit knob (not independently controllable) - see gritEqHz/Q/
    // gainDb in PluginProcessor.cpp for why 110Hz specifically.
    juce::dsp::IIR::Filter<float> gritEqFilterL, gritEqFilterR;

    // Cache guards for the Grit EQ / Brightness filter coefficient recomputation in processBlock()
    // (each involves a genuine std::tan() call plus a heap allocation - see that call site's
    // comment) - skip recompute when the underlying amount hasn't changed since the last block.
    // -1.0f sentinels (both amounts are always in [0,1]) reset in prepareToPlay(), not just at
    // construction - both filters' coefficients also depend on sampleRateHz, so a cached
    // "unchanged amount" value from a prior session at a different sample rate must not survive a
    // prepareToPlay() call at a new rate.
    float lastGritAmount = -1.0f;
    float lastBrightnessAmount = -1.0f;

    // Grit's drive/makeup/output-trim coefficients, derived purely from Grit (no sample-rate
    // dependency) - recomputed only inside the lastGritAmount cache guard above, so they're now
    // members (read every sample in the per-sample loop) rather than per-block locals.
    float gritK = 0.0f, gritMakeup = 1.0f, gritOutputTrim = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluxAudioProcessor)
};
