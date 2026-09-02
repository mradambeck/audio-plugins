#include "PluginProcessor.h"
#include "AuraParameterMap.h"

#include <algorithm>
#include <cmath>

namespace
{
    juce::String withSign(float v, int decimals, const char* unit)
    {
        return (v > 0.0f ? juce::String("+") : juce::String()) + juce::String(v, decimals) + " " + unit;
    }
}

AuraAudioProcessor::AuraAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    timeSecondsParam = apvts.getRawParameterValue(timeSecondsParamID);
    lowDbParam = apvts.getRawParameterValue(lowDbParamID);
    highDbParam = apvts.getRawParameterValue(highDbParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    mixPercentParam = apvts.getRawParameterValue(mixPercentParamID);
    inputGainDbParam = apvts.getRawParameterValue(inputGainDbParamID);
    outputGainDbParam = apvts.getRawParameterValue(outputGainDbParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

AuraAudioProcessor::~AuraAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AuraAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 0.1-5.5s is the measured range (ml-toolkit/effects/ambience/findings.md); extended to 8.0s
    // for extrapolation headroom the same way Intruder's Decay extends past its own measured
    // range. Close to literal seconds from ~1.1s up (see findings.md - unlike Intruder's own
    // heavily-compressed Decay scale on the same hardware unit).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{timeSecondsParamID, 1},
        "Time",
        juce::NormalisableRange<float>(0.1f, 8.0f, 0.01f, 0.5f),
        2.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " s"; })));

    // -8..+6dB matches the captured range. NOT wired to any DSP effect yet - see
    // AuraParameterMap.h's comment on why (the measured Low-attributable delta flips sign
    // depending on Time, not something safe to hardcode into a formula yet). Still exposed so the
    // control surface matches the real hardware and presets/automation don't need to change once
    // it is wired up.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowDbParamID, 1},
        "Low",
        juce::NormalisableRange<float>(-8.0f, 6.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    // -8..0dB matches the captured range - see findings.md's "High" section for the broadband
    // tilt (bass up/treble down as this goes negative) plus decay-shortening behavior this drives.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highDbParamID, 1},
        "High",
        juce::NormalisableRange<float>(-8.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{preDelayMsParamID, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{mixPercentParamID, 1},
        "Blend",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " %"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{inputGainDbParamID, 1},
        "Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{outputGainDbParamID, 1},
        "Volume",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void AuraAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRateHz = sampleRate;
    engine.prepare(sampleRate);
}

void AuraAudioProcessor::releaseResources() {}

bool AuraAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void AuraAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    if (buffer.getNumChannels() < 2)
        return;

    // Phase 4-equivalent mapping layer: Time/High are hardware-meaningful UI values, not DSP-
    // ready coefficients - AuraParameterMap converts them via the fitted reference curves
    // (ml-toolkit/effects/ambience/findings.md), kept separate from AuraFDNEngine so the mapping
    // can be refit without touching the DSP (same convention as Intruder).
    const auto bandGains = AuraParameterMap::mapTimeAndHighToBandGains(timeSecondsParam->load(), highDbParam->load());
    engine.setBandGains(bandGains.highBandGain, bandGains.lowBandGain);
    engine.setDampingWeight(bandGains.dampingWeight);
    engine.setPreDelayMs(preDelayMsParam->load());

    const auto inputGain = std::pow(10.0f, inputGainDbParam->load() / 20.0f);
    const auto outputGain = std::pow(10.0f, outputGainDbParam->load() / 20.0f);
    const auto mix = std::clamp(mixPercentParam->load() * 0.01f, 0.0f, 1.0f);

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    juce::HeapBlock<float> dryL(static_cast<size_t>(numSamples));
    juce::HeapBlock<float> dryR(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
    {
        dryL[n] = left[n];
        dryR[n] = right[n];
        left[n] *= inputGain;
        right[n] *= inputGain;
    }

    engine.processStereo(left, right, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto wetL = left[n] * outputGain;
        const auto wetR = right[n] * outputGain;
        left[n] = dryL[n] * (1.0f - mix) + wetL * mix;
        right[n] = dryR[n] * (1.0f - mix) + wetR * mix;
    }
}

// Defined in PluginEditor.cpp (not here) so this file has no GUI dependency.

bool AuraAudioProcessor::hasEditor() const { return true; }

const juce::String AuraAudioProcessor::getName() const { return JucePlugin_Name; }

bool AuraAudioProcessor::acceptsMidi() const { return false; }
bool AuraAudioProcessor::producesMidi() const { return false; }
bool AuraAudioProcessor::isMidiEffect() const { return false; }
double AuraAudioProcessor::getTailLengthSeconds() const { return 10.0; }

int AuraAudioProcessor::getNumPrograms() { return 1; }
int AuraAudioProcessor::getCurrentProgram() { return currentProgramIndex; }
void AuraAudioProcessor::setCurrentProgram(int) {}
const juce::String AuraAudioProcessor::getProgramName(int) { return {}; }
void AuraAudioProcessor::changeProgramName(int, const juce::String&) {}

void AuraAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void AuraAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuraAudioProcessor();
}
