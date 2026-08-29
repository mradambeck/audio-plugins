#include "PluginProcessor.h"
#include "IntruderParameterMap.h"

#include <algorithm>
#include <cmath>

namespace
{
    juce::String withSign(float v, int decimals, const char* unit)
    {
        return (v > 0.0f ? juce::String("+") : juce::String()) + juce::String(v, decimals) + " " + unit;
    }
}

IntruderAudioProcessor::IntruderAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    decaySecondsParam = apvts.getRawParameterValue(decaySecondsParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    tiltDbParam = apvts.getRawParameterValue(tiltDbParamID);
    tighterParam = apvts.getRawParameterValue(tighterParamID);
    mixPercentParam = apvts.getRawParameterValue(mixPercentParamID);
    inputGainDbParam = apvts.getRawParameterValue(inputGainDbParamID);
    outputGainDbParam = apvts.getRawParameterValue(outputGainDbParamID);
    triggerThresholdDbParam = apvts.getRawParameterValue(triggerThresholdDbParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

IntruderAudioProcessor::~IntruderAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout IntruderAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Decay range: covers and extends past the ~0.19-0.49s measured on the reference captures (see
    // analysis/findings.md - the hardware's own 0.1-9.8 label isn't literal seconds), giving the
    // plugin's own Decay a full, musically useful range. Floor raised 0.1 -> 0.3s 2026-08-29 (Adam's
    // call) - ceiling unchanged at 10.0s.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{decaySecondsParamID, 1},
        "Decay",
        juce::NormalisableRange<float>(0.3f, 10.0f, 0.01f, 0.4f),
        1.5f,
        juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " s"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{preDelayMsParamID, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    // Tilt (H): -9..0dB matches the hardware's own measured range, extended slightly for
    // extrapolation headroom - see findings.md's "H" section for the tilt/pivot behavior this
    // drives (bass up / treble down as this goes negative, pivoting ~2kHz).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{tiltDbParamID, 1},
        "Low/High",
        juce::NormalisableRange<float>(-12.0f, 3.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{tighterParamID, 1},
        "Smoothing",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " %"; })));

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

    // NOT on the real hardware (IMPLEMENTATION.md has no threshold/sensitivity control at all) -
    // this plugin's own gate-retrigger detector needs one since a single fixed internal value
    // can't fit every source's gain staging (see IntruderFDNEngine.cpp's triggerHysteresisGapDb
    // comment for the empirical case). -36dB default matches this plugin's original fixed
    // behavior exactly, so existing sessions/presets don't change sound on load.
    //
    // Range was originally -60..0dB (the full linear-gain span) but both extremes turned out to
    // be non-functional in practice, not just extreme: a retrigger-count sweep against
    // BadVerb.wav (2026-08-29) showed 0 retriggers over 28s at 0dB (essentially never crosses)
    // and a flat 1 retrigger (stuck open, no more per-hit gating at all) at -50dB and below,
    // peaking around 200 retriggers near -25dB. Low end trimmed to -46dB, clear of that stuck-open
    // cliff. High end deliberately left at 0dB rather than trimmed to match (e.g. -4dB) - per
    // Adam, a dB control topping out anywhere other than 0 reads as an arbitrary/broken-looking
    // number to a user, even though -4..0dB is itself close to a dead zone (15 retriggers by
    // -5dB, versus 0 right at 0dB) - a UX call, not a claim that top-end range is doing much.
    // Exact cliff position is loop/gain-staging dependent, not a universal constant either way.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{triggerThresholdDbParamID, 1},
        "Threshold",
        juce::NormalisableRange<float>(-41.0f, 0.0f, 0.1f),
        -36.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{bypassParamID, 1}, "Bypass", false));

    return {params.begin(), params.end()};
}

void IntruderAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    sampleRateHz = sampleRate;
    engine.prepare(sampleRate);
}

void IntruderAudioProcessor::releaseResources() {}

bool IntruderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void IntruderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (bypassParam->load() > 0.5f)
        return;

    if (buffer.getNumChannels() < 2)
        return;

    // Phase 4 mapping layer: H and Tighter are hardware-meaningful UI values, not DSP-ready
    // coefficients - IntruderParameterMap converts them via the measured reference curves
    // (analysis/findings.md), kept separate from IntruderFDNEngine per IMPLEMENTATION.md's Phase 5
    // spec so the mapping can be refit without touching the DSP.
    engine.setDecaySeconds(decaySecondsParam->load());
    engine.setPreDelayMs(preDelayMsParam->load());
    engine.setTilt(IntruderParameterMap::mapTiltDbFromH(tiltDbParam->load()));
    engine.setSpacingMultiplier(IntruderParameterMap::mapTighterToSpacingMultiplier(tighterParam->load() * 0.01f));
    engine.setTriggerFloorDb(triggerThresholdDbParam->load());

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

bool IntruderAudioProcessor::hasEditor() const { return true; }

const juce::String IntruderAudioProcessor::getName() const { return JucePlugin_Name; }

bool IntruderAudioProcessor::acceptsMidi() const { return false; }
bool IntruderAudioProcessor::producesMidi() const { return false; }
bool IntruderAudioProcessor::isMidiEffect() const { return false; }
double IntruderAudioProcessor::getTailLengthSeconds() const { return 10.0; }

int IntruderAudioProcessor::getNumPrograms() { return 1; }
int IntruderAudioProcessor::getCurrentProgram() { return currentProgramIndex; }
void IntruderAudioProcessor::setCurrentProgram(int) {}
const juce::String IntruderAudioProcessor::getProgramName(int) { return {}; }
void IntruderAudioProcessor::changeProgramName(int, const juce::String&) {}

void IntruderAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary(*xml, destData);
    }
}

void IntruderAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new IntruderAudioProcessor();
}
