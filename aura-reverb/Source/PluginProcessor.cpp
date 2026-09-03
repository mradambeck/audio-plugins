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
    lowCutHzParam = apvts.getRawParameterValue(lowCutHzParamID);
    highDbParam = apvts.getRawParameterValue(highDbParamID);
    preDelayMsParam = apvts.getRawParameterValue(preDelayMsParamID);
    bitDepthParam = apvts.getRawParameterValue(bitDepthParamID);
    dryParam = apvts.getRawParameterValue(dryParamID);
    wetParam = apvts.getRawParameterValue(wetParamID);
    bypassParam = apvts.getRawParameterValue(bypassParamID);
}

AuraAudioProcessor::~AuraAudioProcessor() = default;

const juce::StringArray& AuraAudioProcessor::getConverterChoices()
{
    static const juce::StringArray choices { "8 bit", "16 bit", "24 bit" };
    return choices;
}

float AuraAudioProcessor::getBitDepthForChoiceIndex(int index)
{
    switch (juce::jlimit(0, 2, index))
    {
        case 0:  return 8.0f;
        case 1:  return 16.0f;
        default: return 24.0f;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AuraAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 0.1-5.5s is the measured range (ml-toolkit/effects/ambience/findings.md); extended to 8.0s
    // for extrapolation headroom the same way Intruder's own Decay extends past its own measured
    // range. Close to literal seconds from ~1.1s up (see findings.md). Displayed as "Decay" (Adam,
    // 2026-09-02) even though the underlying parameter ID stays timeSeconds - unlike Intruder's
    // OWN "Decay" control, which is a heavily-compressed display scale on the same hardware unit,
    // Aura's is close to literal seconds; same label, different behavior, don't assume parity
    // between the two plugins' "Decay" knobs just because they share a name.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{timeSecondsParamID, 1},
        "Decay",
        juce::NormalisableRange<float>(0.1f, 8.0f, 0.01f, 0.5f),
        2.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("s")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " s"; })));

    // Repurposed from the real hardware's own "Low" knob (was -8..+6dB, unwired - see
    // AuraFDNEngine.h's setLowCutHz() comment for why: direct measurement found no onset-tone
    // effect and only a small, sign-inconsistent decay effect conditional on High, nothing safe to
    // hardcode). Now a plain 0-300Hz utility high-pass on the dry input, ahead of pre-delay and
    // the whole effect - matches Caverns' own "Low Cut" naming/range-shape convention (though
    // Caverns filters its wet tap, not the dry input - see AuraFDNEngine.h for why Aura's needs to
    // sit earlier). Default 0Hz = off, a genuine bypass (AuraFDNEngine::lowCutActive).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{lowCutHzParamID, 1},
        "Low Cut",
        juce::NormalisableRange<float>(0.0f, 300.0f, 1.0f, 0.5f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return v <= 0.0f ? juce::String("Off") : juce::String(v, 0) + " Hz"; })));

    // -8..0dB matches the captured range - see findings.md's "High" section for the broadband
    // tilt (bass up/treble down as this goes negative) plus decay-shortening behavior this drives.
    // Displayed as "Color" (Adam, 2026-09-02) - underlying param ID stays highDb.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{highDbParamID, 1},
        "Color",
        juce::NormalisableRange<float>(-8.0f, 0.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("dB")
            .withStringFromValueFunction([](float v, int) { return withSign(v, 1, "dB"); })));

    // 0-200ms matches the real RMX16 Ambience program's own pre-delay range (per Adam, 2026-09-02).
    // AuraFDNEngine already allocates 200ms of pre-delay buffer headroom, so this needed no engine
    // change - the parameter was just capped lower than the hardware for no good reason.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{preDelayMsParamID, 1},
        "Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("ms")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + " ms"; })));

    // "Converter" (Adam, 2026-09-03): a discrete 3-position bit-depth selector - a button that
    // cycles through 8/16/24 bit, with an LED list on the panel showing which is active (matches
    // flux-phaser's own Stages Shift-button/LED-list pattern) - replacing the original continuous
    // 8-24 knob. Underlying param ID stays bitDepth (unchanged since the original knob) -
    // AuraFDNEngine::setBitDepth still takes a plain bit count; getBitDepthForChoiceIndex() below
    // maps this choice's index to that value, same split as flux-phaser's stagesParam/
    // getStageCountForChoiceIndex(). 24 bit is still a GENUINE bypass (AuraFDNEngine::
    // bitDepthActive) - unchanged from the original knob's contract.
    //
    // Default index 0 (8 bit) is Adam's explicit choice for this 3-position control - a heavier,
    // deliberately more colored default than the old continuous knob's 16-bit default, which was
    // chosen instead because it reproduces the real hardware's own measured ~90dB dynamic-range
    // ceiling (see README.md's "How it works" for that measurement). This control is no longer
    // trying to default to hardware accuracy; the choice of WHICH default sits at 8/16/24 is a
    // separate decision from the mechanism, and could change again without touching the DSP.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{bitDepthParamID, 1},
        "Converter",
        getConverterChoices(),
        0));

    // Independent Dry/Wet level pair (Adam, 2026-09-02), replacing the old single Blend + Volume
    // pair - same convention as caverns-delay's own dryParamID/wetParamID (percent, not dB;
    // multiplicative gain, not a crossfade). Wet goes past 100% (unity) up to 200% so the reverb
    // can be pushed louder than the dry tap, matching Caverns' own headroom rationale.
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
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f),
        50.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel("%")
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 1) + "%"; })));

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
    const auto decayParams = AuraParameterMap::mapTimeAndHighToDecayParams(timeSecondsParam->load(), highDbParam->load());
    engine.setBandGains(decayParams.decayGain, decayParams.decayGain);
    engine.setDampingWeight(decayParams.dampingWeight);
    engine.setInputTilt(AuraParameterMap::mapInputTiltDb(highDbParam->load()));
    engine.setPreDelayMs(preDelayMsParam->load());
    engine.setBitDepth(getBitDepthForChoiceIndex((int) bitDepthParam->load()));
    engine.setLowCutHz(lowCutHzParam->load());
    engine.setSubBassGain(AuraParameterMap::mapTimeToSubBassGain(timeSecondsParam->load()));

    // Independent Dry/Wet level pair, not a crossfade - see createParameterLayout()'s comment.
    // Percent-to-linear, matching caverns-delay's own dryGain/wetGain convention exactly.
    const auto dryGain = dryParam->load() * 0.01f;
    const auto wetGain = wetParam->load() * 0.01f;

    const auto numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);

    juce::HeapBlock<float> dryL(static_cast<size_t>(numSamples));
    juce::HeapBlock<float> dryR(static_cast<size_t>(numSamples));
    for (int n = 0; n < numSamples; ++n)
    {
        dryL[n] = left[n];
        dryR[n] = right[n];
    }

    engine.processStereo(left, right, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        const auto wetL = left[n] * wetGain;
        const auto wetR = right[n] * wetGain;
        left[n] = dryL[n] * dryGain + wetL;
        right[n] = dryR[n] * dryGain + wetR;
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
