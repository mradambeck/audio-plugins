#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"
#include "AuraFDNEngine.h"

// AMS RMX16 "Ambience" emulation. Decay and Color match the real unit's own control set (displayed
// as "Decay"/"Color" - underlying param IDs stay timeSeconds/highDb, see their own comments in
// createParameterLayout()), plus standard Dry/Wet/Pre-Delay/Bypass controls this catalog exposes
// on every reverb (Dry/Wet independent level pair, not a single blend knob - same convention as
// caverns-delay's own dryParamID/wetParamID), plus a discrete 3-position "Converter" bit-depth
// selector (button + LED list, underlying param ID stays bitDepth - see createParameterLayout()'s
// comment). Low Cut is NOT from the real
// hardware - see AuraFDNEngine.h's setLowCutHz() comment for why the real unit's own "Low" knob
// was repurposed. All DSP lives in AuraFDNEngine; this class is parameter plumbing and dry/wet
// mixing.
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

    // "Converter" (Bit Depth) choices, in the same order as the "bitDepth" AudioParameterChoice
    // and the editor's 3-LED list - both read this so the mapping only lives in one place. Matches
    // flux-phaser's own getStageChoices()/getStageCountForChoiceIndex() pattern.
    static const juce::StringArray& getConverterChoices();
    static float getBitDepthForChoiceIndex(int index);

    static constexpr auto timeSecondsParamID = "timeSeconds";
    static constexpr auto lowCutHzParamID = "lowCutHz";
    static constexpr auto highDbParamID = "highDb";
    static constexpr auto preDelayMsParamID = "preDelayMs";
    static constexpr auto bitDepthParamID = "bitDepth";
    static constexpr auto dryParamID = "dry";
    static constexpr auto wetParamID = "wet";
    static constexpr auto bypassParamID = "bypass";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* timeSecondsParam = nullptr;
    std::atomic<float>* lowCutHzParam = nullptr;
    std::atomic<float>* highDbParam = nullptr;
    std::atomic<float>* preDelayMsParam = nullptr;
    std::atomic<float>* bitDepthParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    double sampleRateHz = 44100.0;

    AuraFDNEngine engine;

    // See common/Presets/FactoryPreset.h - getNumPrograms()/getCurrentProgram()/setCurrentProgram()/
    // getProgramName() above just forward to this.
    wildjag::FactoryPresetList factoryPresets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuraAudioProcessor)
};
