#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../common/Presets/FactoryPreset.h"
#include "ShieldsFDNEngine.h"

// Shoegaze-inspired diffuse reverb built around an 8-line Hadamard-mixed FDN (ShieldsFDNEngine) -
// see that class for the DSP core itself. This processor owns parameter state, the dry/wet mix,
// and stereo buffer plumbing; no per-sample DSP math lives here beyond mixing dry against the
// engine's wet output.
class ShieldsAudioProcessor : public juce::AudioProcessor
{
public:
    ShieldsAudioProcessor();
    ~ShieldsAudioProcessor() override;

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

    juce::AudioProcessorValueTreeState apvts;

    static constexpr auto diffusionParamID = "diffusion";
    static constexpr auto feedbackParamID = "feedback";
    static constexpr auto sizeParamID = "size";
    static constexpr auto dampingParamID = "damping";
    static constexpr auto bandwidthHzParamID = "bandwidthHz";
    static constexpr auto lowCutHzParamID = "lowCutHz";
    static constexpr auto bitDepthParamID = "bitDepth";
    static constexpr auto dryParamID = "dry";
    static constexpr auto wetParamID = "wet";
    static constexpr auto wobbleParamID = "wobble";
    static constexpr auto bypassParamID = "bypass";

    // Exposed so the offline IR-render harness (Source/Tools/RenderIR.cpp) can drive the engine
    // directly from fixed CLI-specified values without going through the host-automation path.
    ShieldsFDNEngine& getEngineForRenderHarness() { return engine; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Built-in factory presets (see PluginProcessor.cpp's getFactoryPresets()) - the
    // getNumPrograms()/getCurrentProgram()/setCurrentProgram()/getProgramName() overrides above
    // just forward to this. See common/Presets/FactoryPreset.h for the shared mechanism every Wild
    // Jag plugin with built-in presets now uses.
    wildjag::FactoryPresetList factoryPresets;

    std::atomic<float>* diffusionParam = nullptr;
    std::atomic<float>* feedbackParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* bandwidthHzParam = nullptr;
    std::atomic<float>* lowCutHzParam = nullptr;
    std::atomic<float>* bitDepthParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    std::atomic<float>* wobbleParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    ShieldsFDNEngine engine;

    // Scratch buffer holding the engine's wet output before the dry/wet mix. Sized once in
    // prepareToPlay() with headroom over the host's stated block size and never resized on the
    // audio thread - see prepareToPlay() for why that headroom matters.
    juce::AudioBuffer<float> wetBuffer;

    // Multiple of the host's stated block size that wetBuffer is allocated for. 4x covers hosts
    // that hand over a larger-than-advertised block (Logic's offline bounce being the case that
    // prompted this) without the audio thread ever having to allocate.
    static constexpr int blockSizeHeadroom = 4;
    int maxBlockSize = 0;

    // False until prepareToPlay() has run - processBlock() early-outs rather than indexing the
    // engine's not-yet-allocated delay lines.
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShieldsAudioProcessor)
};
