#pragma once

#include "../dsp/CircularDelayBuffer.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>

// The wet path of a convolution reverb: pre-delay -> convolution -> high/low cut -> dry/wet mix,
// with a ramped bypass. Owns no parameters and no variant knowledge; ConvolutionProcessor drives it.
//
// Deliberately ONE juce::dsp::Convolution, not the two-voice crossfade this catalog's plan
// originally called for. juce_Convolution.cpp's Convolution::Impl already keeps the outgoing engine
// alive as `previousEngine` and crossfades it against the incoming one over 50 ms via its internal
// CrossoverMixer, driven from installPendingEngine() on the audio thread. That is exactly the
// click-free IR swap a second voice would have provided, so a second voice would have duplicated it
// - and would have needed a way to know when the asynchronous load had actually gone live, which
// the public API does not expose. See ConvolutionEngineTests for the swap-discontinuity check that
// holds this behaviour in place.
namespace wildjag::conv
{

class ConvolutionEngine
{
public:
    ConvolutionEngine();

    // Longest pre-delay offered. Sizes the delay buffers, so it is a hard limit rather than a
    // parameter range that could be widened without re-preparing.
    static constexpr float maxPreDelayMs = 500.0f;

    // Filter settings at or beyond these values are treated as "off" and skipped entirely rather
    // than run at a near-transparent cutoff, so the documented default of "not affecting the
    // signal" is bit-exact. See ConvolutionEngineTests' filter-neutrality check.
    static constexpr float lowCutNeutralHz = 20.0f;
    static constexpr float highCutNeutralHz = 20000.0f;

    // Allocates. numChannels must be 1 or 2 - juce::dsp::Convolution supports no more.
    //
    // `initialIR` is loaded before juce::dsp::Convolution::prepare() rather than after, which
    // juce_Convolution.h calls out explicitly: prepare() finalises the most recent
    // loadImpulseResponse(), so an IR supplied this way is already live on the very first process()
    // call. Passing it afterwards instead would leave the first ~50 ms crossfading up from the raw
    // input, which is audible on the first note after every transport start.
    void prepare(double sampleRate, int maxBlockSize, int numChannels,
                 juce::AudioBuffer<float>&& initialIR = {});
    void reset();

    // Hands a shaped, already-session-rate IR to the convolution. Wait-free, and per
    // juce_Convolution.h's threading note this must be called from the audio thread. Takes
    // ownership; the buffer must have been allocated somewhere else (IRLoadWorker).
    void loadIR(juce::AudioBuffer<float>&& shapedAtSessionRate);

    void setPreDelayMs(float ms) noexcept;
    void setLowCutHz(float hz) noexcept;
    void setHighCutHz(float hz) noexcept;
    void setDryGain(float linearGain) noexcept;
    void setWetGain(float linearGain) noexcept;
    void setBypassed(bool shouldBeBypassed) noexcept;

    // Processes in place: `buffer` arrives holding the dry input and leaves holding the mix.
    void process(juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return convolution.getLatency(); }
    int getCurrentIRSize() const noexcept { return convolution.getCurrentIRSize(); }
    bool isPrepared() const noexcept { return prepared; }

private:
    juce::dsp::Convolution convolution;

    juce::dsp::StateVariableTPTFilter<float> lowCutFilter, highCutFilter;
    bool lowCutActive = false, highCutActive = false;
    float lowCutTargetHz = lowCutNeutralHz, highCutTargetHz = highCutNeutralHz;

    std::vector<wildjag::dsp::CircularDelayBuffer> preDelayLines;

    juce::SmoothedValue<float> preDelaySamples, dryGain, wetGain, bypassAmount;

    juce::AudioBuffer<float> wetBuffer;

    double currentSampleRate = 0.0;
    int maxBlockSize = 0;
    int numChannels = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionEngine)
};

} // namespace wildjag::conv
