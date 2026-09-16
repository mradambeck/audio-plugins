#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"
#include "../../common/convolution/ConvolutionEngine.h"
#include "../../common/dsp/OnePoleFilter.h"
#include "InhaltIRWorker.h"

// AMS RMX16 "NonLin" recreation - built by CONVOLVING a synthesized IR, not by running a live
// FDN. See ml-toolkit/effects/nonlin/ and the project plan for why: the hardware's own response
// to any input is a fixed, gated impulse response, so convolving a synthesized one is exact under
// ANY input signal, unlike an envelope-follower-driven live gate (which only matches under a
// single impulse). InhaltIRWorker synthesizes that IR off the audio thread whenever Time or High
// change; ConvolutionEngine (shared with the convolution-base/variant family) owns pre-delay, low
// cut, dry/wet and a click-free ramped bypass around it.
class InhaltAudioProcessor : public juce::AudioProcessor
{
public:
    InhaltAudioProcessor();
    ~InhaltAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

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

    // Ramped bypass through the engine, not an early-out - see ConvolutionProcessor's own comment
    // on why an early-out would guillotine a convolution tail.
    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    // Time knob, deliberately in the hardware's OWN 0.1-9.8 label units, not seconds - the label
    // is not seconds (see InhaltParameterMap::gateLengthMsForDisplay for the real mapping) and
    // naming this ID/parameter "timeSeconds" would be actively false. The editor shows the mapped
    // gate length underneath.
    static constexpr auto timeKnobParamID = "timeKnob";
    static constexpr auto highParamID = "high";
    static constexpr auto preDelayMsParamID = "preDelayMs";
    static constexpr auto lowCutHzParamID = "lowCutHz";
    static constexpr auto converterParamID = "converter";
    static constexpr auto widthParamID = "width";
    static constexpr auto dryParamID = "dry";
    static constexpr auto wetParamID = "wet";
    static constexpr auto bypassParamID = "bypass";

    // For the render harness and tests - same convention as every other plugin's
    // getEngineForRenderHarness()/getEngineForTests() accessor.
    wildjag::conv::ConvolutionEngine& getEngineForRenderHarness() noexcept { return engine; }
    inhalt::InhaltIRWorker& getIRWorkerForRenderHarness() noexcept { return irWorker; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static std::vector<wildjag::FactoryPreset> getFactoryPresets();

    wildjag::conv::ConvolutionEngine engine;
    inhalt::InhaltIRWorker irWorker;

    std::atomic<float>* timeKnobParam = nullptr;
    std::atomic<float>* highParam = nullptr;
    std::atomic<float>* preDelayMsParam = nullptr;
    std::atomic<float>* lowCutHzParam = nullptr;
    std::atomic<float>* converterParam = nullptr;
    std::atomic<float>* widthParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    float lastRequestedTimeKnob = -1.0f;
    float lastRequestedHigh = 1000.0f; // deliberately outside the valid range, so the first
    // processBlock always issues an initial synthesis request even if the default APVTS value is
    // literally 0.

    double currentSampleRate = 44100.0;

    // Converter (Vintage/Modern) - bandwidth + noise-floor only, NOT a saturation stage (harmonics
    // are explicitly out of scope - see the project plan). Applied post-engine, not inside
    // ConvolutionEngine (which stays variant-agnostic). Vintage: 20Hz-18kHz -3/+0dB, ~90dB dynamic
    // range (~16-bit-class quantization). Modern: near-flat to 18kHz, ~112dB (quantization
    // effectively bypassed). Both figures are from the AMS RMX16 spec sheet Adam supplied, not
    // measured from a capture - see PluginProcessor.cpp's createParameterLayout() comment on this
    // parameter for the exact numbers and why it's spec-derived rather than calibrated.
    wildjag::dsp::OnePoleFilter converterBandwidthL, converterBandwidthR;

    wildjag::FactoryPresetList factoryPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InhaltAudioProcessor)
};
