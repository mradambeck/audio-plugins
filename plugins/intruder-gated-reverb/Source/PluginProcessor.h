#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "IntruderFDNEngine.h"

// AMS RMX16 "Non-Lin 2" emulation. Parameter set matches the real unit's control set
// (IMPLEMENTATION.md): Decay, Pre-Delay, Mix, Input/Output level, and H (renamed "Tilt" per
// analysis/findings.md - see that doc for why "HF Damp" would have undersold what it does), plus
// Tighter. All DSP lives in IntruderFDNEngine; this class is parameter plumbing and dry/wet mixing.
class IntruderAudioProcessor : public juce::AudioProcessor
{
public:
    IntruderAudioProcessor();
    ~IntruderAudioProcessor() override;

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

    static constexpr auto decaySecondsParamID = "decaySeconds";
    static constexpr auto preDelayMsParamID = "preDelayMs";
    static constexpr auto tiltDbParamID = "tiltDb";
    static constexpr auto tighterParamID = "tighter";
    static constexpr auto mixPercentParamID = "mixPercent";
    static constexpr auto inputGainDbParamID = "inputGainDb";
    static constexpr auto outputGainDbParamID = "outputGainDb";
    static constexpr auto triggerThresholdDbParamID = "triggerThresholdDb";
    static constexpr auto bypassParamID = "bypass";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* decaySecondsParam = nullptr;
    std::atomic<float>* preDelayMsParam = nullptr;
    std::atomic<float>* tiltDbParam = nullptr;
    std::atomic<float>* tighterParam = nullptr;
    std::atomic<float>* mixPercentParam = nullptr;
    std::atomic<float>* inputGainDbParam = nullptr;
    std::atomic<float>* outputGainDbParam = nullptr;
    std::atomic<float>* triggerThresholdDbParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    int currentProgramIndex = 0;
    double sampleRateHz = 44100.0;

    IntruderFDNEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntruderAudioProcessor)
};
