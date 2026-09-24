#include "ConvolutionProcessor.h"

#include <algorithm>
#include <cmath>

namespace wildjag::conv
{

namespace
{
    juce::String hzString(float value, int)
    {
        if (value >= 1000.0f)
            return juce::String(value / 1000.0f, 2) + " kHz";
        return juce::String(value, 0) + " Hz";
    }

    juce::String msString(float value, int)
    {
        // One decimal at every value, matching Caverns' and Aura's own "350.0 ms" readouts. A
        // precision that changes with magnitude makes a single knob read "0.0 ms" and then "32 ms"
        // as it is turned, which looks like a formatting bug rather than a choice.
        return juce::String(value, 1) + " ms";
    }

    juce::String percentString(float value, int)
    {
        return juce::String(value, 1) + "%";
    }
}

ConvolutionProcessor::ConvolutionProcessor(const ConvolutionVariant& variantToUse)
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      variant(variantToUse),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      worker(variantToUse)
{
    // A convolution reverb with no IR has nothing to convolve with. This is a variant packaging
    // mistake, caught here rather than as silence in a DAW.
    jassert(! variant.irs.empty());

    irIndexParam = apvts.getRawParameterValue(irIndexParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    lengthPercentParam = apvts.getRawParameterValue(lengthPercentParamID);
    attackMsParam = apvts.getRawParameterValue(attackMsParamID);
    lowCutHzParam = apvts.getRawParameterValue(lowCutHzParamID);
    highCutHzParam = apvts.getRawParameterValue(highCutHzParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(bypassParamID));

    // Runs for the processor's whole lifetime rather than being tied to prepareToPlay: it idles on
    // a wait() until something asks for an IR, and starting/stopping it around every host
    // re-prepare would only add races.
    worker.start();
}

ConvolutionProcessor::~ConvolutionProcessor()
{
    worker.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout ConvolutionProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // The IR list is per-variant, so the choice count is too. Presets therefore do not port between
    // variants - correct, since each variant is its own product with its own IRs.
    juce::StringArray irNames;
    for (const auto& ir : variant.irs)
        irNames.add(ir.displayName != nullptr ? ir.displayName : "IR");

    if (irNames.isEmpty())
        irNames.add("IR");

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { irIndexParamID, 1 }, "IR", irNames,
        juce::jlimit(0, irNames.size() - 1, variant.defaultIRIndex)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { preDelayMsParamID, 1 }, "Pre-Delay",
        juce::NormalisableRange<float> { 0.0f, ConvolutionEngine::maxPreDelayMs, 0.1f, 0.4f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(msString)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lengthPercentParamID, 1 }, "Length",
        juce::NormalisableRange<float> { 5.0f, 100.0f, 0.1f }, 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(percentString)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { attackMsParamID, 1 }, "Attack",
        juce::NormalisableRange<float> { 0.0f, 500.0f, 0.1f, 0.4f }, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(msString)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { lowCutHzParamID, 1 }, "Low Cut",
        juce::NormalisableRange<float> { ConvolutionEngine::lowCutNeutralHz, 2000.0f, 1.0f, 0.3f },
        ConvolutionEngine::lowCutNeutralHz,
        juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(hzString)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { highCutHzParamID, 1 }, "High Cut",
        juce::NormalisableRange<float> { 200.0f, ConvolutionEngine::highCutNeutralHz, 1.0f, 0.3f },
        ConvolutionEngine::highCutNeutralHz,
        juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction(hzString)));

    // Independent Dry and Wet, matching Shields/Caverns/Aura, with the wet given headroom past
    // unity. That headroom is why there is no separate output gain: a third gain stage would only
    // be a redundant place to lose level.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { dryParamID, 1 }, "Dry",
        juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(percentString)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { wetParamID, 1 }, "Wet",
        juce::NormalisableRange<float> { 0.0f, 200.0f, 0.1f }, 40.0f,
        juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(percentString)));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { bypassParamID, 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

IRShaper::Params ConvolutionProcessor::currentShapeParams() const noexcept
{
    return { lengthPercentParam->load(), attackMsParam->load() };
}

void ConvolutionProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = std::max(samplesPerBlock, 1) * blockSizeHeadroom;

    const auto channels = juce::jlimit(1, 2, std::max(getTotalNumOutputChannels(), 1));

    const auto index = (int) irIndexParam->load();
    const auto shapeParams = currentShapeParams();

    // Tells the worker this rate is current BEFORE the synchronous shape below, so it drops
    // anything left in its pending slot from before this re-prepare, and so any request already in
    // flight on the background thread discards its result instead of delivering an IR shaped for a
    // rate this instance has since moved on from - see IRLoadWorker::setSessionSampleRate()'s
    // comment for why that race is real (a re-prepare can land mid-shape).
    worker.setSessionSampleRate(sampleRate);

    // Synchronous, on the message thread, so the very first processBlock already has the right IR.
    // Leaving this to the worker would mean up to a debounce period of dry signal after every
    // transport start or sample-rate change.
    auto initialIR = worker.shapeSynchronously(index, shapeParams, sampleRate);

    engine.prepare(sampleRate, maxBlockSize, channels, std::move(initialIR));

    // Push the current parameter values in and then snap every ramp to them. Without this the
    // engine starts each prepare with its smoothers at zero and glides up to the real values over
    // the first 20-50 ms: the wet signal fades in on every transport start, and an impulse at
    // sample 0 escapes before Pre-Delay has ramped in at all. Both showed up as failures in
    // analysis/validate.py and in neither unit test, because the tests call reset() by hand.
    applyParametersToEngine();
    engine.reset();

    lastRequestedIndex = index;
    lastRequestedLengthPercent = shapeParams.lengthPercent;
    lastRequestedAttackMs = shapeParams.attackMs;

    reportedLatency = engine.getLatencySamples();
    setLatencySamples(reportedLatency);

    const auto irSamples = engine.getCurrentIRSize();
    tailSeconds.store(sampleRate > 0.0
                          ? (double) irSamples / sampleRate + ConvolutionEngine::maxPreDelayMs * 0.001
                          : 0.0);

    prepared = true;
}

