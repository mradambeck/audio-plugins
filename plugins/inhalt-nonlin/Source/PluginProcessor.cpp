#include "PluginProcessor.h"

#include "../../common/convolution/IRLibrary.h"

#include <cmath>

namespace
{
    // No .aupreset files exist yet - Inhalt has no host session history to decode presets from,
    // unlike every other plugin's getFactoryPresets() (see PluginProcessor.cpp's own comment in
    // e.g. shields-reverb). Ship empty rather than hand-tuning fabricated presets; add real ones
    // once Adam has saved some through a host, matching this catalog's standing convention.
    const std::vector<wildjag::FactoryPreset>& getFactoryPresetsTable()
    {
        static const std::vector<wildjag::FactoryPreset> presets;
        return presets;
    }

    // Headroom over the host's stated block size, same reasoning/value as every other plugin in
    // this catalog (see ShieldsAudioProcessor::prepareToPlay's own comment).
    constexpr int blockSizeHeadroom = 2;

    // Vintage/Modern converter - see PluginProcessor.h's converterBandwidthL/R comment. Both
    // figures come from the AMS RMX16 spec sheet Adam supplied, not a capture measurement.
    constexpr float vintageBandwidthHz = 18000.0f;   // 20Hz-18kHz -3/+0dB (original unit)
    constexpr float modernBandwidthHz = 21000.0f;    // effectively flat to 18kHz (500-series reissue)
    constexpr float vintageQuantizationLevels = 32768.0f; // ~16-bit-class, matching a ~90dB DR unit
} // namespace

InhaltAudioProcessor::InhaltAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
      factoryPresets(getFactoryPresetsTable())
{
    timeKnobParam = apvts.getRawParameterValue(timeKnobParamID);
    highParam = apvts.getRawParameterValue(highParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    lowCutHzParam = apvts.getRawParameterValue(lowCutHzParamID);
    converterParam = apvts.getRawParameterValue(converterParamID);
    widthParam = apvts.getRawParameterValue(widthParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(bypassParamID));

    // Runs for the processor's whole lifetime, same reasoning as ConvolutionProcessor's own
    // worker.start() - it idles on wait() until something asks for an IR.
    irWorker.start();
}

InhaltAudioProcessor::~InhaltAudioProcessor()
{
    irWorker.stop();
}

juce::AudioProcessorValueTreeState::ParameterLayout InhaltAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Deliberately the hardware's OWN 0.1-9.8 label scale, kept per Adam's explicit instruction -
    // NOT real seconds (measured -20dB gate length only spans ~102-300ms across this whole range;
    // see InhaltParameterMap::gateLengthMsForDisplay). The editor shows the mapped ms value
    // alongside this knob so the label's real meaning stays visible.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{timeKnobParamID, 1},
        "Time",
        juce::NormalisableRange<float>(0.1f, 9.8f, 0.01f),
        2.2f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highParamID, 1},
        "High Frequency",
        juce::NormalisableRange<float>(-9.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " dB"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{preDelayMsParamID, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    // 0Hz = genuine bypass, same explicit-bypass contract as every other Low Cut in this catalog
    // (Aura, Shields) - see ConvolutionEngine::lowCutNeutralHz.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowCutHzParamID, 1},
        "Low Cut",
        juce::NormalisableRange<float>(0.0f, 300.0f, 0.1f, 0.5f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) { return v <= 0.0f ? juce::String("Off") : juce::String((int) v) + " Hz"; })));

    // Bandwidth + noise-floor only, NOT a saturation stage - see PluginProcessor.h's
    // converterBandwidthL/R comment. Both positions are documented spec-sheet numbers, not
    // calibrated against a capture (no bypass/converter-isolating capture exists yet).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{converterParamID, 1},
        "Converter",
        juce::StringArray { "Vintage", "Modern" },
        0));

    // 100% = the hardware's own measured width (IACC ~0.006-0.037, side/mid ~0dB - see the
    // project plan) - a bit-identical no-op at the default, same explicit-bypass contract as
    // every other utility control in this catalog.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{widthParamID, 1},
        "Width",
        juce::NormalisableRange<float>(0.0f, 150.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{dryParamID, 1},
        "Dry",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        100.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{wetParamID, 1},
        "Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void InhaltAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const auto maxBlockSize = std::max(samplesPerBlock, 1) * blockSizeHeadroom;
    const auto channels = juce::jlimit(1, 2, std::max(getTotalNumOutputChannels(), 1));

    const auto timeKnob = timeKnobParam->load();
    const auto high = highParam->load();

    // Synchronous, on the message thread - same reasoning as ConvolutionProcessor's own
    // prepareToPlay: the very first processBlock needs the right IR already loaded, not up to a
    // debounce period of dry signal after every transport start or sample-rate change.
    auto initialIR = inhalt::InhaltIRWorker::synthesizeSynchronously(timeKnob, high, sampleRate, 0);
    engine.prepare(sampleRate, maxBlockSize, channels, std::move(initialIR));

    // Push current parameter values and snap every ramp to them immediately - the exact fix
    // ConvBase's own empirical validation found missing (see plugins/convolution-base's history):
    // without this, prepareToPlay leaves the engine's smoothers at zero, fading the wet signal in
    // over ~20ms on every transport start and letting an impulse escape before Pre-Delay ramps in.
    engine.setPreDelayMs(preDelayMsParam->load());
    engine.setLowCutHz(lowCutHzParam->load());
    engine.setHighCutHz(20000.0f); // no High Cut control on Inhalt - leave the engine's own filter neutral
    engine.setDryGain(dryParam->load() * 0.01f);
    engine.setWetGain(wetParam->load() * 0.01f);
    engine.setBypassed(bypassParam != nullptr && bypassParam->get());
    engine.reset();

    converterBandwidthL.reset();
    converterBandwidthR.reset();

    lastRequestedTimeKnob = timeKnob;
    lastRequestedHigh = high;

    setLatencySamples(engine.getLatencySamples());
}

