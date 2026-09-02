#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AuraFDNEngine.h"

// AMS RMX16 "Ambience" emulation. Parameter set matches the real unit's control set (Time, Low,
// High) plus standard Mix/Gain/Pre-Delay/Bypass controls this catalog exposes on every reverb.
// All DSP lives in AuraFDNEngine; this class is parameter plumbing and dry/wet mixing.
class AuraAudioProcessor : public juce::AudioProcessor
{
public:
    AuraAudioProcessor();
    ~AuraAudioProcessor() override;

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

    static constexpr auto timeSecondsParamID = "timeSeconds";
    static constexpr auto lowDbParamID = "lowDb";
    static constexpr auto highDbParamID = "highDb";
    static constexpr auto preDelayMsParamID = "preDelayMs";
    static constexpr auto bitDepthParamID = "bitDepth";
    static constexpr auto mixPercentParamID = "mixPercent";
    static constexpr auto inputGainDbParamID = "inputGainDb";
    static constexpr auto outputGainDbParamID = "outputGainDb";
    static constexpr auto bypassParamID = "bypass";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* timeSecondsParam = nullptr;
    std::atomic<float>* lowDbParam = nullptr;
    std::atomic<float>* highDbParam = nullptr;
    std::atomic<float>* preDelayMsParam = nullptr;
    std::atomic<float>* bitDepthParam = nullptr;
    std::atomic<float>* mixPercentParam = nullptr;
    std::atomic<float>* inputGainDbParam = nullptr;
    std::atomic<float>* outputGainDbParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    int currentProgramIndex = 0;
    double sampleRateHz = 44100.0;

    AuraFDNEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraAudioProcessor)
};
