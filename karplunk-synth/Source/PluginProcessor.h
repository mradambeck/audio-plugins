#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "KarplunkExcitation.h"
#include "KarplunkLoopFilter.h"
#include "KarplunkVoice.h"

// Extended Karplus-Strong string synth. Single-voice for this base scaffold (see README.md's
// roadmap note - polyphony is the very next task, added before any of the four experimental
// areas are swapped). This class owns parameter state and MIDI dispatch only; all DSP lives in
// the four standalone classes composed by Voice (KarplunkExcitation.h, KarplunkLoopFilter.h,
// KarplunkStringLine.h, KarplunkVoice.h) - see those files for the actual algorithm and the swap
// seams.
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
    Voice voice;

    // -1 = no note currently sounding. Only a Note Off matching this note number stops the voice
    // - a stray Note Off for a different/already-released note is ignored, matching standard MIDI
    // handling for a monophonic instrument.
    int currentMidiNote = -1;

    juce::SmoothedValue<float> dampingSmoothed;
    juce::SmoothedValue<float> outputLevelSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KarplunkAudioProcessor)
};