void ConvolutionProcessor::applyParametersToEngine() noexcept
{
    engine.setPreDelayMs(preDelayMsParam->load());
    engine.setLowCutHz(lowCutHzParam->load());
    engine.setHighCutHz(highCutHzParam->load());
    engine.setDryGain(dryParam->load() * 0.01f);
    engine.setWetGain(wetParam->load() * 0.01f);
    engine.setBypassed(bypassParam != nullptr && bypassParam->get());
}

void ConvolutionProcessor::releaseResources() {}

void ConvolutionProcessor::reset()
{
    engine.reset();
}

bool ConvolutionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in.isDisabled() || out.isDisabled())
        return false;

    // Mono in / mono out is supported, unlike the rest of this catalog's stereo-out-only effects.
    // For a convolution reverb it is a real use case (a mono IR on a mono bus should stay mono
    // rather than being forced to duplicate into a fake stereo image).
    if (out == juce::AudioChannelSet::mono())
        return in == juce::AudioChannelSet::mono();

    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();

    return false;
}

void ConvolutionProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (! prepared)
        return;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    const auto totalIn = getTotalNumInputChannels();
    const auto totalOut = getTotalNumOutputChannels();

    // Mono in, stereo out: duplicate before anything reads channel 1, over the FULL host buffer -
    // not just the first maxBlockSize chunk below - so a stereo IR still produces a proper stereo
    // image even when an oversized block has to be split across multiple engine.process() calls.
    if (totalIn < 2 && totalOut >= 2)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    for (int channel = totalIn; channel < totalOut; ++channel)
        if (channel > 1)
            buffer.clear(channel, 0, numSamples);

    // Ask for a re-shape only when the IR selection or its shape actually moved. The worker
    // debounces the resulting burst during a knob drag.
    const auto index = (int) irIndexParam->load();
    const auto shapeParams = currentShapeParams();

    if (index != lastRequestedIndex
        || ! juce::approximatelyEqual(shapeParams.lengthPercent, lastRequestedLengthPercent)
        || ! juce::approximatelyEqual(shapeParams.attackMs, lastRequestedAttackMs))
    {
        lastRequestedIndex = index;
        lastRequestedLengthPercent = shapeParams.lengthPercent;
        lastRequestedAttackMs = shapeParams.attackMs;
        worker.requestShape(index, shapeParams, currentSampleRate);
    }

    // Collect a finished IR if one is waiting. juce::dsp::Convolution crossfades the outgoing engine
    // against the incoming one internally, so this is where a click-free swap actually happens.
    if (worker.tryPopShapedIR(incomingIR))
    {
        engine.loadIR(std::move(incomingIR));

        if (currentSampleRate > 0.0)
            tailSeconds.store((double) engine.getCurrentIRSize() / currentSampleRate
                              + ConvolutionEngine::maxPreDelayMs * 0.001);
    }

    applyParametersToEngine();

    // The engine is sized with headroom in prepareToPlay() so a normal block runs as a single pass
    // through this loop. If a host ever hands over more than that (Logic's offline bounce can),
    // process it in maxBlockSize-sized chunks instead of silently truncating the tail - see
    // ConvolutionProcessorTests' oversized-block test. juce::dsp::Convolution (and the rest of
    // ConvolutionEngine's state - the pre-delay lines, the ramped smoothers) is a continuous
    // per-sample streaming design that doesn't care how its input is chunked across calls, only
    // that no single call exceeds the block size it was prepared for.
    int processed = 0;
    while (processed < numSamples)
    {
        const auto chunk = std::min(numSamples - processed, maxBlockSize);
        juce::AudioBuffer<float> chunkView(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                            processed, chunk);
        engine.process(chunkView, chunk);
        processed += chunk;
    }

    // Expected to be zero and never to change, so in practice this fires no host callbacks at all.
    // It is here so that the number the host gets is whatever the convolution actually reports,
    // rather than a constant that could quietly drift away from the truth.
    if (const auto latency = engine.getLatencySamples(); latency != reportedLatency)
    {
        reportedLatency = latency;
        setLatencySamples(latency);
    }
}

bool ConvolutionProcessor::hasEditor() const { return true; }

const juce::String ConvolutionProcessor::getName() const
{
    return variant.displayName != nullptr ? juce::String(variant.displayName) : juce::String("Convolution");
}

bool ConvolutionProcessor::acceptsMidi() const { return false; }
bool ConvolutionProcessor::producesMidi() const { return false; }
bool ConvolutionProcessor::isMidiEffect() const { return false; }
double ConvolutionProcessor::getTailLengthSeconds() const { return tailSeconds.load(); }

int ConvolutionProcessor::getNumPrograms() { return 1; }
int ConvolutionProcessor::getCurrentProgram() { return 0; }
void ConvolutionProcessor::setCurrentProgram(int) {}
const juce::String ConvolutionProcessor::getProgramName(int) { return {}; }
void ConvolutionProcessor::changeProgramName(int, const juce::String&) {}

void ConvolutionProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void ConvolutionProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace wildjag::conv