void InhaltAudioProcessor::releaseResources() {}

void InhaltAudioProcessor::reset()
{
    engine.reset();
    converterBandwidthL.reset();
    converterBandwidthR.reset();
}

bool InhaltAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void InhaltAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (! engine.isPrepared())
        return;

    if (buffer.getNumChannels() < 2)
        return;

    const auto numSamples = buffer.getNumSamples();

    if (getTotalNumInputChannels() < 2)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    // Ask for a re-synthesis only when Time or High actually moved - the worker debounces the
    // resulting burst during a knob drag.
    const auto timeKnob = timeKnobParam->load();
    const auto high = highParam->load();
    if (! juce::approximatelyEqual(timeKnob, lastRequestedTimeKnob)
        || ! juce::approximatelyEqual(high, lastRequestedHigh))
    {
        lastRequestedTimeKnob = timeKnob;
        lastRequestedHigh = high;
        irWorker.requestSynthesis(timeKnob, high, currentSampleRate);
    }

    // Collect a finished IR if one is waiting - juce::dsp::Convolution crossfades the outgoing
    // engine against the incoming one internally (see ConvolutionEngine's own header comment), so
    // this is where a click-free swap actually happens.
    juce::AudioBuffer<float> incomingIR;
    if (irWorker.tryPopSynthesizedIR(incomingIR))
        engine.loadIR(std::move(incomingIR));

    // Kept so Width/Converter below can be confined to the WET contribution only - see the
    // comment after engine.process(). Aura's own dry tap (a plain input copy mixed outside its
    // FDN engine) never passes through its own Bit Depth stage either; this reproduces the same
    // "dry stays pristine, only the wet signal is coloured" contract despite ConvolutionEngine
    // mixing dry/wet internally rather than exposing a wet-only tap.
    juce::AudioBuffer<float> dryCopy;
    dryCopy.makeCopyOf(buffer, true);

    const auto dryGainValue = dryParam->load() * 0.01f;

    engine.setPreDelayMs(preDelayMsParam->load());
    engine.setLowCutHz(lowCutHzParam->load());
    engine.setDryGain(dryGainValue);
    engine.setWetGain(wetParam->load() * 0.01f);
    engine.setBypassed(bypassParam != nullptr && bypassParam->get());

    engine.process(buffer, numSamples);

    // buffer now holds dryGainValue*dryCopy + wetGain*wetSignal (the engine's own internal mix -
    // see ConvolutionEngine::process()'s step 4). Subtract the dry contribution back out so Width
    // and Converter below colour ONLY the wet reverb signal, never the dry tap - a one-pole
    // filter's own impulse response (Converter's bandwidth stage) does not preserve "sample 0 ==
    // input", so applying it to the combined signal would have audibly smeared even a pure
    // Dry=100/Wet=0 passthrough (caught by InhaltProcessorTests's passthrough test before this
    // shipped). Slightly approximate for the few milliseconds an active Bypass transition is
    // ramping (this uses the plain dry param value, not the engine's own internally-ramped
    // bypass-blended coefficient) - a narrow edge case, and far better than colouring the dry
    // signal permanently.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::addWithMultiply(
            buffer.getWritePointer(channel), dryCopy.getReadPointer(channel), -dryGainValue, numSamples);

    // --- Width: mid/side scale, wet-only. 100% is a bit-identical no-op. ---
    const auto widthPercent = widthParam->load();
    if (! juce::approximatelyEqual(widthPercent, 100.0f))
    {
        const auto widthScale = widthPercent * 0.01f;
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto mid = (left[i] + right[i]) * 0.5f;
            const auto side = (left[i] - right[i]) * 0.5f * widthScale;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }

    // --- Converter: bandwidth + quantization only, no saturation (see this file's own comment
    // above and PluginProcessor.h's converterBandwidthL/R comment). Vintage is index 0. Wet-only,
    // same reasoning as Width above. ---
    const auto isVintage = converterParam->load() < 0.5f;
    const auto bandwidthHz = isVintage ? vintageBandwidthHz : modernBandwidthHz;
    converterBandwidthL.setCutoffHz(bandwidthHz, currentSampleRate);
    converterBandwidthR.setCutoffHz(bandwidthHz, currentSampleRate);

    {
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = converterBandwidthL.processSample(left[i]);
            right[i] = converterBandwidthR.processSample(right[i]);
            if (isVintage)
            {
                left[i] = std::round(left[i] * vintageQuantizationLevels) / vintageQuantizationLevels;
                right[i] = std::round(right[i] * vintageQuantizationLevels) / vintageQuantizationLevels;
            }
        }
    }

    // Add the untouched dry contribution back.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::addWithMultiply(
            buffer.getWritePointer(channel), dryCopy.getReadPointer(channel), dryGainValue, numSamples);

    setLatencySamples(engine.getLatencySamples());
}

