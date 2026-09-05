#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"

// Vintage sampler emulation instrument (see concrete-sampler-plugin-plan.md for the full design).
// Phase 0: scaffold only. No sample zones and no pitch/quantization/filter DSP yet - processBlock()
// just clears its output buffer. The point of this phase is the plugin/test/render-tool scaffold
// and the offline analysis harness every later phase depends on, not anything audible yet.
class ConcreteAudioProcessor : public juce::AudioProcessor
{
public:
    ConcreteAudioProcessor();
    ~ConcreteAudioProcessor() override;

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

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // See common/Presets/FactoryPreset.h. Empty until Phase 7 defines the twelve machine presets
    // from the plan's machine table - getNumPrograms() correctly reports 0 until then.
    wildjag::FactoryPresetList factoryPresets;
};
