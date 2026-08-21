#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include "KarplunkExcitation.h"
#include "KarplunkLoopFilter.h"
#include "KarplunkVoice.h"
#include "KarplunkVoiceAllocator.h"

// Extended Karplus-Strong string synth. This class owns parameter state, the voice pool, and
// MIDI dispatch only; all DSP lives in the four standalone classes composed by Voice
// (KarplunkExcitation.h, KarplunkLoopFilter.h, KarplunkStringLine.h, KarplunkVoice.h) - see those
// files for the actual algorithm and the swap seams. Voice-to-note allocation/stealing is its own
// standalone, testable class (KarplunkVoiceAllocator.h) - see that file for why it's separate.
class KarplunkAudioProcessor : public juce::AudioProcessor
{
public:
    KarplunkAudioProcessor();
    ~KarplunkAudioProcessor() override;

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

    static constexpr auto dampingParamID = "damping";
    static constexpr auto outputLevelParamID = "outputLevel";
    static constexpr auto brightnessParamID = "brightness";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleMidiMessage(const juce::MidiMessage& message) noexcept;

    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* outputLevelParam = nullptr;
    std::atomic<float>* brightnessParam = nullptr;

    using Voice = SingleLineKarplunkVoice<NoiseBurstExcitation, TwoPointAverageLoopFilter, LinearInterpolator>;

    // 8 voices, basic oldest-voice-stealing (see KarplunkVoiceAllocator.h) - each Voice composes
    // its three area-components by value, so this pool needed zero changes to any of the four
    // experimental-area classes, exactly as the base scaffold's architecture was designed for.
    static constexpr int numVoices = 8;
    std::array<Voice, numVoices> voices;
    KarplunkVoiceAllocator<numVoices> voiceAllocator;

    // Fixed headroom applied to the summed voice output before Output Level, so a full chord at
    // max velocity doesn't clip harder than a single note did in the mono scaffold. A constant
    // (not activity-dependent) scale, so it doesn't itself introduce level pumping as voices
    // come and go.
    static constexpr float polyHeadroomGain = 0.35355339f; // 1 / sqrt(numVoices)

    juce::SmoothedValue<float> dampingSmoothed;
    juce::SmoothedValue<float> outputLevelSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarplunkAudioProcessor)
};