// createEditor() lives in PluginEditor.cpp, not here (and Source/Tests/TestCreateEditorStub.cpp
// for the Tests target) - keeps PluginProcessor.cpp (and InhaltTests, which links only this file)
// free of any GUI/LookAndFeel/font dependency, matching every other plugin in this catalog.
bool InhaltAudioProcessor::hasEditor() const { return true; }

const juce::String InhaltAudioProcessor::getName() const { return JucePlugin_Name; }

bool InhaltAudioProcessor::acceptsMidi() const { return false; }
bool InhaltAudioProcessor::producesMidi() const { return false; }
bool InhaltAudioProcessor::isMidiEffect() const { return false; }
double InhaltAudioProcessor::getTailLengthSeconds() const { return 1.5; }

int InhaltAudioProcessor::getNumPrograms() { return factoryPresets.getNumPrograms(); }
int InhaltAudioProcessor::getCurrentProgram() { return factoryPresets.getCurrentProgram(); }
void InhaltAudioProcessor::setCurrentProgram(int index) { factoryPresets.setCurrentProgram(index, apvts); }
const juce::String InhaltAudioProcessor::getProgramName(int index) { return factoryPresets.getProgramName(index); }
void InhaltAudioProcessor::changeProgramName(int, const juce::String&) {}

void InhaltAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void InhaltAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new InhaltAudioProcessor();
}
