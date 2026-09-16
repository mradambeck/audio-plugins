#pragma once

#include "ConvolutionEngine.h"
#include "ConvolutionVariant.h"
#include "IRLoadWorker.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

// The shared AudioProcessor every convolution variant ships. A variant supplies a
// ConvolutionVariant (name, IR table, default selection) and nothing else - there is no subclass,
// and no branching on a variant's identity anywhere in here.
//
// createEditor() is deliberately NOT defined in ConvolutionProcessor.cpp. It lives in
// ConvolutionEditor.cpp, so headless targets (tests, the render harness) can link this processor
// against Source/Tests/TestCreateEditorStub.cpp and skip the editor, the LookAndFeel and the
// variant's BinaryData entirely - the same split every other plugin in this catalog uses.
namespace wildjag::conv
{

class ConvolutionProcessor : public juce::AudioProcessor
{
    // Declared before apvts on purpose. Members initialise in declaration order, and apvts's
    // initialiser calls createParameterLayout(), which reads the variant's IR names to build the
    // IR choice parameter - so the reference has to be bound by then.
    const ConvolutionVariant& variant;

public:
    explicit ConvolutionProcessor(const ConvolutionVariant& variantToUse);
    ~ConvolutionProcessor() override;

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

    // Hosts get a real bypass parameter rather than this catalog's usual
    // "if (bypassParam->load()) return;" early-out. That idiom truncates a convolution tail
    // instantly - an audible click and a lost tail - whereas ConvolutionEngine ramps the mix and
    // keeps convolving through the transition. Reported latency is zero, so there is no PDC shift
    // to reconcile either way.
    juce::AudioParameterBool* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    static constexpr auto irIndexParamID = "irIndex";
    static constexpr auto preDelayMsParamID = "preDelayMs";
    static constexpr auto lengthPercentParamID = "lengthPercent";
    static constexpr auto attackMsParamID = "attackMs";
    static constexpr auto lowCutHzParamID = "lowCutHz";
    static constexpr auto highCutHzParamID = "highCutHz";
    static constexpr auto dryParamID = "dry";
    static constexpr auto wetParamID = "wet";
    static constexpr auto bypassParamID = "bypass";

    const ConvolutionVariant& getVariant() const noexcept { return variant; }

    // The editor polls this for the waveform display; the render harness and tests use it to drive
    // the pipeline synchronously.
    IRLoadWorker& getIRLoadWorker() noexcept { return worker; }

    // Exposed so ConvolutionEngineTests can assert on latency and IR size without reaching through
    // a host.
    const ConvolutionEngine& getEngineForTests() const noexcept { return engine; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    IRShaper::Params currentShapeParams() const noexcept;

    ConvolutionEngine engine;
    IRLoadWorker worker;

    std::atomic<float>* irIndexParam = nullptr;
    std::atomic<float>* preDelayMsParam = nullptr;
    std::atomic<float>* lengthPercentParam = nullptr;
    std::atomic<float>* attackMsParam = nullptr;
    std::atomic<float>* lowCutHzParam = nullptr;
    std::atomic<float>* highCutHzParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    // Last values handed to the worker. processBlock only re-requests when one of them actually
    // moves, so a static setting costs one comparison per block rather than a wakeup.
    int lastRequestedIndex = -1;
    float lastRequestedLengthPercent = -1.0f;
    float lastRequestedAttackMs = -1.0f;

    // Receives a finished IR from the worker. A member rather than a local so the move-assignment
    // in tryPopShapedIR() never frees a buffer on the audio thread: it is left moved-from (and so
    // empty) after every handover to the engine.
    juce::AudioBuffer<float> incomingIR;

    double currentSampleRate = 0.0;
    int maxBlockSize = 0;

    // Multiple of the host's stated block size the engine is sized for. Hosts may hand over a block
    // larger than samplesPerBlock (Logic's offline bounce does), and reallocating on the audio
    // thread is a priority-inversion stall - same reasoning and same factor as shields-reverb.
    static constexpr int blockSizeHeadroom = 4;

    bool prepared = false;
    int reportedLatency = 0;
    std::atomic<double> tailSeconds { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConvolutionProcessor)
};

} // namespace wildjag::conv
